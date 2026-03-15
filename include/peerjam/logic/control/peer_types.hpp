#pragma once

#include "peerjam/logic/audio/audio_stream.hpp"

#include <string>
#include <sys/socket.h>

namespace peerjam {

struct PeerOptions {
    std::string server_host;
    int server_port {10999};
    std::string group;
    std::string group_password;
    std::string user;
    std::string user_password;
    std::string stun_host;
    int stun_port {3478};
    int udp_port {12000};
    int duration_seconds {10};
    double interval_ms {5.0};
    int payload_bytes {128};
    int warmup_packets {20};
    int path_reprobe_interval_ms {3000};
    double path_switch_margin_score {1.0};
    int path_switch_consecutive_wins {2};
    AudioOptions audio;
};

struct RemoteEndpoint {
    sockaddr_storage addr {};
    socklen_t addr_len {};
    std::string user;
};

}  // namespace peerjam
