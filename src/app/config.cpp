#include "peerjam/app/config.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace peerjam {
namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool parse_bool(const std::string& value) {
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

int parse_int(const std::unordered_map<std::string, std::string>& values, const std::string& key, int current) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return current;
    }
    try {
        return std::stoi(it->second);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for config key " + key + ": " + it->second);
    }
}

double parse_double(const std::unordered_map<std::string, std::string>& values, const std::string& key, double current) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return current;
    }
    try {
        return std::stod(it->second);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid number for config key " + key + ": " + it->second);
    }
}

std::string parse_string(const std::unordered_map<std::string, std::string>& values, const std::string& key, const std::string& current) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return current;
    }
    return it->second;
}

std::unordered_map<std::string, std::string> parse_config_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const auto pos = trimmed.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        values.emplace(trim(trimmed.substr(0, pos)), trim(trimmed.substr(pos + 1)));
    }
    return values;
}

void apply_logging_config(const std::unordered_map<std::string, std::string>& values, LoggingOptions& logging) {
    logging.enabled = values.count("logging.enabled") ? parse_bool(values.at("logging.enabled")) : logging.enabled;
    logging.level = parse_string(values, "logging.level", logging.level);
    logging.sink = parse_string(values, "logging.sink", logging.sink);
    logging.file_path = parse_string(values, "logging.file_path", logging.file_path);
}

void apply_recommendation_config(const std::unordered_map<std::string, std::string>& values, RecommendationPolicyOptions& recommendation) {
    recommendation.sample_rate_hz = parse_int(values, "recommendation.sample_rate_hz", recommendation.sample_rate_hz);
    recommendation.aggressive_frame_samples = parse_int(values, "recommendation.aggressive.frame_samples", recommendation.aggressive_frame_samples);
    recommendation.conservative_frame_samples = parse_int(values, "recommendation.conservative.frame_samples", recommendation.conservative_frame_samples);
    recommendation.fallback_frame_samples = parse_int(values, "recommendation.fallback.frame_samples", recommendation.fallback_frame_samples);
    recommendation.not_ready_loss_percent = parse_double(values, "recommendation.not_ready.loss_percent", recommendation.not_ready_loss_percent);
    recommendation.not_ready_one_way_p95_ms = parse_double(values, "recommendation.not_ready.one_way_p95_ms", recommendation.not_ready_one_way_p95_ms);
    recommendation.not_ready_jitter_p95_ms = parse_double(values, "recommendation.not_ready.jitter_p95_ms", recommendation.not_ready_jitter_p95_ms);
    recommendation.aggressive_ready_loss_percent = parse_double(values, "recommendation.aggressive_ready.loss_percent", recommendation.aggressive_ready_loss_percent);
    recommendation.aggressive_ready_one_way_p95_ms = parse_double(values, "recommendation.aggressive_ready.one_way_p95_ms", recommendation.aggressive_ready_one_way_p95_ms);
    recommendation.aggressive_ready_jitter_p95_ms = parse_double(values, "recommendation.aggressive_ready.jitter_p95_ms", recommendation.aggressive_ready_jitter_p95_ms);
    recommendation.conservative_ready_loss_percent = parse_double(values, "recommendation.conservative_ready.loss_percent", recommendation.conservative_ready_loss_percent);
    recommendation.conservative_ready_one_way_p95_ms = parse_double(values, "recommendation.conservative_ready.one_way_p95_ms", recommendation.conservative_ready_one_way_p95_ms);
    recommendation.conservative_ready_jitter_p95_ms = parse_double(values, "recommendation.conservative_ready.jitter_p95_ms", recommendation.conservative_ready_jitter_p95_ms);
    recommendation.not_ready_buffer_min_ms = parse_double(values, "recommendation.not_ready.buffer_min_ms", recommendation.not_ready_buffer_min_ms);
    recommendation.not_ready_buffer_max_ms = parse_double(values, "recommendation.not_ready.buffer_max_ms", recommendation.not_ready_buffer_max_ms);
    recommendation.ready_buffer_min_ms = parse_double(values, "recommendation.ready.buffer_min_ms", recommendation.ready_buffer_min_ms);
    recommendation.ready_buffer_max_ms = parse_double(values, "recommendation.ready.buffer_max_ms", recommendation.ready_buffer_max_ms);
    recommendation.marginal_buffer_min_ms = parse_double(values, "recommendation.marginal.buffer_min_ms", recommendation.marginal_buffer_min_ms);
    recommendation.marginal_buffer_max_ms = parse_double(values, "recommendation.marginal.buffer_max_ms", recommendation.marginal_buffer_max_ms);
}

