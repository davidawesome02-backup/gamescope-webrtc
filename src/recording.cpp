

#include "recording.hpp"



#define PW_THROW_IF(cond, msg) if (cond) throw std::runtime_error(msg);


static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    while (*pix_fmts != AV_PIX_FMT_NONE) {
        if (*pix_fmts == AV_PIX_FMT_VULKAN)
            return *pix_fmts;
        pix_fmts++;
    }
    fprintf(stderr, "Vulkan pixel format not offered by encoder.\n");
    return AV_PIX_FMT_NONE;
}


static bool setup_libav_buffers(stateData *data) {
    int width = data->width;
    int height = data->height;
    int streamWidth = data->streamWidth;
    int streamHeight = data->streamHeight;
    int ret = 0;

    // 1. Vulkan HW device
    ret = av_hwdevice_ctx_create(&data->hw_device_ctx, AV_HWDEVICE_TYPE_VULKAN, NULL, NULL, 0);
    PW_THROW_IF(ret < 0, "Failed to create Vulkan hwdevice context");

    // 2. Vulkan HW frames context (YUV420P format)
    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(data->hw_device_ctx);
    PW_THROW_IF(!hw_frames_ref, "Could not alloc Vulkan hwframe context");

    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)hw_frames_ref->data;
    frames_ctx->format = AV_PIX_FMT_VULKAN;
    frames_ctx->sw_format = AV_PIX_FMT_NV12; // <-- CPU-side format
    frames_ctx->width = streamWidth;
    frames_ctx->height = streamHeight;
    frames_ctx->initial_pool_size = 2;
    ret = av_hwframe_ctx_init(hw_frames_ref);
    PW_THROW_IF(ret < 0, "Failed to init Vulkan hwframe ctx");

    // 3. Encoder config
    const AVCodec *encoder = avcodec_find_encoder_by_name("h264_vulkan");
    PW_THROW_IF(!encoder, "Failed to get h264_vulkan encoder");

    data->encCtx = avcodec_alloc_context3(encoder);
    data->encCtx->width = streamWidth;
    data->encCtx->height = streamHeight;
    data->encCtx->pix_fmt = AV_PIX_FMT_VULKAN;
    data->encCtx->time_base = AVRational{1, (int)data->fps};
    data->encCtx->framerate = AVRational{(int)data->fps, 1};
    data->encCtx->gop_size = data->fps;
    data->encCtx->max_b_frames = 0;
    data->encCtx->has_b_frames = 0;
    data->encCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // av_opt_set(data->encCtx->priv_data, "repeat_headers", "0", 0);
    data->encCtx->hw_device_ctx = av_buffer_ref(data->hw_device_ctx);
    data->encCtx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);

    // Optional: Rate control settings (not strictly required)
    AVDictionary *codec_opts = NULL;
    av_dict_set(&codec_opts, "preset", "ultrafast", 0);
    // av_dict_set(&codec_opts, "tune", "zerolatency", 0);
    av_dict_set(&codec_opts, "qp", "18", 0); // Optional: Fixed QP

    ret = avcodec_open2(data->encCtx, encoder, &codec_opts);
    av_dict_free(&codec_opts);
    PW_THROW_IF(ret < 0, "avcodec_open2 failed for encoder");

    // 4. RTP output setup (unchanged)
    data->rtpFmt = nullptr;
    ret = avformat_alloc_output_context2(&data->rtpFmt, nullptr, "rtp", nullptr);
    PW_THROW_IF(ret < 0 || !data->rtpFmt, "avformat_alloc_output_context2 failed");
    AVStream *rtpStream = avformat_new_stream(data->rtpFmt, nullptr);
    PW_THROW_IF(!rtpStream, "avformat_new_stream failed");
    avcodec_parameters_from_context(rtpStream->codecpar, data->encCtx);
    rtpStream->time_base = data->encCtx->time_base;

    int avio_buf_size = RTP_MTU + 256;
    uint8_t *avioBuffer = (uint8_t *)av_malloc(avio_buf_size);
    AVIOContext *avio = avio_alloc_context(avioBuffer, avio_buf_size, 1, data->track.get(), nullptr, rtp_avio_write, nullptr);
    data->rtpFmt->pb = avio;
    data->rtpFmt->packet_size = RTP_MTU;
    data->rtpFmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "payload_type", "96", 0);
    av_dict_set(&opts, "ssrc", std::to_string(SSRC).c_str(), 0);
    ret = avformat_write_header(data->rtpFmt, &opts);
    av_dict_free(&opts);
    PW_THROW_IF(ret < 0, "avformat_write_header failed");

    // 5. Buffer setup
    data->latest_frame = av_frame_alloc();
    data->latest_frame->format = AV_PIX_FMT_NV12;
    data->latest_frame->width = width;
    data->latest_frame->height = height;
    ret = av_frame_get_buffer(data->latest_frame, 32);
    PW_THROW_IF(ret < 0, "Failed to get latest_frame frame buffer");

    // Vulkan hw frame for encoder
    data->hw_frame = av_frame_alloc();
    data->hw_frame->format = AV_PIX_FMT_VULKAN;
    data->hw_frame->width = streamWidth;
    data->hw_frame->height = streamHeight;
    data->hw_frame->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    ret = av_hwframe_get_buffer(hw_frames_ref, data->hw_frame, 0);
    PW_THROW_IF(ret < 0, "Failed to get GPU hw_frame buffer");

    // Packet allocation
    data->encPkt = av_packet_alloc();

    std::cout << "ALL BUFFERS ALLOCATED" << std::endl;
    return true;
}

