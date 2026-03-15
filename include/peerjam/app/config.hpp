#pragma once

#include "peerjam/logic/control/peer_types.hpp"

#include <string>

namespace peerjam {

enum class AppKind {
    SignalServer,
    NetTest,
    PeerProbe,
};

bool load_peer_config(const std::string& path, PeerOptions& options);
bool load_server_config(const std::string& path, ServerOptions& options);
const char* default_config_path(AppKind kind);

}  // namespace peerjam
