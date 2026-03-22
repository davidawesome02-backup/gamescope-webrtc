#pragma once

#include <spa/param/video/format-utils.h>
#include <spa/debug/types.h>
#include <spa/param/video/type-info.h>

#include <chrono>
#include <thread>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <cstring>
#include <atomic>
#include <csignal>

#include <pipewire/pipewire.h>


// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>

#include <linux/uinput.h>
#include <unistd.h>
#include <fcntl.h>
}

#include "rtc/rtc.hpp"

#include <vector>
#include <filesystem>

#include <vulkan/vulkan.h>

const uint32_t SSRC = 42;
const int RTP_MTU = 1200;

#define send_instantly false


typedef struct {
    uint8_t buffer[1024];
    struct spa_pod_builder b;

    struct spa_rectangle min_size;
    struct spa_rectangle max_size;
    struct spa_rectangle default_size;

    struct spa_fraction min_framerate;
    struct spa_fraction max_framerate;
    struct spa_fraction default_framerate;

    const struct spa_pod *pw_target_connect_helper_params[1];
} pw_connect_params_joined_obj;

typedef struct {
    // General
    uint32_t fps = 60;

    // Uinput virtualization
    int uinput_kbm_fd;
    std::string uinput_kbm_dev_path;
    int uinput_ctrl_fd;
    std::string uinput_ctrl_dev_path;


    // Opening websocket and webrtc connection
    std::shared_ptr<rtc::PeerConnection> pc_connection;
    std::shared_ptr<rtc::Track> track;
    std::shared_ptr<rtc::DataChannel> datatrack;
    bool ws_should_close;

    std::shared_ptr<rtc::WebSocket> connection_open_socket; // Websocket to backend to get a temp join code
    std::shared_ptr<std::string> connection_code; // Short ~6 character code established with the websocket.
    std::shared_ptr<std::string> ICE_offer_str; // Full offer code


    // AV encoding frames and data
    AVFrame *latest_frame = nullptr;
    AVFrame *hw_frame = nullptr;

    AVPacket *encPkt = nullptr;
    int64_t frame_pts = 0;
    int real_width = 0; // used for image copying from pipewire (has extra blank at most 3 pixels)
    int width = 0, height = 0; // Av frame sizes. (cut off at most 3 pixels)
    
    AVFormatContext *rtpFmt;
    AVCodecContext *encCtx;

    AVBufferRef *device_ctx;
    AVBufferRef *hw_device_ctx;
    AVBufferRef *hw_frames_ctx;

    bool pipeline_ready; // Used to say if we should send frames after setup.
    bool force_keyframe;
    bool last_force_keyframe; // Needed to turn back off the resend headers flag
    

    // Pipewire context information
    struct pw_main_loop *loop;
    struct pw_stream *stream;
    struct spa_video_info pw_req_format;

    pw_connect_params_joined_obj pw_connect_params;
    spa_hook registry_listener;
    spa_hook core_listener;
    int pw_target_search_pid;
    int pw_target_client_id;
    int pw_target_id;


    std::time_t pw_disconnect_time; // If 0, we are active, otherwise is a timestamp of when it stated a disconnect. If has been 5 sec disconnects.
} stateData;


template <class T>
static T read_le_from_vec(const std::vector<std::byte>& buf, std::size_t offset) {
    static_assert(std::is_integral_v<T>, "T must be an integer type");

    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(std::to_integer<uint8_t>(buf[offset + i])) << (8 * i);
    }
    return value;
}

void exit_streaming(stateData* data);

#include <uinput_helper.hpp>
#include <webrtc.hpp>
