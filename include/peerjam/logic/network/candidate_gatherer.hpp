#pragma once

#include "peerjam/logic/transport/udp_datagram_socket.hpp"
#include "peerjam/logic/network/candidate.hpp"

#include <optional>
#include <vector>

namespace peerjam {

class CandidateGatherer {
  public:
    std::vector<NetworkCandidate> gather_local_candidates(int port) const;
    std::optional<NetworkCandidate> gather_server_reflexive_candidate(
        const UdpDatagramSocket& socket,
        const std::string& stun_host,
        int stun_port,
        int local_port
    ) const;
    NetworkCandidate make_remote_host_candidate(const sockaddr_storage& addr, socklen_t addr_len) const;
};

}  // namespace peerjam
