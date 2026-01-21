#include <webrtc.hpp>


using nlohmann::json;


int rtp_avio_write(void *opaque, const uint8_t *buf, int buf_size) {
    try {
        auto *track = reinterpret_cast<rtc::Track *>(opaque);
        // printf("Would have writen %d\n", buf_size);
        if (track && track->isOpen())
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

// std::string b32enc(const std::string& in) {
//     static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
//     std::string out;
//     uint32_t buf = 0;
//     int bits = 0;

//     for (unsigned char c : in) {
//         buf = (buf << 8) | c;
//         bits += 8;
//         while (bits >= 5) {
//             out.push_back(A[(buf >> (bits - 5)) & 31]);
//             bits -= 5;
//         }
//     }
//     if (bits)
//         out.push_back(A[(buf << (5 - bits)) & 31]);
//     return out;
// }

#include <string>
#include <cstdint>

std::string b32enc(const std::string& in) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    std::string out;
    uint32_t buf = 0;
    int bits = 0;

    for (unsigned char c : in) {
        buf = (buf << 8) | c;
        bits += 8;

        while (bits >= 5) {
            out.push_back(A[(buf >> (bits - 5)) & 31]);
            bits -= 5;
            buf &= (1u << bits) - 1;  // 🔑 discard consumed bits
        }
    }

    if (bits > 0) {
        out.push_back(A[(buf << (5 - bits)) & 31]);
    }

    return out;
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
    pc->onGatheringStateChange([data, pc](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            json msg = {
                {"type", description->typeString()},
                {"sdp", std::string(description.value())}
            };
            // std::cout << "hi me asdasd asd " << std::endl;
            // std::cout << msg << std::endl;
            std::string msg_as_str = msg.dump();
            // std::cout << msg_as_str << std::endl;
            std::string msg_as_b32 = b32enc(msg_as_str);
            // std::cout << msg_as_b32 << std::endl;
            // std::cout << "hi me asdasd asd " << std::endl;

            std::string url_base = "wss://webrtc-streaming-pages.pages.dev/websocket";//"localhost:7777/websocket";
            std::string url = url_base+"?offer="+msg_as_b32;
            
            // int rtc_ws_precon = rtcCreateWebSocket(url.c_str());
            // auto test = rtc::WebSocket(url);
            data->connection_open_socket = std::make_shared<rtc::WebSocket>();
            
            // ws.get


            data->connection_open_socket.get()->onMessage([pc](rtc::message_variant a){
                if (!std::holds_alternative<std::string>(a)) return;

                json container_json = json::parse(std::get<std::string>(a));
                std::cout << "REC msg from server: "+container_json["type"].get<std::string>() << std::endl;
                if (container_json["type"].get<std::string>() == "code") {
                    std::cout << "Connection code: "+container_json["code"].get<std::string>() << std::endl;
                }
                if (container_json["type"].get<std::string>() != "accept") return;

                json j = json::parse(container_json["accept"].get<std::string>());
                rtc::Description answer(j["sdp"].get<std::string>(), j["type"].get<std::string>());
                pc->setRemoteDescription(answer);
            });

            data->connection_open_socket.get()->onOpen([pc](void){
                std::cout << "Opened ws!" << std::endl;
            });
            data->connection_open_socket.get()->onClosed([pc](void){
                std::cout << "Closed ws!" << std::endl;
            });
            data->connection_open_socket.get()->onError([pc](std::string error){
                std::cout << "Error ws!: "+error << std::endl;
            });

            data->connection_open_socket.get()->open(url);


            // ws.get().
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

    // std::cout << "Paste browser answer (JSON):" << std::endl;
    
    
    // std::string sdp;
    // std::getline(std::cin, sdp);
    
    
    // json j = json::parse(sdp);
    // rtc::Description answer(j["sdp"].get<std::string>(), j["type"].get<std::string>());
    // pc->setRemoteDescription(answer);

}




