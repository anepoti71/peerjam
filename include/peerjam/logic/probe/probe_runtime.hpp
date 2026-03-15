#pragma once

#include "peerjam/logic/probe/measurement.hpp"
#include "peerjam/logic/control/peer_types.hpp"
#include "peerjam/logic/transport/udp_datagram_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace peerjam {

class ProbeRuntime {
  public:
    explicit ProbeRuntime(const PeerOptions& options);

    void set_session_token(std::uint64_t token);
    bool handle_datagram(const std::uint8_t* data, std::size_t size, const sockaddr_storage& addr, socklen_t addr_len, const UdpDatagramSocket& socket);
    void run_probe_window(const UdpDatagramSocket& socket, const RemoteEndpoint& endpoint);
    MeasurementReport finish() const;

  private:
    static std::uint64_t now_ns();

    PeerOptions options_;
    MeasurementTracker tracker_;
    std::uint64_t session_token_ {};
};

}  // namespace peerjam
