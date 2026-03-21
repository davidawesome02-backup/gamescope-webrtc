

#include "recording.hpp"



#define PW_THROW_IF(cond, msg) if (cond) throw std::runtime_error(msg);

static bool setup_libav_buffers(stateData *data) {
    int ret = 0;

    ret = av_hwdevice_ctx_create(&data->hw_device_ctx, AV_HWDEVICE_TYPE_VULKAN, NULL, NULL, 0);
    PW_THROW_IF(ret < 0, "Failed to create Vulkan hwdevice context");

    data->hw_frames_ctx = av_hwframe_ctx_alloc(data->hw_device_ctx);
    PW_THROW_IF(!data->hw_frames_ctx, "Could not alloc Vulkan hwframe context");

    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)data->hw_frames_ctx->data;
    frames_ctx->format = AV_PIX_FMT_VULKAN;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = data->width;
    frames_ctx->height = data->height;
    frames_ctx->initial_pool_size = 4;

    ret = av_hwframe_ctx_init(data->hw_frames_ctx);
    PW_THROW_IF(ret < 0, "Failed to init Vulkan hwframe ctx");

    const AVCodec *encoder = avcodec_find_encoder_by_name("h264_vulkan");
    PW_THROW_IF(!encoder, "Failed to get h264_vulkan encoder");

    data->encCtx = avcodec_alloc_context3(encoder);
    data->encCtx->width = data->width;
    data->encCtx->height = data->height;
    data->encCtx->pix_fmt = AV_PIX_FMT_VULKAN;
    data->encCtx->time_base = AVRational{1, (int)data->fps};
    data->encCtx->framerate = AVRational{(int)data->fps, 1};
    data->encCtx->gop_size = data->fps;
    data->encCtx->max_b_frames = 0;
    av_opt_set(data->encCtx->priv_data, "repeat_headers", "0", 0);
    data->encCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    data->encCtx->hw_device_ctx = av_buffer_ref(data->hw_device_ctx);
    data->encCtx->hw_frames_ctx = av_buffer_ref(data->hw_frames_ctx);

    AVDictionary *codec_opts = NULL;
    av_dict_set(&codec_opts, "preset", "ultrafast", 0);
    // av_dict_set(&codec_opts, "qp", "0", 0);
    av_dict_set(&codec_opts, "qp", "18", 0);

    ret = avcodec_open2(data->encCtx, encoder, &codec_opts);
    av_dict_free(&codec_opts);
    PW_THROW_IF(ret < 0, "avcodec_open2 failed");

    if (!data->track) {
        rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
        media.addH264Codec(96);
        media.addSSRC(SSRC, "video-send");
        data->track = data->pc_connection->addTrack(media);
        data->pc_connection->setLocalDescription();
    }

    if (!data->rtpFmt) {
        ret = avformat_alloc_output_context2(&data->rtpFmt, nullptr, "rtp", nullptr);
        PW_THROW_IF(ret < 0 || !data->rtpFmt, "rtp ctx failed");

        AVStream *rtpStream = avformat_new_stream(data->rtpFmt, nullptr);
        PW_THROW_IF(!rtpStream, "stream failed");

        avcodec_parameters_from_context(rtpStream->codecpar, data->encCtx);
        rtpStream->time_base = data->encCtx->time_base;

        int avio_buf_size = RTP_MTU + 256;
        uint8_t *avioBuffer = (uint8_t *)av_malloc(avio_buf_size);

        AVIOContext *avio = avio_alloc_context(
            avioBuffer,
            avio_buf_size,
            1,
            data->track.get(),
            nullptr,
            rtp_avio_write,
            nullptr
        );

        data->rtpFmt->pb = avio;
        data->rtpFmt->packet_size = RTP_MTU;
        data->rtpFmt->flags |= AVFMT_FLAG_CUSTOM_IO;

        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "payload_type", "96", 0);
        av_dict_set(&opts, "ssrc", std::to_string(SSRC).c_str(), 0);

        ret = avformat_write_header(data->rtpFmt, &opts);
        av_dict_free(&opts);
        PW_THROW_IF(ret < 0, "write header failed");
    }

    data->latest_frame = av_frame_alloc();
    data->latest_frame->format = AV_PIX_FMT_NV12;
    data->latest_frame->width = data->width;
    data->latest_frame->height = data->height;
    ret = av_frame_get_buffer(data->latest_frame, 32);
    PW_THROW_IF(ret < 0, "latest_frame alloc failed");

    data->hw_frame = av_frame_alloc();
    data->hw_frame->format = AV_PIX_FMT_VULKAN;
    data->hw_frame->hw_frames_ctx = av_buffer_ref(data->hw_frames_ctx);
    ret = av_hwframe_get_buffer(data->hw_frames_ctx, data->hw_frame, 0);
    PW_THROW_IF(ret < 0, "hw frame alloc failed");

    data->encPkt = av_packet_alloc();
    data->pipeline_ready = true;
    data->force_keyframe = true;

    std::cout << "ALL BUFFERS ALLOCATED" << std::endl;
    return true;
}

