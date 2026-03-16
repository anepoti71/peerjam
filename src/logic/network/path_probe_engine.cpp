#include "peerjam/logic/network/path_probe_engine.hpp"

#include "peerjam/logic/probe/probe_packet.hpp"
#include "peerjam/logic/probe/measurement.hpp"
#include "peerjam/logic/transport/udp_datagram_socket.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace peerjam {
namespace {

constexpr int kProbePollMs = 5;
constexpr std::size_t kPacketHeaderSize = 4 + 1 + 1 + 8 + 4 + 8 + 4;
constexpr std::size_t kTypeOffset = 5;
constexpr std::size_t kSessionOffset = 6;
constexpr std::size_t kSequenceOffset = 14;
constexpr std::size_t kSentAtOffset = 18;
constexpr std::size_t kPayloadSizeOffset = 26;
constexpr std::array<std::uint8_t, 4> kPacketMagic {'P', 'J', 'N', 'P'};
constexpr std::uint8_t kPacketVersion = 1;

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

std::uint64_t host_to_be64(std::uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(value);
#else
    return value;
#endif
}

std::uint32_t host_to_be32(std::uint32_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

void write_be64(std::vector<std::uint8_t>& packet, std::size_t offset, std::uint64_t value) {
    const auto be = host_to_be64(value);
    std::memcpy(packet.data() + offset, &be, sizeof(be));
}

void write_be32(std::vector<std::uint8_t>& packet, std::size_t offset, std::uint32_t value) {
    const auto be = host_to_be32(value);
    std::memcpy(packet.data() + offset, &be, sizeof(be));
}

std::vector<std::uint8_t> make_probe_packet_template(std::uint64_t session_token, int payload_bytes) {
    const auto payload_size = static_cast<std::size_t>(std::max(0, payload_bytes));
    std::vector<std::uint8_t> packet(kPacketHeaderSize + payload_size, 0);
    std::memcpy(packet.data(), kPacketMagic.data(), kPacketMagic.size());
    packet[4] = kPacketVersion;
    packet[kTypeOffset] = static_cast<std::uint8_t>(ProbePacketType::Probe);
    write_be64(packet, kSessionOffset, session_token);
    write_be32(packet, kPayloadSizeOffset, static_cast<std::uint32_t>(payload_size));
    return packet;
}

std::string endpoint_key(const sockaddr_storage& addr) {
    std::string key;
    if (addr.ss_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(&addr);
        key.reserve(1 + sizeof(in->sin_port) + sizeof(in->sin_addr));
        key.push_back(static_cast<char>(AF_INET));
        key.append(reinterpret_cast<const char*>(&in->sin_port), sizeof(in->sin_port));
        key.append(reinterpret_cast<const char*>(&in->sin_addr), sizeof(in->sin_addr));
        return key;
    }
    if (addr.ss_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        key.reserve(1 + sizeof(in6->sin6_port) + sizeof(in6->sin6_addr));
        key.push_back(static_cast<char>(AF_INET6));
        key.append(reinterpret_cast<const char*>(&in6->sin6_port), sizeof(in6->sin6_port));
        key.append(reinterpret_cast<const char*>(&in6->sin6_addr), sizeof(in6->sin6_addr));
        return key;
    }
    key.push_back(static_cast<char>(addr.ss_family));
    return key;
}

struct PairProbeContext {
    NetworkCandidate remote;
    MeasurementTracker tracker;
    std::vector<std::uint8_t> encoded;
};

void send_probe(
    PairProbeContext& ctx,
    const UdpDatagramSocket& socket,
    std::uint32_t sequence,
    bool warmup_only
) {
    if (ctx.encoded.empty()) {
        return;
    }
    const auto sent_at_ns = now_ns();
    write_be32(ctx.encoded, kSequenceOffset, sequence);
    write_be64(ctx.encoded, kSentAtOffset, sent_at_ns);
    socket.send_to(ctx.remote.addr, ctx.remote.addr_len, ctx.encoded.data(), ctx.encoded.size());
    if (!warmup_only) {
        ctx.tracker.mark_sent(sequence, sent_at_ns);
    }
}

void drain_echoes(
    std::vector<PairProbeContext>& contexts,
    const std::unordered_map<std::string, std::size_t>& endpoint_index,
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
        const auto it = endpoint_index.find(endpoint_key(addr));
        if (it == endpoint_index.end() || it->second >= contexts.size()) {
            continue;
        }
        const auto arrival_ns = now_ns();
        auto& ctx = contexts[it->second];
        if (packet->type == ProbePacketType::Echo) {
            ctx.tracker.on_echo_received(packet->sequence, arrival_ns);
        } else if (packet->type == ProbePacketType::Probe) {
            ctx.tracker.on_probe_received(packet->sequence, arrival_ns);
        }
    }
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
    std::vector<std::vector<CandidatePairMeasurement>> per_local(local_candidates.size());
    std::vector<std::thread> workers;
    workers.reserve(local_candidates.size());

    for (std::size_t local_idx = 0; local_idx < local_candidates.size(); ++local_idx) {
        workers.emplace_back([&, local_idx] {
            const auto& local = local_candidates[local_idx];
            UdpDatagramSocket socket;
            if (socket.open_bound(local.addr, local.addr_len) != 0) {
                return;
            }
            auto& local_measurements = per_local[local_idx];
            local_measurements.reserve(remote_candidates.size());

            std::vector<PairProbeContext> contexts;
            contexts.reserve(remote_candidates.size());
            std::unordered_map<std::string, std::size_t> endpoint_index;
            endpoint_index.reserve(remote_candidates.size());
            for (const auto& remote : remote_candidates) {
                // Cross-family pairs (IPv4 local with IPv6 remote or vice versa) are not valid
                // direct-path candidates for this probe socket model.
                if (local.addr.ss_family != remote.addr.ss_family) {
                    continue;
                }
                // Loopback/non-loopback pairs are unreachable in real network sessions.
                if (local.is_loopback != remote.is_loopback) {
                    continue;
                }
                PairProbeContext context {
                    .remote = remote,
                    .tracker = MeasurementTracker(
                        static_cast<std::uint32_t>(std::max(1.0, (options_.duration_seconds * 1000.0) / options_.interval_ms)),
                        options_.recommendation
                    ),
                    .encoded = make_probe_packet_template(pair_probe_token, options_.payload_bytes),
                };
                const auto index = contexts.size();
                contexts.push_back(std::move(context));
                endpoint_index.emplace(endpoint_key(remote.addr), index);
            }
            if (contexts.empty()) {
                return;
            }

            for (int i = 0; i < options_.warmup_packets; ++i) {
                for (auto& context : contexts) {
                    send_probe(context, socket, static_cast<std::uint32_t>(i), true);
                }
                drain_echoes(
                    contexts,
                    endpoint_index,
                    socket,
                    pair_probe_token,
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(15)
                );
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            const auto expected_packets = std::max(1, static_cast<int>((options_.duration_seconds * 1000.0) / options_.interval_ms));
            for (int i = 0; i < expected_packets; ++i) {
                for (auto& context : contexts) {
                    send_probe(context, socket, static_cast<std::uint32_t>(i), false);
                }
                drain_echoes(
                    contexts,
                    endpoint_index,
                    socket,
                    pair_probe_token,
                    std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double, std::milli>(options_.interval_ms))
                );
            }

            drain_echoes(
                contexts,
                endpoint_index,
                socket,
                pair_probe_token,
                std::chrono::steady_clock::now() + std::chrono::milliseconds(250)
            );
            for (auto& context : contexts) {
                CandidatePairMeasurement pair;
                pair.local = local;
                pair.remote = context.remote;
                pair.probe = context.tracker.finish();
                pair.score = scorer_.score(pair.probe, local, context.remote);
                local_measurements.push_back(std::move(pair));
            }
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    std::vector<CandidatePairMeasurement> measurements;
    measurements.reserve(local_candidates.size() * remote_candidates.size());
    for (auto& local_measurements : per_local) {
        for (auto& pair : local_measurements) {
            measurements.push_back(std::move(pair));
        }
    }
    return measurements;
}

}  // namespace peerjam
