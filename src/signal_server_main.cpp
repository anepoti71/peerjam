#include "aoo/aoo_net.h"
#include "peerjam/app/cli_options.hpp"
#include "peerjam/app/config.hpp"
#include "peerjam/common/logger.hpp"
#include "peerjam/logic/network/candidate_exchange.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout
                << "pj-signal-server [options]\n"
                << "  --config FILE\n"
                << "  --port PORT\n";
            return 0;
        }
    }
    try {
        peerjam::ServerOptions options;
        options.port = 10999;
        options.candidate_exchange_port = 11000;
        options.logging.enabled = true;
        options.logging.level = "info";
        options.logging.sink = "stderr";
        const auto raw_config_path = peerjam::parse_config_path(argc, argv);
        const std::string config_path = raw_config_path.empty()
            ? peerjam::default_config_path(peerjam::AppKind::SignalServer)
            : raw_config_path;
        peerjam::load_server_config(config_path, options);
        const int cli_port = peerjam::parse_server_port_override(argc, argv);
        if (cli_port > 0) {
            options.port = cli_port;
        }

        peerjam::Logger::instance().configure(options.logging);

        peerjam::CandidateExchangeServer candidate_server;
        if (!candidate_server.start(options.candidate_exchange_port)) {
            PJ_LOG(Error, "failed to create candidate signaling server on TCP port " << options.candidate_exchange_port);
            return 1;
        }

        int32_t error = 0;
        aoonet_server* server = aoonet_server_new(options.port, &error);
        if (!server) {
            PJ_LOG(Error, "failed to create signaling server on port " << options.port << ", error=" << error);
            return 1;
        }

        PJ_LOG(Info, "pj-signal-server listening on TCP/UDP port " << options.port
            << " and candidate exchange TCP port " << options.candidate_exchange_port);
        const int rc = aoonet_server_run(server);
        candidate_server.stop();
        aoonet_server_free(server);
        return rc > 0 ? 0 : 1;
    } catch (const std::exception& exc) {
        std::cerr << "fatal: " << exc.what() << "\n";
        return 1;
    }
}
