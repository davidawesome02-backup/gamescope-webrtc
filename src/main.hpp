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
#include <libswscale/swscale.h>

#include <linux/uinput.h>
#include <unistd.h>
#include <fcntl.h>
}

#include "rtc/rtc.hpp"

#include <vector>
#include <filesystem>



const uint32_t SSRC = 42;
const int RTP_MTU = 1200;





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

        int uinput_kbm_fd;
        int uinput_crl_fd;
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


#include <uinput_helper.hpp>
#include <webrtc.hpp>
