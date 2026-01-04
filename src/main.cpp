#include <spa/param/video/format-utils.h>
#include <spa/debug/types.h>
#include <spa/param/video/type-info.h>

#include <pipewire/pipewire.h>

#include "rtc/rtc.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <cstring>
#include <atomic>
#include <csignal>


// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>

#include <linux/uinput.h>
#include <unistd.h>
#include <fcntl.h>
}


// Helper (for error handling)
#define PW_THROW_IF(cond, msg) if (cond) throw std::runtime_error(msg);

using nlohmann::json;

static const uint32_t SSRC = 42;
static const int RTP_MTU = 1200;


#define send_instantly false
// #define send_instantly true

// RTP AVIO write callback: called by FFmpeg for each RTP packet
static int rtp_avio_write(void *opaque, const uint8_t *buf, int buf_size) {
    try {
        auto *track = reinterpret_cast<rtc::Track *>(opaque);
        // printf("Would have writen %d\n", buf_size);
        if (track->isOpen())
            track->send(reinterpret_cast<const std::byte *>(buf), buf_size);
    }
    catch (const std::runtime_error& e) {
        // Catch the specific exception type
        std::cerr << "Caught a runtime error: " << e.what() << std::endl;
    }

    return buf_size;
    
}

typedef struct {
        uint32_t fps = 60;

        struct pw_main_loop *loop;
        struct pw_stream *stream;

        struct spa_video_info format;


        std::shared_ptr<rtc::Track> track;
        std::shared_ptr<rtc::DataChannel> datatrack;


        AVFrame *latest_frame = nullptr;
        AVFrame *enc_frame = nullptr;
        AVPacket *encPkt = nullptr;
        int64_t frame_pts = 0;
        int real_width = 0; // used for stride jumps, as a quick patch for requirements of 2x2
        int width = 0, height = 0;
        int streamWidth = 0, streamHeight = 0;
        // int streamWidth = 640, streamHeight = 480;
        SwsContext *sws = nullptr; // For format conversion

        AVFormatContext *rtpFmt;
        AVCodecContext *encCtx;



        int uinput_fd;
} stateData;


static bool setup_libav_buffers(stateData *data) {
    printf("REALLOCING BUFFERS\n");
    int width = data->width;
    int height = data->height;
    int streamWidth = data->streamWidth;
    int streamHeight = data->streamHeight;

    int ret = 0;

    data->latest_frame = av_frame_alloc();
    data->latest_frame->format = AV_PIX_FMT_BGR0;
    data->latest_frame->width  = width;
    data->latest_frame->height = height;
    ret = av_frame_get_buffer(data->latest_frame, 32);
    PW_THROW_IF(ret < 0, "Failed to get latest_frame frame buffer");

    data->enc_frame = av_frame_alloc();
    data->enc_frame->format = AV_PIX_FMT_YUV420P;
    data->enc_frame->width = streamWidth;
    data->enc_frame->height = streamHeight;
    ret = av_frame_get_buffer(data->enc_frame, 32);
    PW_THROW_IF(ret < 0, "Failed to get enc_frame frame buffer");
    

    data->encPkt = av_packet_alloc();



    int TARGET_FPS = data->fps; // DOSNT MATTER AT ALL


    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    data->encCtx = avcodec_alloc_context3(encoder);
    data->encCtx->width = streamWidth;
    data->encCtx->height = streamHeight;
    data->encCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    data->encCtx->time_base = AVRational{1, TARGET_FPS};
    data->encCtx->framerate = AVRational{TARGET_FPS, 1};
    data->encCtx->gop_size = TARGET_FPS;
    data->encCtx->max_b_frames = 0;


    // new
    data->encCtx->has_b_frames = 0;
    data->encCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    AVDictionary *codec_opts = NULL;
    av_dict_set(&codec_opts, "preset", "ultrafast", 0);
    av_dict_set(&codec_opts, "tune", "zerolatency", 0);


    ret = avcodec_open2(data->encCtx, encoder, &codec_opts);
    av_dict_free(&codec_opts);

    PW_THROW_IF(ret < 0, "avcodec_open2 failed for encoder");

    // Create swscale conversion context (RGBA -> YUV420P)
    data->sws = sws_getContext(
        width, height, AV_PIX_FMT_BGR0,
        streamWidth, streamHeight, AV_PIX_FMT_YUV420P,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );
    PW_THROW_IF(!data->sws, "sws_getContext failed");

    // RTP FFmpeg output setup (same as your code)
    data->rtpFmt = nullptr;
    ret = avformat_alloc_output_context2(&data->rtpFmt, nullptr, "rtp", nullptr);
    PW_THROW_IF(ret < 0 || !data->rtpFmt, "avformat_alloc_output_context2 failed");

    AVStream *rtpStream = avformat_new_stream(data->rtpFmt, nullptr);
    PW_THROW_IF(!rtpStream, "avformat_new_stream failed");
    avcodec_parameters_from_context(rtpStream->codecpar, data->encCtx);
    rtpStream->time_base = data->encCtx->time_base;

    int avio_buf_size = RTP_MTU + 256;
    uint8_t *avioBuffer = (uint8_t *)av_malloc(avio_buf_size);

    AVIOContext *avio = avio_alloc_context(
        avioBuffer, avio_buf_size, 1, data->track.get(), nullptr, rtp_avio_write, nullptr);

    data->rtpFmt->pb = avio;
    data->rtpFmt->packet_size = RTP_MTU;
    data->rtpFmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "payload_type", "96", 0);
    av_dict_set(&opts, "ssrc", std::to_string(SSRC).c_str(), 0);
    ret = avformat_write_header(data->rtpFmt, &opts);
    av_dict_free(&opts);
    PW_THROW_IF(ret < 0, "avformat_write_header failed");

    printf("ALL BUFFERS ALLOCATED\n");


    return true;
}