static void send_latest_data(stateData *data) {
    if (!data->pipeline_ready) return;

    data->hw_frame->pts = data->latest_frame->pts = data->frame_pts++;

    int ret = av_hwframe_transfer_data(data->hw_frame, data->latest_frame, 0);
    PW_THROW_IF(ret < 0, "hw transfer failed");

    if (data->last_force_keyframe != data->force_keyframe) {
        if (data->force_keyframe) {
            av_opt_set(data->encCtx->priv_data, "repeat_headers", "1", 0);
        } else {
            av_opt_set(data->encCtx->priv_data, "repeat_headers", "0", 0);
        }
    }

    data->last_force_keyframe = data->force_keyframe;
    if (data->force_keyframe) {
        data->hw_frame->pict_type = AV_PICTURE_TYPE_I;
        data->force_keyframe = false;
    } else {
        data->hw_frame->pict_type = AV_PICTURE_TYPE_NONE;
    }

    avcodec_send_frame(data->encCtx, data->hw_frame);

    while (avcodec_receive_packet(data->encCtx, data->encPkt) == 0) {
        try {
            av_write_frame(data->rtpFmt, data->encPkt);
            avio_flush(data->rtpFmt->pb);
            av_packet_unref(data->encPkt);
        } catch (...) {}
    }
}

static void send_frame_timer(void *userdata, long unsigned int) {
    stateData *data = (stateData *)userdata;
    if (data->latest_frame)
        send_latest_data(data);
}

static void on_param_changed(void *userdata, uint32_t id, const struct spa_pod *param) {
    stateData *data = (stateData *)userdata;
    if (!param || id != SPA_PARAM_Format) return;

    if (spa_format_parse(param, &data->pw_req_format.media_type, &data->pw_req_format.media_subtype) < 0) return;
    if (data->pw_req_format.media_type != SPA_MEDIA_TYPE_video || data->pw_req_format.media_subtype != SPA_MEDIA_SUBTYPE_raw) return;
    if (spa_format_video_raw_parse(param, &data->pw_req_format.info.raw) < 0) return;

    data->height = data->pw_req_format.info.raw.size.height;
    data->width = (data->pw_req_format.info.raw.size.width) & ~3;
    data->real_width = (data->pw_req_format.info.raw.size.width+3) & ~3;

    std::cout << "Format changed to " << data->width << "x" << data->height << std::endl;
}

