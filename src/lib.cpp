#include "lib.h"
#include "main.hpp"

static gamescope_webrtc_ctx* int_gamescope_webrtc_init(bool kbm, bool ctrl) {
    gamescope_webrtc_ctx* allocated_ctx = (gamescope_webrtc_ctx*) calloc(1, sizeof(gamescope_webrtc_ctx));
    stateData* internal_ctx = new stateData();
    allocated_ctx->opaque_internal_ctx = (void*) internal_ctx;

    allocated_ctx->result_err = 0;

    internal_ctx->uinput_kbm_dev_path = setup_uinput_keyboard_mouse(internal_ctx);
    allocated_ctx->kbm_path = internal_ctx->uinput_kbm_dev_path.c_str();
    std::cout << "Returned kbm path: " << allocated_ctx->kbm_path << std::endl;
    // printf("Returned kbm path: %s\n", allocated_ctx->kbm_path);

    internal_ctx->uinput_ctrl_dev_path = setup_uinput_controller(internal_ctx);
    allocated_ctx->ctrl_path = internal_ctx->uinput_ctrl_dev_path.c_str();
    std::cout << "Returned ctrl path: " << allocated_ctx->ctrl_path << std::endl;
    // printf("Returned ctrl path: %s\n", allocated_ctx->ctrl_path);

    return allocated_ctx;
}

static void int_gamescope_webrtc_create_webrtc(gamescope_webrtc_ctx* allocated_ctx, int fps, bool request_code, char* URL) {
    stateData* internal_ctx = (stateData*)allocated_ctx->opaque_internal_ctx;
    internal_ctx->fps = fps;

    std::string URL_str = URL;

    setup_RTC(internal_ctx, request_code, URL_str);
}

static void int_gamescope_webrtc_check_webrtc(gamescope_webrtc_ctx* allocated_ctx) {
    stateData* internal_ctx = (stateData*)allocated_ctx->opaque_internal_ctx;

    auto rtc_connection_obj = internal_ctx->pc_connection.get();
    if (rtc_connection_obj) {
        auto rtc_connection_state = rtc_connection_obj->state();
        switch (rtc_connection_state) {
            case rtc::PeerConnection::State::New:
            case rtc::PeerConnection::State::Connecting:
            case rtc::PeerConnection::State::Connected:
                break;
            default:
                allocated_ctx->webrtc_connection_failed = true;
                std::cerr << "Web rtc died" << std::endl;
                return;
        }
    }

    auto websock_connection_obj = internal_ctx->connection_open_socket.get();
    if (websock_connection_obj) {
        if (websock_connection_obj->isClosed() && internal_ctx->connection_code && internal_ctx->connection_code->empty()) {
            allocated_ctx->webrtc_connection_failed = true;
            std::cerr << "Websocket code connection died" << std::endl;
        }
    }

    
    if (internal_ctx->ICE_offer_str && !internal_ctx->ICE_offer_str->empty()) allocated_ctx->ICE_offer = internal_ctx->ICE_offer_str->c_str();
    if (internal_ctx->connection_code && !internal_ctx->connection_code->empty()) allocated_ctx->join_code = internal_ctx->connection_code->c_str();
}

static void int_gamescope_webrtc_start_recording(gamescope_webrtc_ctx* allocated_ctx, int gamescope_pid) {
    stateData* internal_ctx = (stateData*)allocated_ctx->opaque_internal_ctx;
    std::cout << "Starting stream from library." << std::endl; 


    prepare_recording(internal_ctx, gamescope_pid);
    start_recording(internal_ctx);
}



extern "C" gamescope_webrtc_ctx* gamescope_webrtc_init(bool kbm, bool ctrl) {
    return int_gamescope_webrtc_init(kbm, ctrl);
}

extern "C" 
void    gamescope_webrtc_create_webrtc(gamescope_webrtc_ctx* allocated_ctx, int fps, bool request_code, char* URL) {
    int_gamescope_webrtc_create_webrtc(allocated_ctx, fps, request_code, URL);
}

extern "C"
void    gamescope_webrtc_check_webrtc(gamescope_webrtc_ctx* allocated_ctx) {
    int_gamescope_webrtc_check_webrtc(allocated_ctx);
}

extern "C" 
void    gamescope_webrtc_start_recording(gamescope_webrtc_ctx* allocated_ctx, int gamescope_pid) {
    int_gamescope_webrtc_start_recording(allocated_ctx, gamescope_pid);
}
