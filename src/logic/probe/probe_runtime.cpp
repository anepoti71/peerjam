#include "peerjam/logic/probe/probe_runtime.hpp"

#include "peerjam/logic/probe/probe_packet.hpp"

#include <chrono>
#include <thread>

namespace peerjam {

ProbeRuntime::ProbeRuntime(const PeerOptions& options)
    : options_(options)
    , tracker_(static_cast<std::uint32_t>(
          std::max(1.0, (options_.duration_seconds * 1000.0) / options_.interval_ms))) {}

void ProbeRuntime::set_session_token(std::uint64_t token) {
    session_token_ = token;
}

bool ProbeRuntime::handle_datagram(const std::uint8_t* data, std::size_t size, const sockaddr_storage& addr, socklen_t addr_len, const UdpDatagramSocket& socket) {
    const auto packet = decode_packet(data, size);
    if (!packet) {
        return false;
    }

    const auto now = now_ns();
    if (packet->type == ProbePacketType::Probe) {
        ProbePacket echo = *packet;
        echo.type = ProbePacketType::Echo;
        echo.sent_at_ns = now;
        const auto encoded = encode_packet(echo);
        socket.send_to(addr, addr_len, encoded.data(), encoded.size());
        if (packet->session_token == session_token_) {
            tracker_.set_direct_path_established(true);
            tracker_.on_probe_received(packet->sequence, now);
        }
    } else if (packet->type == ProbePacketType::Echo && packet->session_token == session_token_) {
        tracker_.set_direct_path_established(true);
        tracker_.on_echo_received(packet->sequence, now);
    }
    return true;
}

void ProbeRuntime::run_probe_window(const UdpDatagramSocket& socket, const RemoteEndpoint& endpoint) {
    for (int i = 0; i < options_.warmup_packets; ++i) {
        ProbePacket packet;
        packet.session_token = session_token_;
        packet.sequence = static_cast<std::uint32_t>(i);
        packet.sent_at_ns = now_ns();
        packet.type = ProbePacketType::Probe;
        packet.payload.resize(static_cast<std::size_t>(options_.payload_bytes), 0);
        const auto encoded = encode_packet(packet);
        socket.send_to(endpoint.addr, endpoint.addr_len, encoded.data(), encoded.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const auto expected_packets = std::max(1, static_cast<int>((options_.duration_seconds * 1000.0) / options_.interval_ms));
    for (int i = 0; i < expected_packets; ++i) {
        ProbePacket packet;
        packet.session_token = session_token_;
        packet.sequence = static_cast<std::uint32_t>(i);
        packet.sent_at_ns = now_ns();
        packet.type = ProbePacketType::Probe;
        packet.payload.resize(static_cast<std::size_t>(options_.payload_bytes), 0);
        const auto encoded = encode_packet(packet);
        socket.send_to(endpoint.addr, endpoint.addr_len, encoded.data(), encoded.size());
        tracker_.mark_sent(packet.sequence, packet.sent_at_ns);
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(options_.interval_ms));
    }
}

MeasurementReport ProbeRuntime::finish() const {
    return tracker_.finish();
}

std::uint64_t ProbeRuntime::now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

}  // namespace peerjam
