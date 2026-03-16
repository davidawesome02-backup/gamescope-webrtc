#pragma once

#define GS_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* crl_path;
    const char* kbm_path;

    const char* ICE_offer;
    const char* join_code;
    bool webrtc_connection_failed;

    int result_err; // If not 0, error

    void* opaque_internal_ctx;
} gamescopeWebrtcCtx;



GS_EXPORT gamescopeWebrtcCtx* gamescopeWebrtc_INIT(bool, bool);

GS_EXPORT void gamescopeWebrtc_create_webrtc(gamescopeWebrtcCtx*, int, bool, char*);
GS_EXPORT void gamescopeWebrtc_check_webrtc(gamescopeWebrtcCtx*);
GS_EXPORT void gamescopeWebrtc_start_recording(gamescopeWebrtcCtx*, int);

#ifdef __cplusplus
}
#endif