#define IOCTL_WRAPPER(call) \
    ({ \
        typeof(call) ret = (call); \
        if (ret < 0) { \
            fprintf(stderr, "IOCTL failed at %s:%d: %s\n", __FILE__, __LINE__, strerror(errno)); \
            exit(EXIT_FAILURE); \
        } \
        ret; \
    })


static bool setup_uinput(stateData *data) {
    data->uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (data->uinput_fd < 0) {
        perror("Damn no uinput");
        return false;
    }
    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_SET_EVBIT, EV_REL));
    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_SET_RELBIT, REL_X));
    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_SET_RELBIT, REL_Y));


    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_SET_EVBIT, EV_SYN));


    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_SET_EVBIT, EV_KEY));
    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_SET_KEYBIT, BTN_LEFT));
    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_SET_KEYBIT, BTN_RIGHT));
    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_SET_KEYBIT, BTN_MIDDLE));


    struct uinput_setup usetup = {0};
    // memset(usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "Example dev!");

    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_DEV_SETUP, &usetup));
    IOCTL_WRAPPER(ioctl(data->uinput_fd, UI_DEV_CREATE));

    return true;
}

void emit_uinput(int fd, int type, int code, int val)
{
   struct input_event ie;

   ie.type = type;
   ie.code = code;
   ie.value = val;
   /* timestamp values below are ignored */
   ie.time.tv_sec = 0;
   ie.time.tv_usec = 0;

   ssize_t n = write(fd, &ie, sizeof(ie));
   if (n<0) {
        perror("Failed to write to uinput");
   }
}
static void recive_data_message(stateData *data, rtc::message_variant recived) {
    if (!std::holds_alternative<rtc::binary>(recived)) return;
    auto bin_data = std::get<rtc::binary>(recived);
    if (bin_data.size() == 3 && data->uinput_fd >= 0) {
        int8_t x_movement = std::to_integer<int8_t>(bin_data.at(0));
        int8_t y_movement = std::to_integer<int8_t>(bin_data.at(1));
        int8_t mouse_buttons = std::to_integer<int8_t>(bin_data.at(2));
        printf("Test: %d, %d, %d\n", x_movement, y_movement, mouse_buttons);
        emit_uinput(data->uinput_fd, EV_REL, REL_X, x_movement);
        emit_uinput(data->uinput_fd, EV_REL, REL_Y, y_movement);
        emit_uinput(data->uinput_fd, EV_KEY, BTN_LEFT,      (mouse_buttons>>0) & 1);
        emit_uinput(data->uinput_fd, EV_KEY, BTN_RIGHT,     (mouse_buttons>>1) & 1);
        emit_uinput(data->uinput_fd, EV_KEY, BTN_MIDDLE,    (mouse_buttons>>2) & 1);
        emit_uinput(data->uinput_fd, EV_SYN, SYN_REPORT, 0);

    }
    //  x_movement
    // printf("Test: %d, %d\n", x_movement, y_movement);
}

static void setup_RTC(stateData *data) {
    // ---- WebRTC setup ----
    rtc::InitLogger(rtc::LogLevel::Debug);

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun2.l.google.com:19302");


    auto pc = std::make_shared<rtc::PeerConnection>(config);
    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "State: " << state << std::endl;
    });
    pc->onGatheringStateChange([pc](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            json msg = {
                {"type", description->typeString()},
                {"sdp", std::string(description.value())}
            };
            std::cout << msg << std::endl;
        }
    });

    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(SSRC, "video-send");
    data->track = pc->addTrack(media);

    data->datatrack = pc->createDataChannel("video-data", {
        protocol: "hi",
    });

    data->datatrack.get()->onMessage([data](rtc::message_variant a){
        recive_data_message(data,a);
    });

    // data->datatrack.get
    
    pc->setLocalDescription();

    std::cout << "Paste browser answer (JSON):" << std::endl;
    std::string sdp;
    std::getline(std::cin, sdp);
    json j = json::parse(sdp);
    rtc::Description answer(j["sdp"].get<std::string>(), j["type"].get<std::string>());
    pc->setRemoteDescription(answer);

}

