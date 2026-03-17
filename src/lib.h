#pragma once

#define GS_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* ctrl_path;
    const char* kbm_path;

    const char* ICE_offer;
    const char* join_code;
    bool webrtc_connection_failed;

    int result_err; // If not 0, error

    void* opaque_internal_ctx;
} gamescope_webrtc_ctx;



GS_EXPORT gamescope_webrtc_ctx* gamescope_webrtc_init(bool, bool);

GS_EXPORT void gamescope_webrtc_create_webrtc(gamescope_webrtc_ctx*, int, bool, char*);
GS_EXPORT void gamescope_webrtc_check_webrtc(gamescope_webrtc_ctx*);
GS_EXPORT void gamescope_webrtc_start_recording(gamescope_webrtc_ctx*, int);

#ifdef __cplusplus
}
#endif
