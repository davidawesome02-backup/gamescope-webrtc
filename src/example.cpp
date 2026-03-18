#include <lib.h>



int main(int argc, char *argv[]) {
    gamescope_webrtc_ctx* ctx = gamescope_webrtc_init(false, false);

    gamescope_webrtc_create_webrtc(ctx, 60, true, "wss://webrtc-streaming-pages.pages.dev/websocket");
    
    gamescope_webrtc_start_recording(ctx, 633074);

    return 0;
}