static void send_latest_data(stateData *data) {
    // Convert RGBA -> YUV420P in-place
    sws_scale(
        data->sws,
        data->latest_frame->data, data->latest_frame->linesize, 0, data->height,
        data->enc_frame->data, data->enc_frame->linesize
    );
    data->enc_frame->pts = data->frame_pts++;

#if send_instantly
    int64_t now_us = av_gettime();
    data->enc_frame->pts =
        av_rescale_q(now_us, AVRational{1,1000000}, data->encCtx->time_base);

#endif

    // Encode and stream as before:
    avcodec_send_frame(data->encCtx, data->enc_frame);
    while (avcodec_receive_packet(data->encCtx, data->encPkt) == 0) {
        // printf("Sending frame remote\n");
        try {
            if (data->encPkt) // !just_cleared && 
                av_write_frame(data->rtpFmt, data->encPkt);
        
            if (data->rtpFmt && data->rtpFmt->pb)
                avio_flush(data->rtpFmt->pb); // force immediate write to AVIO callback
            av_packet_unref(data->encPkt);
        }
        catch (const std::runtime_error& e) {
            // Catch the specific exception type
            std::cerr << "Caught a runtime error: " << e.what() << std::endl;
        }
    }
}
/* [on_process] */
static void on_process(void *userdata) {
        // printf("USER DATA PROVIDED!\n");
        stateData *data = (stateData *) userdata;
        
        struct pw_buffer *b;
        // struct spa_buffer *buf;

        if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
                pw_log_warn("out of buffers: %m");
                return;
        }

        if (data->width == 0) {
            pw_stream_queue_buffer(data->stream, b);
            return;
        };


        auto spa_buffer = b->buffer;

        if (!spa_buffer || !spa_buffer->datas[0].data) {
            // Return the buffer to the stream and exit
            pw_stream_queue_buffer(data->stream, b);
            return;
        }

        // Source pointer and stride (assume RGBA)
        uint8_t *src = (uint8_t*)spa_buffer->datas[0].data;
        int stride = data->real_width * 4;

        int just_cleared = false;

        // Allocate latest_frame if needed
        if (data->latest_frame && (data->latest_frame->width != data->width || data->latest_frame->height != data->height)) {
            printf("FREEING BUFFERS\n");

            if (data->encCtx) {
                avcodec_send_frame(data->encCtx, nullptr); // flush
                while (avcodec_receive_packet(data->encCtx, data->encPkt) == 0) {
                    if (data->encPkt)
                        av_packet_unref(data->encPkt);
                }
                avcodec_free_context(&data->encCtx);
                data->encCtx = nullptr;
            }


            av_frame_free(&data->latest_frame); // idk may work :P // May not, @grok is this correct
            av_frame_free(&data->enc_frame); 
            av_packet_free(&data->encPkt);
            data->latest_frame = nullptr;

            just_cleared = true;


            if (data->rtpFmt) {
                avformat_free_context(data->rtpFmt);
                data->rtpFmt = nullptr;
            }
            if (data->sws) {
                sws_freeContext(data->sws);
                data->sws = nullptr;
            }

            printf("BUFFERS FREED\n");

        }
        if (!data->latest_frame) { // Lifetime of enc_frame, latest_frame, and encPkt are synced.
            if (setup_libav_buffers(data)) {
                pw_stream_queue_buffer(data->stream, b);
                return;
            }
        }

        // Copy lines into latest_frame
        for (int y = 0; y < data->height; ++y) {
            memcpy(data->latest_frame->data[0] + y * data->latest_frame->linesize[0],
                src + y * stride, stride);
        }

        #if send_instantly
        send_latest_data(data);
        #endif
        // printf("got a frame of size %d\n", spa_buffer->datas[0].chunk->size);

        pw_stream_queue_buffer(data->stream, b);
}
/* [on_process] */

