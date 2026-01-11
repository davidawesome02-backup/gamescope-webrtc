#include <main.hpp>


// Helper (for error handling)




#define send_instantly false
// #define send_instantly true

// RTP AVIO write callback: called by FFmpeg for each RTP packet





int main(int argc, char *argv[]) {
        stateData data = { 0, };
        data.fps = 60;


        setup_RTC(&data);


        setup_uinput(&data);

        prepare_recording(&data, argc, argv);
        start_recording(&data);

// pw_main_loop_run


        return 0;
}
