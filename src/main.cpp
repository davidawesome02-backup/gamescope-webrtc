#include <main.hpp>


// Helper (for error handling)




#define send_instantly false
// #define send_instantly true

// RTP AVIO write callback: called by FFmpeg for each RTP packet





int main(int argc, char *argv[]) {
        stateData data = { 0, };
        data.fps = 60;


        setup_RTC(&data);
        std::string fake_input_path = setup_uinput_keyboard_mouse(&data);
        data.input_bind_paths.push_back(fake_input_path);

        // spawn_container_and_game(&data);

        prepare_recording(&data, argc, argv);
        start_recording(&data);

// pw_main_loop_run


        return 0;
}
