#include "peerjam/app/cli_options.hpp"
#include "peerjam/app/config.hpp"
#include "peerjam/common/logger.hpp"
#include "peerjam/logic/control/aoo_peer_client.hpp"

#include <iostream>
#include <string>

using peerjam::AooPeerClient;
using peerjam::CliMode;
using peerjam::PeerOptions;

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            peerjam::print_cli_help(CliMode::NetworkTest);
            return 0;
        }
    }
    if (argc == 1) {
        peerjam::print_cli_help(CliMode::NetworkTest);
        return 1;
    }

    try {
        PeerOptions options;
        options.server_port = 10999;
        options.candidate_exchange_port = 11000;
        options.stun_port = 3478;
        options.udp_port = 12000;
        options.duration_seconds = 8;
        options.interval_ms = 10.0;
        options.payload_bytes = 64;
        options.warmup_packets = 20;
        options.path_reprobe_interval_ms = 3000;
        options.path_switch_margin_score = 1.0;
        options.path_switch_consecutive_wins = 2;
        options.logging.enabled = true;
        options.logging.level = "info";
        options.logging.sink = "stderr";
        options.audio.sample_rate_hz = 48000;
        options.audio.frame_samples = 128;
        options.audio.channels = 2;
        options.audio.source_buffer_ms = 3;
        options.audio.sink_buffer_ms = 5;
        options.audio.packetsize = 512;
        options.audio.resend_buffer_ms = 30;
        options.audio.resend_limit = 3;
        options.audio.resend_interval_ms = 3;
        options.audio.resend_max_frames = 4;
        options.audio.redundancy = 1;
        options.audio.dynamic_resampling = true;
        options.audio.tone_hz = 440.0;
        options.audio.tone_level = 0.15;
        options.audio.source_id = 1;
        options.audio.sink_id = 1;
        const auto raw_config_path = peerjam::parse_config_path(argc, argv);
        const std::string config_path = raw_config_path.empty()
            ? peerjam::default_config_path(peerjam::AppKind::NetTest)
            : raw_config_path;
        peerjam::load_peer_config(config_path, options);
        if (!peerjam::parse_peer_options(argc, argv, CliMode::NetworkTest, options)) {
            peerjam::print_cli_help(CliMode::NetworkTest);
            return 1;
        }
        peerjam::Logger::instance().configure(options.logging);
        AooPeerClient client(options);
        return client.run_network_test();
    } catch (const std::exception& exc) {
        std::cerr << "fatal: " << exc.what() << "\n";
        return 1;
    }
}
