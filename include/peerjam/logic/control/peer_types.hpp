#pragma once

#include "peerjam/logic/audio/audio_stream.hpp"
#include "peerjam/logic/probe/recommendation_policy_options.hpp"

#include <string>
#include <sys/socket.h>

namespace peerjam {

struct LoggingOptions {
    bool enabled {};
    std::string level;
    std::string sink;
    std::string file_path;
};

struct PeerOptions {
    std::string server_host;
    int server_port {};
    int candidate_exchange_port {};
    std::string group;
    std::string group_password;
    std::string user;
    std::string user_password;
    std::string stun_host;
    int stun_port {};
    int udp_port {};
    int duration_seconds {};
    double interval_ms {};
    int payload_bytes {};
    int warmup_packets {};
    int path_reprobe_interval_ms {};
    double path_switch_margin_score {};
    int path_switch_consecutive_wins {};
    int ui_telemetry_port {};
    int ui_telemetry_interval_ms {100};
    std::string session_profile_in;
    std::string session_profile_out;
    bool audio_codec_locked {false};
    LoggingOptions logging;
    RecommendationPolicyOptions recommendation;
    AudioOptions audio;
};

struct ServerOptions {
    int port {};
    int candidate_exchange_port {};
    LoggingOptions logging;
};

struct RemoteEndpoint {
    sockaddr_storage addr {};
    socklen_t addr_len {};
    std::string user;
};

}  // namespace peerjam
