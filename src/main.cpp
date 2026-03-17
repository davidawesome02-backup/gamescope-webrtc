#include "main.hpp"

#define send_instantly false

int main(int argc, char *argv[]) {
    stateData data = { 0, };
    data.fps = 60;

    setup_RTC(&data, true, "wss://webrtc-streaming-pages.pages.dev/websocket");
    std::string fake_input_path_kbm = setup_uinput_keyboard_mouse(&data);
    std::string fake_input_path_ctrl = setup_uinput_controller(&data);

    prepare_recording(&data, 3566);
    start_recording(&data);

    return 0;
}
