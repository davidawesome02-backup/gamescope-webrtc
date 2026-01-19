#include <webrtc.hpp>


using nlohmann::json;


int rtp_avio_write(void *opaque, const uint8_t *buf, int buf_size) {
    try {
        auto *track = reinterpret_cast<rtc::Track *>(opaque);
        // printf("Would have writen %d\n", buf_size);
        if (track->isOpen())
            track->send(reinterpret_cast<const std::byte *>(buf), buf_size);
    }
    catch (const std::runtime_error& e) {
        // Catch the specific exception type
        std::cerr << "Caught a runtime error: " << e.what() << std::endl;
    }

    return buf_size;
    
}

static void recive_data_message(stateData *data, rtc::message_variant recived) {
    if (!std::holds_alternative<rtc::binary>(recived)) return;
    auto bin_data = std::get<rtc::binary>(recived);
    
    if (!(data->uinput_kbm_fd >= 0)) return;

    if (bin_data.size() < 1) return;
    auto type_selected = read_le_from_vec<int8_t>(bin_data, 0);
    if (type_selected != 0) return;

    process_remote_message(data, bin_data);
}

void setup_RTC(stateData *data) {
    // ---- WebRTC setup ----
    rtc::InitLogger(rtc::LogLevel::Debug);

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun2.l.google.com:19302");


    auto pc = std::make_shared<rtc::PeerConnection>(config);
    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "State: " << state << std::endl; // Todo if closing, reoffer connection stuff
    });
    pc->onGatheringStateChange([pc](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            json msg = {
                {"type", description->typeString()},
                {"sdp", std::string(description.value())}
            };
            std::cout << msg << std::endl;
        }
    });

    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(SSRC, "video-send");
    data->track = pc->addTrack(media);

    data->datatrack = pc->createDataChannel("video-data", {
        protocol: "hi",
    });

    data->datatrack.get()->onMessage([data](rtc::message_variant a){
        recive_data_message(data,a);
    });

    // data->datatrack.get
    
    pc->setLocalDescription();

    std::cout << "Paste browser answer (JSON):" << std::endl;
    std::string sdp;
    std::getline(std::cin, sdp);
    json j = json::parse(sdp);
    rtc::Description answer(j["sdp"].get<std::string>(), j["type"].get<std::string>());
    pc->setRemoteDescription(answer);

}




