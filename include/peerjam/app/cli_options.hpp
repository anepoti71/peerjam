#pragma once

#include "peerjam/logic/control/peer_types.hpp"

namespace peerjam {

enum class CliMode {
    NetworkTest,
    PeerProbe,
};

bool parse_peer_options(int argc, char** argv, CliMode mode, PeerOptions& options);
void print_cli_help(CliMode mode);
std::string parse_config_path(int argc, char** argv);
int parse_server_port_override(int argc, char** argv);

}  // namespace peerjam
