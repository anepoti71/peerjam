#include "aoo/aoo_net.h"
#include "peerjam/logic/network/candidate_exchange.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int parse_port(int argc, char** argv, int default_port) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--port") {
            return std::stoi(argv[i + 1]);
        }
    }
    return default_port;
}

}  // namespace

int main(int argc, char** argv) {
    const int port = parse_port(argc, argv, 10999);
    peerjam::CandidateExchangeServer candidate_server;
    if (!candidate_server.start(port + 1)) {
        std::cerr << "failed to create candidate signaling server on TCP port " << (port + 1) << "\n";
        return 1;
    }
    int32_t error = 0;
    aoonet_server* server = aoonet_server_new(port, &error);
    if (!server) {
        std::cerr << "failed to create signaling server on port " << port << ", error=" << error << "\n";
        return 1;
    }

    std::cout << "pj-signal-server listening on TCP/UDP port " << port
              << " and candidate exchange TCP port " << (port + 1) << "\n";
    const int rc = aoonet_server_run(server);
    candidate_server.stop();
    aoonet_server_free(server);
    return rc > 0 ? 0 : 1;
}
