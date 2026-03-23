#include "webrtc.hpp"


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

void recreate_webrtc_track(stateData *data) {
    std::cout << "Recreating WebRTC track due to resize..." << std::endl;

    if (data->track) {
        try {
            data->track->close(); // stop old stream
        } catch (...) {}
    }

    // Create new media description
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);

    // You can reuse SSRC or generate a new one
    media.addSSRC(rand(), "video-send");

    data->track = data->pc_connection->addTrack(media);

    data->pc_connection->setLocalDescription();

    std::cout << "New track created" << std::endl;
}

static void recive_data_message(stateData *data, rtc::message_variant recived) {
    if (!std::holds_alternative<rtc::binary>(recived)) return;
    auto bin_data = std::get<rtc::binary>(recived);
    
    if (!(data->uinput_kbm_fd >= 0)) return;
    if (!(data->uinput_ctrl_fd >= 0)) return;

    if (bin_data.size() < 1) return;
    auto type_selected = read_le_from_vec<int8_t>(bin_data, 0);
    if (type_selected != 0) return;

    process_remote_message(data, bin_data);
}



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
            buf &= (1u << bits) - 1;
        }
    }

    if (bits > 0) {
        out.push_back(A[(buf << (5 - bits)) & 31]);
    }

    return out;
}


void setup_RTC(stateData *data, bool create_code, std::string url_base) {
    // ---- WebRTC setup ----
    rtc::InitLogger(rtc::LogLevel::Warning);

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun2.l.google.com:19302");


    auto pc = std::make_shared<rtc::PeerConnection>(config);
    data->pc_connection = pc;
    
    pc->onStateChange([data](rtc::PeerConnection::State state) {
        std::cout << "State: " << state << std::endl; // Todo if closing, reoffer connection stuff
        if (state == rtc::PeerConnection::State::Closed || state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Disconnected) {exit_streaming(data);}
    });
    pc->onGatheringStateChange([data, pc, url_base, create_code](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            json msg = {
                {"type", description->typeString()},
                {"sdp", std::string(description.value())}
            };

            std::string msg_as_str = msg.dump();
            data->ICE_offer_str = std::make_shared<std::string>(msg_as_str);

            std::string msg_as_b32 = b32enc(msg_as_str);

            if (create_code) {
                data->ws_should_close = false;

                std::string url = url_base+"?offer="+msg_as_b32;
                
                data->connection_open_socket = std::make_shared<rtc::WebSocket>();
                

                data->connection_open_socket.get()->onMessage([pc, data](rtc::message_variant a){
                    if (!std::holds_alternative<std::string>(a)) return;

                    json container_json = json::parse(std::get<std::string>(a));
                    std::cout << "REC msg from server: "+container_json["type"].get<std::string>() << std::endl;
                    if (container_json["type"].get<std::string>() == "code") {
                        std::cout << "Connection code: "+container_json["code"].get<std::string>() << std::endl;

                        data->connection_code = std::make_shared<std::string>(container_json["code"].get<std::string>());
                    }
                    if (container_json["type"].get<std::string>() != "accept") return;

                    json j = json::parse(container_json["accept"].get<std::string>());
                    rtc::Description answer(j["sdp"].get<std::string>(), j["type"].get<std::string>());
                    pc->setRemoteDescription(answer);

                    data->ws_should_close = true;
                });

                data->connection_open_socket.get()->onOpen([pc](void){
                    std::cout << "Opened ws!" << std::endl;
                });
                data->connection_open_socket.get()->onClosed([pc, data](void){
                    std::cout << "Closed ws!" << std::endl;
                    if (!data->ws_should_close) exit_streaming(data);
                });
                data->connection_open_socket.get()->onError([pc, data](std::string error){
                    std::cout << "Error ws!: "+error << std::endl;
                    exit_streaming(data);
                });

                data->connection_open_socket.get()->open(url);
            }

        }
    });

    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(SSRC, "video-send");
    data->track = pc->addTrack(media);

    data->datatrack = pc->createDataChannel("video-data", {
        protocol: "Remote input v0",
    });

    data->datatrack.get()->onMessage([data](rtc::message_variant a){
        recive_data_message(data,a);
    });

    pc->setLocalDescription();
}