// CPU conversion + upload + encode
static void send_latest_data(stateData *data) {
    data->latest_frame->pts = data->frame_pts++;

    // Upload NV12 (CPU) to Vulkan (GPU)
    data->hw_frame->pts = data->latest_frame->pts;
    int ret = av_hwframe_transfer_data(data->hw_frame, data->latest_frame, 0);
    PW_THROW_IF(ret < 0, "av_hwframe_transfer_data failed (NV12 CPU->Vulkan)");

    // Encode as usual
    avcodec_send_frame(data->encCtx, data->hw_frame);
    while (avcodec_receive_packet(data->encCtx, data->encPkt) == 0) {
        try {
            if (data->encPkt)
                av_write_frame(data->rtpFmt, data->encPkt);
            if (data->rtpFmt && data->rtpFmt->pb)
                avio_flush(data->rtpFmt->pb);
            av_packet_unref(data->encPkt);
        } catch (const std::runtime_error& e) {
            std::cerr << "Caught a runtime error: " << e.what() << std::endl;
        }
    }
}

static void send_frame_timer(void *userdata, long unsigned int a) {
    stateData *data = (stateData *)userdata;

    // Call on_process to send a frame (this could be a duplicate frame or an empty frame if no new content)
    if (data->latest_frame)
        send_latest_data(data);  // This sends a frame, whether new or not
}



static void on_param_changed(void *userdata, uint32_t id, const struct spa_pod *param) {


        stateData *data = (stateData *)userdata;


        printf("Format requested!\n");
        if (param == NULL || id != SPA_PARAM_Format) {
                printf("\tFormat parameter not specifed, %d  %p\n", id, param);
                if (id == 4 && param == NULL) exit(0); // Exit if no more stream. TODO: exit if pid not found mby..?
                return;
        }

        if (spa_format_parse(param,
                        &data->format.media_type,
                        &data->format.media_subtype) < 0) {
                printf("\tUnable to parse format for media type\n");
                return;
        }

        if (data->format.media_type != SPA_MEDIA_TYPE_video ||
            data->format.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
                printf("\tFormat media is not correct\n");
                return;
        }

        if (spa_format_video_raw_parse(param, &data->format.info.raw) < 0) {
                printf("\tUnable to locate raw video stream\n");
                return;
        }

        printf("got video format:\n");
        printf("  format: %d (%s)\n", data->format.info.raw.format,
                        spa_debug_type_find_name(spa_type_video_format,
                                data->format.info.raw.format));
        printf("  size: %dx%d\n", data->format.info.raw.size.width,
                        data->format.info.raw.size.height);

        // int prescaled_height = data->format.info.raw.size.height
        // int prescaled_width = data->format.info.raw.size.width&(~1); // Make even line count, rounds down to prevent errors in h264 
        data->real_width = data->format.info.raw.size.width;

        data->height = data->format.info.raw.size.height;
        data->width = data->format.info.raw.size.width;

        data->streamHeight = data->format.info.raw.size.height;
        data->streamWidth = data->format.info.raw.size.width;
                    
        printf("  framerate: %d/%d\n", data->format.info.raw.framerate.num,
                        data->format.info.raw.framerate.denom);

}