static void on_param_changed(void *userdata, uint32_t id, const struct spa_pod *param) {
        stateData *data = (stateData *)userdata;
        printf("Format requested!\n");
        if (param == NULL || id != SPA_PARAM_Format) {
                printf("\tRETURNA, %d  %p\n", id, param);
                return;
        }

        if (spa_format_parse(param,
                        &data->format.media_type,
                        &data->format.media_subtype) < 0) {
                printf("\tRETURNB\n");
                return;
        }

        if (data->format.media_type != SPA_MEDIA_TYPE_video ||
            data->format.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
                printf("\tRETURNC\n");
                return;
        }

        if (spa_format_video_raw_parse(param, &data->format.info.raw) < 0) {
                printf("\tRETURND\n");
                return;
        }

        printf("got video format:\n");
        printf("  format: %d (%s)\n", data->format.info.raw.format,
                        spa_debug_type_find_name(spa_type_video_format,
                                data->format.info.raw.format));
        printf("  size: %dx%d\n", data->format.info.raw.size.width,
                        data->format.info.raw.size.height);

        int prescaled_height = data->format.info.raw.size.height&(~1); // Make even line count, rounds down to prevent errors in h264 
        int prescaled_width = data->format.info.raw.size.width&(~1); // Make even line count, rounds down to prevent errors in h264 
        data->real_width = data->format.info.raw.size.width;
        data->height = prescaled_height;
        data->width = prescaled_width;
        data->streamHeight = prescaled_height;
        data->streamWidth = prescaled_width;
                    
        printf("  framerate: %d/%d\n", data->format.info.raw.framerate.num,
                        data->format.info.raw.framerate.denom);

}

static const struct pw_stream_events stream_events = {
        PW_VERSION_STREAM_EVENTS,
        .param_changed = on_param_changed,
        .process = on_process,
};


static void send_frame_timer(void *userdata, long unsigned int a) {
    // printf("Timer! %d\n", a);
    stateData *data = (stateData *)userdata;

    // Call on_process to send a frame (this could be a duplicate frame or an empty frame if no new content)
    if (data->latest_frame)
        send_latest_data(data);  // This sends a frame, whether new or not
}


int main(int argc, char *argv[]) {
        stateData data = { 0, };
        data.fps = 60;

        const struct spa_pod *params[1];
        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        struct pw_properties *props;

        pw_init(&argc, &argv);

        data.loop = pw_main_loop_new(NULL);

        props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
                        NULL);
        if (argc > 1)
                pw_properties_set(props, PW_KEY_TARGET_OBJECT, argv[1]);

        data.stream = pw_stream_new_simple(
                        pw_main_loop_get_loop(data.loop),
                        "video-capture",
                        props,
                        &stream_events,
                        &data);

        struct spa_rectangle min_size = SPA_RECTANGLE(1, 1);
        struct spa_rectangle max_size = SPA_RECTANGLE(4096, 4096);
        struct spa_rectangle default_size = SPA_RECTANGLE(1920, 1080);

        struct spa_fraction min_framerate = SPA_FRACTION(0, 1);
        struct spa_fraction max_framerate = SPA_FRACTION(1000, 1);
        struct spa_fraction default_framerate = SPA_FRACTION(data.fps, 1);

        params[0] = (const struct spa_pod*) spa_pod_builder_add_object(&b,
                SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
                SPA_FORMAT_mediaType,       SPA_POD_Id(SPA_MEDIA_TYPE_video),
                SPA_FORMAT_mediaSubtype,    SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
                SPA_FORMAT_VIDEO_format,    SPA_POD_CHOICE_ENUM_Id(2,
                                                SPA_VIDEO_FORMAT_BGRx,
                                                SPA_VIDEO_FORMAT_BGRx),
                SPA_FORMAT_VIDEO_size,      SPA_POD_CHOICE_RANGE_Rectangle(
                                                &default_size,
                                                &min_size,
                                                &max_size),
                SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
                                                &default_framerate,
                                                &min_framerate,
                                                &max_framerate));

        int re = pw_stream_connect(data.stream,
                          PW_DIRECTION_INPUT,
                          PW_ID_ANY,
                        //   147,
                          (enum pw_stream_flags) (PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                          params, 1);
        printf("Hi me! GH, %d\n", re);


        setup_RTC(&data);


        #if send_instantly
        #else
        const int interval_ms = 1000 / data.fps; // 16ms for 60 FPS
        auto frame_timer = pw_loop_add_timer(pw_main_loop_get_loop(data.loop), send_frame_timer, (void*) &data);
        PW_THROW_IF(!frame_timer, "Failed to create timer for frame sending");
        // Set the timer to trigger every interval_ms milliseconds
        struct timespec interval = { .tv_sec = 0, .tv_nsec = 1000000 * (1000 / data.fps) };  // 60 FPS in ns
        struct timespec value = { .tv_sec = 0, .tv_nsec = 1000000 * (1000 / data.fps) };  // 60 FPS in ns

        // Set the initial value and interval for the timer
        pw_loop_update_timer(pw_main_loop_get_loop(data.loop), frame_timer, &value, &interval, true);

        // pw_timer_set(frame_timer, interval_ms, interval_ms);  // The same interval for both initial delay and periodicity
        #endif

        setup_uinput(&data);


        pw_main_loop_run(data.loop);

        pw_stream_destroy(data.stream);
        pw_main_loop_destroy(data.loop);

        return 0;
}