void apply_audio_config(const std::unordered_map<std::string, std::string>& values, AudioOptions& audio) {
    audio.seconds = parse_int(values, "audio.seconds", audio.seconds);
    audio.sample_rate_hz = parse_int(values, "audio.sample_rate_hz", audio.sample_rate_hz);
    audio.frame_samples = parse_int(values, "audio.frame_samples", audio.frame_samples);
    audio.channels = parse_int(values, "audio.channels", audio.channels);
    audio.source_buffer_ms = parse_int(values, "audio.source_buffer_ms", audio.source_buffer_ms);
    audio.sink_buffer_ms = parse_int(values, "audio.sink_buffer_ms", audio.sink_buffer_ms);
    audio.packetsize = parse_int(values, "audio.packetsize", audio.packetsize);
    audio.resend_buffer_ms = parse_int(values, "audio.resend_buffer_ms", audio.resend_buffer_ms);
    audio.resend_limit = parse_int(values, "audio.resend_limit", audio.resend_limit);
    audio.resend_interval_ms = parse_int(values, "audio.resend_interval_ms", audio.resend_interval_ms);
    audio.resend_max_frames = parse_int(values, "audio.resend_max_frames", audio.resend_max_frames);
    audio.redundancy = parse_int(values, "audio.redundancy", audio.redundancy);
    audio.dynamic_resampling = values.count("audio.dynamic_resampling") ? parse_bool(values.at("audio.dynamic_resampling")) : audio.dynamic_resampling;
    audio.codec = parse_string(values, "audio.codec", audio.codec);
    audio.device_io_enabled = values.count("audio.device_io_enabled") ? parse_bool(values.at("audio.device_io_enabled")) : audio.device_io_enabled;
    audio.input_device_index = parse_int(values, "audio.input_device_index", audio.input_device_index);
    audio.output_device_index = parse_int(values, "audio.output_device_index", audio.output_device_index);
    audio.tone_hz = parse_double(values, "audio.tone_hz", audio.tone_hz);
    audio.tone_level = parse_double(values, "audio.tone_level", audio.tone_level);
    audio.source_id = parse_int(values, "audio.source_id", audio.source_id);
    audio.sink_id = parse_int(values, "audio.sink_id", audio.sink_id);
}

}  // namespace

bool load_peer_config(const std::string& path, PeerOptions& options) {
    if (path.empty()) {
        return false;
    }
    const auto values = parse_config_file(path);
    if (values.empty()) {
        return false;
    }
    options.server_host = parse_string(values, "peer.server_host", options.server_host);
    options.server_port = parse_int(values, "peer.server_port", options.server_port);
    options.candidate_exchange_port = parse_int(values, "peer.candidate_exchange_port", options.candidate_exchange_port);
    options.group = parse_string(values, "peer.group", options.group);
    options.group_password = parse_string(values, "peer.group_password", options.group_password);
    options.user = parse_string(values, "peer.user", options.user);
    options.user_password = parse_string(values, "peer.user_password", options.user_password);
    options.stun_host = parse_string(values, "peer.stun_host", options.stun_host);
    options.stun_port = parse_int(values, "peer.stun_port", options.stun_port);
    options.udp_port = parse_int(values, "peer.udp_port", options.udp_port);
    options.duration_seconds = parse_int(values, "probe.duration_seconds", options.duration_seconds);
    options.interval_ms = parse_double(values, "probe.interval_ms", options.interval_ms);
    options.payload_bytes = parse_int(values, "probe.payload_bytes", options.payload_bytes);
    options.warmup_packets = parse_int(values, "probe.warmup_packets", options.warmup_packets);
    options.path_reprobe_interval_ms = parse_int(values, "path.reprobe_interval_ms", options.path_reprobe_interval_ms);
    options.path_switch_margin_score = parse_double(values, "path.switch_margin_score", options.path_switch_margin_score);
    options.path_switch_consecutive_wins = parse_int(values, "path.switch_consecutive_wins", options.path_switch_consecutive_wins);
    options.ui_telemetry_port = parse_int(values, "ui.telemetry_port", options.ui_telemetry_port);
    options.ui_telemetry_interval_ms = parse_int(values, "ui.telemetry_interval_ms", options.ui_telemetry_interval_ms);
    options.session_profile_in = parse_string(values, "profile.in", options.session_profile_in);
    options.session_profile_out = parse_string(values, "profile.out", options.session_profile_out);
    apply_logging_config(values, options.logging);
    apply_recommendation_config(values, options.recommendation);
    apply_audio_config(values, options.audio);
    return true;
}

bool load_server_config(const std::string& path, ServerOptions& options) {
    if (path.empty()) {
        return false;
    }
    const auto values = parse_config_file(path);
    if (values.empty()) {
        return false;
    }
    options.port = parse_int(values, "server.port", options.port);
    options.candidate_exchange_port = parse_int(values, "server.candidate_exchange_port", options.candidate_exchange_port);
    apply_logging_config(values, options.logging);
    return true;
}

const char* default_config_path(AppKind kind) {
    switch (kind) {
        case AppKind::SignalServer:
            return "resources/config/signal-server.conf";
        case AppKind::NetTest:
            return "resources/config/net-test.conf";
        case AppKind::PeerProbe:
            return "resources/config/peer-probe.conf";
    }
    return "";
}

}  // namespace peerjam