/* [on_process] */
static void on_process(void *userdata) {
        stateData *data = (stateData *) userdata;
        
        struct pw_buffer *b;

        if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
                pw_log_warn("out of buffers: %m");
                return;
        }

        if (data->width == 0) {
            std::cout << "TODO FIX: Zero width process?" << std::endl;
            pw_stream_queue_buffer(data->stream, b);
            return;
        };


        auto spa_buffer = b->buffer;

        if (!spa_buffer || !spa_buffer->datas[0].data) {
            // Return the buffer to the stream and exit
            // Currently this fails on PID for some reason??
            std::cout << "TODO FIX: No data supplied; type: " << std::endl;
            pw_stream_queue_buffer(data->stream, b);
            return;
        }

        int just_cleared = false;


        // Allocate latest_frame if needed
        if (data->latest_frame && (data->latest_frame->width != data->width || data->latest_frame->height != data->height)) {
            std::cout << "FREEING BUFFERS" << std::endl;;

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

            std::cout << "BUFFERS FREED" << std::endl;;

        }
        if (!data->latest_frame) { // Lifetime of enc_frame, latest_frame, and encPkt are synced.
            if (setup_libav_buffers(data)) {
                pw_stream_queue_buffer(data->stream, b);
                return;
            }
        }


        auto frame_single_size = data->width*data->height;
        memcpy(data->latest_frame->data[0], (uint8_t*)spa_buffer->datas[0].data                    , frame_single_size  );
        memcpy(data->latest_frame->data[1], (uint8_t*)spa_buffer->datas[0].data + frame_single_size, frame_single_size/2);


        #if send_instantly
        send_latest_data(data);
        #endif

        pw_stream_queue_buffer(data->stream, b);
}



static const struct pw_stream_events stream_events = {
        PW_VERSION_STREAM_EVENTS,
        .param_changed = on_param_changed,
        .process = on_process,
};


void print_spa_dict(const struct spa_dict *props) {
    if (!props) {
        std::cout << "\t\t\t[spa_dict] (null)\n";
        return;
    }
    for (uint32_t i = 0; i < props->n_items; ++i) {
        std::cout << "\t\t\t" << props->items[i].key << " = " << props->items[i].value << std::endl;
    }
}

static void registry_event_global(void *data_raw,
                                  uint32_t id,
                                  uint32_t permissions,
                                  const char *type,
                                  uint32_t version,
                                  const struct spa_dict *props)
{
    stateData *data = (stateData *)data_raw;

    std::cout << "\t\tOffered: " << id << std::endl;
    print_spa_dict(props);

    const char* pid = spa_dict_lookup(props, PW_KEY_SEC_PID);
    // if (pid) std::cout << "Comparing pid: "<< pid << " to target pid: " << data->pw_target_search_pid << std::endl;
    if (pid && atoi(pid) == data->pw_target_search_pid) {
        data->pw_target_client_id = id;
        std::cout << "Updated client target ID: " << id << std::endl;
        return;
    }

    const char* client_id = spa_dict_lookup(props, PW_KEY_CLIENT_ID);
    // if (client_id) std::cout << "Comparing client_id: "<< client_id << " to target client: " << data->pw_target_client_id << std::endl;
    if (client_id && atoi(client_id) == data->pw_target_client_id) {
        std::cout << "Updated target ID: " << id << std::endl;
        const char* media_class = spa_dict_lookup(props, "media.class");
    std::cout << "Connecting to node id: " << data->pw_target_id << " with media.class: " << (media_class ?: "(none)") << std::endl;
        data->pw_target_id = id;
        return;
    }

    return;
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    // .global_remove = registry_global_remove,
};

static void core_done(void *data_raw, uint32_t id, int seq)
{
    stateData *data = (stateData *)data_raw;

    std::cout << "Core sync done; using stream ID: " << data->pw_target_id << std::endl;
    if (data->pw_target_id > 0) {
        int re = pw_stream_connect(data->stream,
            PW_DIRECTION_INPUT,
            data->pw_target_id,
            (enum pw_stream_flags) (PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
            data->pw_connect_params.pw_target_connect_helper_params, 1);
    }
}

static const pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = core_done,
};


