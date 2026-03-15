#include "peerjam/logic/network/path_probe_engine.hpp"

#include "peerjam/logic/probe/probe_packet.hpp"
#include "peerjam/logic/probe/measurement.hpp"
#include "peerjam/logic/transport/udp_datagram_socket.hpp"

#include <array>
#include <chrono>
#include <poll.h>
#include <thread>

namespace peerjam {
namespace {

constexpr int kProbePollMs = 5;

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

void drain_echoes(
    MeasurementTracker& tracker,
    const UdpDatagramSocket& socket,
    std::uint64_t session_token,
    std::chrono::steady_clock::time_point deadline
) {
    std::array<std::uint8_t, 1500> buffer {};
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd pfd {};
        pfd.fd = socket.native_handle();
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, kProbePollMs) <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        sockaddr_storage addr {};
        socklen_t addr_len = sizeof(addr);
        const int n = socket.receive(buffer.data(), buffer.size(), addr, addr_len);
        if (n <= 0) {
            continue;
        }
        const auto packet = decode_packet(buffer.data(), static_cast<std::size_t>(n));
        if (!packet || packet->session_token != session_token) {
            continue;
        }
        const auto arrival_ns = now_ns();
        if (packet->type == ProbePacketType::Echo) {
            tracker.set_direct_path_established(true);
            tracker.on_echo_received(packet->sequence, arrival_ns);
        } else if (packet->type == ProbePacketType::Probe) {
            tracker.set_direct_path_established(true);
            tracker.on_probe_received(packet->sequence, arrival_ns);
        }
    }
}

MeasurementReport probe_pair(
    const PeerOptions& options,
    const NetworkCandidate& local,
    const NetworkCandidate& remote,
    std::uint64_t session_token
) {
    UdpDatagramSocket socket;
    if (socket.open_bound(local.addr, local.addr_len) != 0) {
        return {};
    }

    MeasurementTracker tracker(static_cast<std::uint32_t>(
        std::max(1.0, (options.duration_seconds * 1000.0) / options.interval_ms)));

    auto send_probe = [&](std::uint32_t sequence, bool warmup_only) {
        ProbePacket packet;
        packet.session_token = session_token;
        packet.sequence = sequence;
        packet.sent_at_ns = now_ns();
        packet.type = ProbePacketType::Probe;
        packet.payload.resize(static_cast<std::size_t>(options.payload_bytes), 0);
        const auto encoded = encode_packet(packet);
        socket.send_to(remote.addr, remote.addr_len, encoded.data(), encoded.size());
        if (!warmup_only) {
            tracker.mark_sent(sequence, packet.sent_at_ns);
        }
    };

    for (int i = 0; i < options.warmup_packets; ++i) {
        send_probe(static_cast<std::uint32_t>(i), true);
        drain_echoes(tracker, socket, session_token, std::chrono::steady_clock::now() + std::chrono::milliseconds(15));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto expected_packets = std::max(1, static_cast<int>((options.duration_seconds * 1000.0) / options.interval_ms));
    for (int i = 0; i < expected_packets; ++i) {
        send_probe(static_cast<std::uint32_t>(i), false);
        drain_echoes(
            tracker,
            socket,
            session_token,
            std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double, std::milli>(options.interval_ms))
        );
    }

    drain_echoes(tracker, socket, session_token, std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
    return tracker.finish();
}

}  // namespace

CandidateProbeEngine::CandidateProbeEngine(const PeerOptions& options)
    : options_(options) {}

std::vector<CandidatePairMeasurement> CandidateProbeEngine::measure_candidate_pairs(
    const std::vector<NetworkCandidate>& local_candidates,
    const std::vector<NetworkCandidate>& remote_candidates,
    std::uint64_t session_token
) const {
    const std::uint64_t pair_probe_token = session_token ^ 0x9E3779B97F4A7C15ULL;
    std::vector<CandidatePairMeasurement> measurements;
    measurements.reserve(local_candidates.size() * remote_candidates.size());
    for (const auto& local : local_candidates) {
        for (const auto& remote : remote_candidates) {
            CandidatePairMeasurement pair;
            pair.local = local;
            pair.remote = remote;
            pair.probe = probe_pair(options_, local, remote, pair_probe_token);
            pair.score = scorer_.score(pair.probe, local, remote);
            measurements.push_back(std::move(pair));
        }
    }
    return measurements;
}

}  // namespace peerjam
