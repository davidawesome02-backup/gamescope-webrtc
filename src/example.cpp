#include <lib.h>
#include <string>

#include <unistd.h>

#include <stdio.h>
#include <cstring>
#include <iostream>

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf(
            "Pass nothing to use a random PID that offers it\n"
            "Pass --auto to spawn gamescope with all args specifed after\n"
            "I suggest testing with gamescope -- ./src/gamescope-webrtc --auto gamescope -W 1920 -w 1920 -H 1080 -h 1080 --force-windows-fullscreen konsole\n"
        );
        exit(0);
    }

    gamescope_webrtc_ctx* ctx = gamescope_webrtc_init(false, false);

    std::string api_endpoint = "wss://webrtc-streaming-pages.pages.dev/websocket";
    gamescope_webrtc_create_webrtc(ctx, 60, true, (char*) api_endpoint.c_str());

    
    int pid_target = -1;
    if (argc > 1) {
        pid_target = fork();
        if (strcmp(argv[1], "--auto") == 0) {
            if (pid_target == 0) {
                char** argv_new = (char**) calloc(sizeof(void*), argc+6);
                for (int i=0; i<argc; i++) argv_new[i] = argv[i];

                argv_new[argc  ] = (char*) "--grab";
                argv_new[argc+1] = (char*) "--libinput-hold-dev";
                argv_new[argc+2] = (char*) ctx->kbm_path;
                // argv_new[argc+3] = (char*) "--backend-disable-mouse";
                // argv_new[argc+4] = (char*) "--backend-disable-keyboard";

                
                printf("Spawning process with params:");
                for (int i=2; i<argc+6; i++) {
                    printf("| %s\n", argv_new[i]);
                }


                execvp(argv_new[2],&argv_new[2]);
                exit(0);
            }
            printf("Started proces PID - %lld   process name (--auto): %s\n", pid_target, argv[2]);
        } else {
            if (pid_target == 0) {
                execvp(argv[1],&argv[1]);
                exit(0);
            }
            printf("Started proces PID - %lld   process name: %s\n", pid_target, argv[1]);
        }
    }

    gamescope_webrtc_start_recording(ctx, pid_target); // PID or -1 if selecing first in pw list.

    return 0;
}
