#include <lib.h>
#include <string>


int main(int argc, char *argv[]) {
    gamescope_webrtc_ctx* ctx = gamescope_webrtc_init(false, false);

    std::string api_endpoint = "wss://webrtc-streaming-pages.pages.dev/websocket";
    gamescope_webrtc_create_webrtc(ctx, 60, true, (char*) api_endpoint.c_str());
    
    gamescope_webrtc_start_recording(ctx, -1); // Or PID specified.

    return 0;
}
