#include "peerjam/app/cli_options.hpp"

#include <iostream>
#include <stdexcept>

namespace peerjam {
namespace {

bool read_string_arg(char** begin, char** end, const std::string& flag, std::string& target) {
    for (char** it = begin; it != end; ++it) {
        if (flag == *it && (it + 1) != end) {
            target = *(it + 1);
            return true;
        }
    }
    return false;
}

bool read_int_arg(char** begin, char** end, const std::string& flag, int& target) {
    for (char** it = begin; it != end; ++it) {
        if (flag == *it && (it + 1) != end) {
            try {
                target = std::stoi(*(it + 1));
            } catch (const std::exception&) {
                throw std::runtime_error("invalid integer for " + flag + ": " + *(it + 1));
            }
            return true;
        }
    }
    return false;
}

bool read_double_arg(char** begin, char** end, const std::string& flag, double& target) {
    for (char** it = begin; it != end; ++it) {
        if (flag == *it && (it + 1) != end) {
            try {
                target = std::stod(*(it + 1));
            } catch (const std::exception&) {
                throw std::runtime_error("invalid number for " + flag + ": " + *(it + 1));
            }
            return true;
        }
    }
    return false;
}

}  // namespace

void print_cli_help(CliMode mode) {
    if (mode == CliMode::NetworkTest) {
        std::cout
            << "pj-net-test --server-host HOST --group NAME --user USER [options]\n"
            << "  --config FILE\n"
            << "  --server-port PORT\n"
            << "  --stun-host HOST\n"
            << "  --stun-port PORT\n"
            << "  --udp-port PORT\n"
            << "  --duration SECONDS\n"
            << "  --interval-ms MS\n"
            << "  --payload-bytes BYTES\n"
            << "  --path-reprobe-ms MS\n"
            << "  --path-switch-margin SCORE\n"
            << "  --path-switch-wins N\n"
            << "  --profile-out FILE\n"
            << "  --group-password PASSWORD\n"
            << "  --user-password PASSWORD\n";
        return;
    }

    std::cout
        << "pj-peer-probe --server-host HOST --group NAME --user USER [options]\n"
        << "  --config FILE\n"
        << "  --server-port PORT\n"
        << "  --stun-host HOST\n"
        << "  --stun-port PORT\n"
        << "  --udp-port PORT\n"
        << "  --duration SECONDS\n"
        << "  --interval-ms MS\n"
        << "  --payload-bytes BYTES\n"
        << "  --path-reprobe-ms MS\n"
        << "  --path-switch-margin SCORE\n"
        << "  --path-switch-wins N\n"
        << "  --profile-in FILE\n"
        << "  --audio-seconds SECONDS\n"
        << "  --sample-rate HZ\n"
        << "  --frame-samples SAMPLES\n"
        << "  --channels N\n"
        << "  --source-buffer-ms MS\n"
        << "  --sink-buffer-ms MS\n"
        << "  --tone-hz HZ\n"
        << "  --audio-device-io\n"
        << "  --disable-drift-correction\n"
        << "  --group-password PASSWORD\n"
        << "  --user-password PASSWORD\n";
}

std::string parse_config_path(int argc, char** argv) {
    std::string path;
    read_string_arg(argv, argv + argc, "--config", path);
    return path;
}

int parse_server_port_override(int argc, char** argv) {
    int port = 0;
    read_int_arg(argv, argv + argc, "--port", port);
    return port;
}

bool parse_peer_options(int argc, char** argv, CliMode mode, PeerOptions& options) {
    if (mode == CliMode::NetworkTest) {
        options.audio.seconds = 0;
    }

    read_string_arg(argv, argv + argc, "--server-host", options.server_host);
    read_int_arg(argv, argv + argc, "--server-port", options.server_port);
    read_string_arg(argv, argv + argc, "--stun-host", options.stun_host);
    read_int_arg(argv, argv + argc, "--stun-port", options.stun_port);
    read_string_arg(argv, argv + argc, "--group", options.group);
    read_string_arg(argv, argv + argc, "--group-password", options.group_password);
    read_string_arg(argv, argv + argc, "--user", options.user);
    read_string_arg(argv, argv + argc, "--user-password", options.user_password);
    read_int_arg(argv, argv + argc, "--udp-port", options.udp_port);
    read_int_arg(argv, argv + argc, "--duration", options.duration_seconds);
    read_double_arg(argv, argv + argc, "--interval-ms", options.interval_ms);
    read_int_arg(argv, argv + argc, "--payload-bytes", options.payload_bytes);
    read_int_arg(argv, argv + argc, "--path-reprobe-ms", options.path_reprobe_interval_ms);
    read_double_arg(argv, argv + argc, "--path-switch-margin", options.path_switch_margin_score);
    read_int_arg(argv, argv + argc, "--path-switch-wins", options.path_switch_consecutive_wins);
    read_string_arg(argv, argv + argc, "--profile-out", options.session_profile_out);

    if (mode == CliMode::PeerProbe) {
        read_string_arg(argv, argv + argc, "--profile-in", options.session_profile_in);
        read_int_arg(argv, argv + argc, "--audio-seconds", options.audio.seconds);
        read_int_arg(argv, argv + argc, "--sample-rate", options.audio.sample_rate_hz);
        read_int_arg(argv, argv + argc, "--frame-samples", options.audio.frame_samples);
        read_int_arg(argv, argv + argc, "--channels", options.audio.channels);
        read_int_arg(argv, argv + argc, "--source-buffer-ms", options.audio.source_buffer_ms);
        read_int_arg(argv, argv + argc, "--sink-buffer-ms", options.audio.sink_buffer_ms);
        read_double_arg(argv, argv + argc, "--tone-hz", options.audio.tone_hz);
        for (char** it = argv; it != argv + argc; ++it) {
            if (std::string(*it) == "--audio-device-io") {
                options.audio.device_io_enabled = true;
            }
            if (std::string(*it) == "--disable-drift-correction") {
                options.audio.dynamic_resampling = false;
            }
        }
    }

    return !options.server_host.empty() && !options.group.empty() && !options.user.empty();
}

}  // namespace peerjam