void prepare_recording(stateData *data, int targetPid) {
        struct pw_properties *props;


        pw_init(nullptr, nullptr);

        data->loop = pw_main_loop_new(NULL);

        props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
                        NULL);

        data->stream = pw_stream_new_simple(
                        pw_main_loop_get_loop(data->loop),
                        "video-capture",
                        props,
                        &stream_events,
                        data);



        data->pw_connect_params.b = SPA_POD_BUILDER_INIT(data->pw_connect_params.buffer, sizeof(data->pw_connect_params.buffer));

        data->pw_connect_params.min_size = SPA_RECTANGLE(1, 1);
        data->pw_connect_params.max_size = SPA_RECTANGLE(4096, 4096);
        data->pw_connect_params.default_size = SPA_RECTANGLE(1920, 1080);

        data->pw_connect_params.min_framerate = SPA_FRACTION(0, 1);
        data->pw_connect_params.max_framerate = SPA_FRACTION(1000, 1);
        data->pw_connect_params.default_framerate = SPA_FRACTION(data->fps, 1);

        data->pw_connect_params.pw_target_connect_helper_params[0] =
            (const struct spa_pod*) spa_pod_builder_add_object(
                &data->pw_connect_params.b,
                SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
                SPA_FORMAT_mediaType,       SPA_POD_Id(SPA_MEDIA_TYPE_video),
                SPA_FORMAT_mediaSubtype,    SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
                SPA_FORMAT_VIDEO_format,    SPA_POD_CHOICE_ENUM_Id(2, SPA_VIDEO_FORMAT_NV12, SPA_VIDEO_FORMAT_NV12),
                SPA_FORMAT_VIDEO_size,      SPA_POD_CHOICE_RANGE_Rectangle(
                    &data->pw_connect_params.default_size,
                    &data->pw_connect_params.min_size,
                    &data->pw_connect_params.max_size
                ),
                SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
                    &data->pw_connect_params.default_framerate,
                    &data->pw_connect_params.min_framerate,
                    &data->pw_connect_params.max_framerate
                )
            );
        

        if (targetPid > 0) {
            data->pw_target_search_pid = targetPid;
            data->pw_target_client_id = -1;
            data->pw_target_id = -1;

            pw_context *context = pw_context_new(
                pw_main_loop_get_loop(data->loop),
                nullptr, 0);
            if (!context) {
                std::cerr << "Failed to create context\n";
                return;// 1;
            }

            pw_core *core = pw_context_connect(context, nullptr, 0);
            if (!core) {
                std::cerr << "Failed to connect core\n";
                return;
            }

            pw_registry *registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);


            pw_core_add_listener(core,
                                &data->core_listener,
                                &core_events,
                                data);

            pw_registry_add_listener(registry, &data->registry_listener, &registry_events, data);
            
            pw_core_sync(core, PW_ID_CORE, 0);

            std::cout << "Should be listening ??" << std::endl;
        } else {
            int re = pw_stream_connect(data->stream,
                            PW_DIRECTION_INPUT,
                            PW_ID_ANY,
                            (enum pw_stream_flags) (PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                            data->pw_connect_params.pw_target_connect_helper_params, 1);
            std::cout << "Connecting to any stream - stream id: " << re << std::endl;
        }

        #if send_instantly
        #else
        const int interval_ms = 1000 / data->fps; // 16ms for 60 FPS
        auto frame_timer = pw_loop_add_timer(pw_main_loop_get_loop(data->loop), send_frame_timer, (void*) data);
        PW_THROW_IF(!frame_timer, "Failed to create timer for frame sending");
        // Set the timer to trigger every interval_ms milliseconds
        struct timespec interval = { .tv_sec = 0, .tv_nsec = 1000000 * (1000 / data->fps) };  // 60 FPS in ns
        struct timespec value = { .tv_sec = 0, .tv_nsec = 1000000 * (1000 / data->fps) };  // 60 FPS in ns

        // Set the initial value and interval for the timer
        pw_loop_update_timer(pw_main_loop_get_loop(data->loop), frame_timer, &value, &interval, true);

        #endif
}
void start_recording(stateData *data) {

    pw_main_loop_run(data->loop);

    pw_stream_destroy(data->stream);
    pw_main_loop_destroy(data->loop);
}