static void on_process(void *userdata) {
    stateData *data = (stateData *) userdata;
    struct pw_buffer *b = pw_stream_dequeue_buffer(data->stream);
    if (!b) return;

    auto spa_buffer = b->buffer;
    if (!spa_buffer || !spa_buffer->datas[0].data) {
        pw_stream_queue_buffer(data->stream, b);
        return;
    }

    if (!data->latest_frame || data->latest_frame->width != data->width || data->latest_frame->height != data->height) {
        std::cout << "FREEING ENCODER/FRAMES FOR RESIZE" << std::endl;


        data->pipeline_ready = false;

        // Flush encoder
        if (data->encCtx) {
            avcodec_send_frame(data->encCtx, nullptr);
            while (avcodec_receive_packet(data->encCtx, data->encPkt) == 0)
                av_packet_unref(data->encPkt);
            avcodec_free_context(&data->encCtx);
        }

        av_frame_free(&data->latest_frame);
        av_frame_free(&data->hw_frame);
        av_packet_free(&data->encPkt);

        data->latest_frame = nullptr;
        
        setup_libav_buffers(data); // rebuild only encoder + frames
        pw_stream_queue_buffer(data->stream, b);
        return;
    }


    size_t full_frame_size = data->real_width * data->height;
    uint8_t *src = (uint8_t*)spa_buffer->datas[0].data;

    // Copy Y plane
    for (int y = 0; y < data->height; ++y) {
        memcpy(
            data->latest_frame->data[0] + y * data->latest_frame->linesize[0],
            src + y * data->real_width,
            data->real_width
        );
    }

    for (int y = 0; y < data->height/2; ++y) {
        memcpy(
            data->latest_frame->data[1] + y * data->latest_frame->linesize[1],
            src + full_frame_size + y * data->real_width,
            data->real_width
        );
    }

#if send_instantly
    send_latest_data(data);
#endif

    pw_stream_queue_buffer(data->stream, b);
}


static void on_state_changed(void *userdata, pw_stream_state old, pw_stream_state state, const char *error) {
    stateData *data = (stateData *) userdata;

    if (state == PW_STREAM_STATE_ERROR) std::cout << "PW error: " << error << std::endl;

    if (
        state == PW_STREAM_STATE_PAUSED     ||
        state == PW_STREAM_STATE_ERROR      ||
        state == PW_STREAM_STATE_UNCONNECTED
    ) {
        data->pw_disconnect_time = std::time(0);
    } else {
        data->pw_disconnect_time = 0;
    }
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
    .process = on_process,
};


static void check_recording_disconnect(void *userdata, unsigned long) {
    stateData *data = (stateData *) userdata;

    if (data->pw_disconnect_time == 0) return;

    if (std::time(0) - data->pw_disconnect_time >= 4) {
        std::cout << "PW stream has been paused for >5 seconds, assuming we are dead / have nothing to capture." << std::endl;
        exit_streaming(data);
    }
}



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
                return;
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
        struct timespec send_frame_timer_interval = { .tv_sec = 0, .tv_nsec = 1000000 * interval_ms };  // 60 FPS in ns
        struct timespec send_frame_timer_value = { .tv_sec = 0, .tv_nsec = 1000000 * interval_ms };  // 60 FPS in ns

        // Set the initial value and interval for the timer
        pw_loop_update_timer(pw_main_loop_get_loop(data->loop), frame_timer, &send_frame_timer_value, &send_frame_timer_interval, true);

        #endif


        auto disconnect_timer = pw_loop_add_timer(pw_main_loop_get_loop(data->loop), check_recording_disconnect, (void*) data);
        PW_THROW_IF(!disconnect_timer, "Failed to create timer for disconnect checks");
        struct timespec check_recording_disconnect_interval = { .tv_sec = 1, .tv_nsec = 0 };  // 60 FPS in ns
        struct timespec check_recording_disconnect_value = { .tv_sec = 1, .tv_nsec = 0 };  // 60 FPS in ns
        pw_loop_update_timer(pw_main_loop_get_loop(data->loop), disconnect_timer, &check_recording_disconnect_value, &check_recording_disconnect_interval, true);

        data->pw_disconnect_time = std::time(0);
}
void start_recording(stateData *data) {

    pw_main_loop_run(data->loop);

    pw_stream_destroy(data->stream);
    pw_main_loop_destroy(data->loop);
}




