#include "peerjam/logic/network/candidate_gatherer.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <cstring>

namespace peerjam {
namespace {

constexpr std::uint32_t kStunMagicCookie = 0x2112A442;
constexpr std::uint16_t kStunBindingRequest = 0x0001;
constexpr std::uint16_t kStunBindingSuccessResponse = 0x0101;
constexpr std::uint16_t kStunMappedAddress = 0x0001;
constexpr std::uint16_t kStunXorMappedAddress = 0x0020;

int candidate_priority(CandidateType type, bool is_ipv6, bool is_loopback) {
    int base = 0;
    switch (type) {
        case CandidateType::Host:
            base = 3000;
            break;
        case CandidateType::ServerReflexive:
            base = 2000;
            break;
        case CandidateType::Relay:
            base = 1000;
            break;
    }
    if (is_ipv6) {
        base += 100;
    }
    if (is_loopback) {
        base -= 500;
    }
    return base;
}

bool is_loopback_addr(const sockaddr* addr) {
    if (addr->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(addr);
        const auto octet = ntohl(in->sin_addr.s_addr) >> 24;
        return octet == 127;
    }
    if (addr->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(addr);
        static const in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
        return std::memcmp(&in6->sin6_addr, &loopback, sizeof(loopback)) == 0;
    }
    return false;
}

void write_u16(std::uint8_t* out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[1] = static_cast<std::uint8_t>(value & 0xFF);
}

void write_u32(std::uint8_t* out, std::uint32_t value) {
    out[0] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    out[1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[3] = static_cast<std::uint8_t>(value & 0xFF);
}

std::uint16_t read_u16(const std::uint8_t* in) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(in[0]) << 8) | in[1]);
}

std::uint32_t read_u32(const std::uint8_t* in) {
    return (static_cast<std::uint32_t>(in[0]) << 24)
        | (static_cast<std::uint32_t>(in[1]) << 16)
        | (static_cast<std::uint32_t>(in[2]) << 8)
        | static_cast<std::uint32_t>(in[3]);
}

std::array<std::uint8_t, 20> make_stun_binding_request(std::array<std::uint8_t, 12>& transaction_id) {
    std::array<std::uint8_t, 20> packet {};
    static thread_local std::mt19937_64 rng {std::random_device{}()};
    for (auto& byte : transaction_id) {
        byte = static_cast<std::uint8_t>(rng() & 0xFF);
    }
    write_u16(packet.data(), kStunBindingRequest);
    write_u16(packet.data() + 2, 0);
    write_u32(packet.data() + 4, kStunMagicCookie);
    std::memcpy(packet.data() + 8, transaction_id.data(), transaction_id.size());
    return packet;
}

bool resolve_udp_endpoint(const std::string& host, int port, sockaddr_storage& addr, socklen_t& addr_len) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;
    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || !result) {
        return false;
    }

    bool ok = false;
    for (auto* it = result; it != nullptr; it = it->ai_next) {
        if (it->ai_addrlen <= sizeof(sockaddr_storage)) {
            std::memcpy(&addr, it->ai_addr, static_cast<std::size_t>(it->ai_addrlen));
            addr_len = static_cast<socklen_t>(it->ai_addrlen);
            ok = true;
            break;
        }
    }
    ::freeaddrinfo(result);
    return ok;
}

std::optional<NetworkCandidate> parse_stun_success_response(
    const std::uint8_t* data,
    std::size_t size,
    const std::array<std::uint8_t, 12>& transaction_id,
    int local_port
) {
    if (size < 20) {
        return std::nullopt;
    }
    if (read_u16(data) != kStunBindingSuccessResponse) {
        return std::nullopt;
    }
    if (read_u32(data + 4) != kStunMagicCookie) {
        return std::nullopt;
    }
    if (std::memcmp(data + 8, transaction_id.data(), transaction_id.size()) != 0) {
        return std::nullopt;
    }

    const std::size_t message_length = read_u16(data + 2);
    std::size_t offset = 20;
    const std::size_t limit = std::min(size, 20 + message_length);
    while (offset + 4 <= limit) {
        const std::uint16_t attr_type = read_u16(data + offset);
        const std::uint16_t attr_len = read_u16(data + offset + 2);
        const std::size_t value_offset = offset + 4;
        if (value_offset + attr_len > limit) {
            break;
        }

        if ((attr_type == kStunXorMappedAddress || attr_type == kStunMappedAddress) && attr_len >= 8) {
            const std::uint8_t family = data[value_offset + 1];
            const std::uint16_t raw_port = read_u16(data + value_offset + 2);
            NetworkCandidate candidate;
            candidate.type = CandidateType::ServerReflexive;
            candidate.foundation = "srflx:stun";
            candidate.interface_name = "stun";
            if (family == 0x01 && attr_len >= 8) {
                auto* in = reinterpret_cast<sockaddr_in*>(&candidate.addr);
                in->sin_family = AF_INET;
                std::uint32_t addr_value = read_u32(data + value_offset + 4);
                std::uint16_t port_value = raw_port;
                if (attr_type == kStunXorMappedAddress) {
                    port_value ^= static_cast<std::uint16_t>(kStunMagicCookie >> 16);
                    addr_value ^= kStunMagicCookie;
                }
                in->sin_port = htons(port_value);
                in->sin_addr.s_addr = htonl(addr_value);
                candidate.addr_len = sizeof(sockaddr_in);
                candidate.is_ipv6 = false;
                candidate.is_loopback = false;
                candidate.priority = candidate_priority(candidate.type, false, false);
                return candidate;
            }
            if (family == 0x02 && attr_len >= 20) {
                auto* in6 = reinterpret_cast<sockaddr_in6*>(&candidate.addr);
                in6->sin6_family = AF_INET6;
                std::uint16_t port_value = raw_port;
                if (attr_type == kStunXorMappedAddress) {
                    port_value ^= static_cast<std::uint16_t>(kStunMagicCookie >> 16);
                }
                in6->sin6_port = htons(port_value);
                const std::uint8_t* src = data + value_offset + 4;
                std::uint8_t decoded[16] {};
                if (attr_type == kStunXorMappedAddress) {
                    const std::uint8_t cookie_bytes[4] = {
                        static_cast<std::uint8_t>((kStunMagicCookie >> 24) & 0xFF),
                        static_cast<std::uint8_t>((kStunMagicCookie >> 16) & 0xFF),
                        static_cast<std::uint8_t>((kStunMagicCookie >> 8) & 0xFF),
                        static_cast<std::uint8_t>(kStunMagicCookie & 0xFF),
                    };
                    for (int i = 0; i < 4; ++i) {
                        decoded[i] = src[i] ^ cookie_bytes[i];
                    }
                    for (int i = 0; i < 12; ++i) {
                        decoded[i + 4] = src[i + 4] ^ transaction_id[static_cast<std::size_t>(i)];
                    }
                    std::memcpy(&in6->sin6_addr, decoded, sizeof(decoded));
                } else {
                    std::memcpy(&in6->sin6_addr, src, 16);
                }
                candidate.addr_len = sizeof(sockaddr_in6);
                candidate.is_ipv6 = true;
                candidate.is_loopback = false;
                candidate.priority = candidate_priority(candidate.type, true, false);
                return candidate;
            }
        }

        offset = value_offset + attr_len;
        if (attr_len % 4 != 0) {
            offset += 4 - (attr_len % 4);
        }
    }

    (void)local_port;
    return std::nullopt;
}

}  // namespace

std::vector<NetworkCandidate> CandidateGatherer::gather_local_candidates(int port) const {
    std::vector<NetworkCandidate> candidates;
    ifaddrs* ifaddr = nullptr;
    if (::getifaddrs(&ifaddr) != 0) {
        return candidates;
    }

    for (auto* it = ifaddr; it != nullptr; it = it->ifa_next) {
        if (!it->ifa_addr || !(it->ifa_flags & IFF_UP)) {
            continue;
        }
        if (it->ifa_addr->sa_family != AF_INET && it->ifa_addr->sa_family != AF_INET6) {
            continue;
        }

        NetworkCandidate candidate;
        candidate.type = CandidateType::Host;
        candidate.foundation = std::string("host:") + (it->ifa_name ? it->ifa_name : "unknown");
        candidate.interface_name = it->ifa_name ? it->ifa_name : "unknown";
        candidate.is_ipv6 = it->ifa_addr->sa_family == AF_INET6;
        candidate.is_loopback = is_loopback_addr(it->ifa_addr);
        candidate.addr_len = candidate.is_ipv6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
        std::memcpy(&candidate.addr, it->ifa_addr, static_cast<std::size_t>(candidate.addr_len));
        if (candidate.is_ipv6) {
            reinterpret_cast<sockaddr_in6*>(&candidate.addr)->sin6_port = htons(static_cast<std::uint16_t>(port));
        } else {
            reinterpret_cast<sockaddr_in*>(&candidate.addr)->sin_port = htons(static_cast<std::uint16_t>(port));
        }
        candidate.priority = candidate_priority(candidate.type, candidate.is_ipv6, candidate.is_loopback);
        candidates.push_back(candidate);
    }

    ::freeifaddrs(ifaddr);
    return candidates;
}

std::optional<NetworkCandidate> CandidateGatherer::gather_server_reflexive_candidate(
    const UdpDatagramSocket& socket,
    const std::string& stun_host,
    int stun_port,
    int local_port
) const {
    if (!socket.is_open() || stun_host.empty() || stun_port <= 0) {
        return std::nullopt;
    }

    sockaddr_storage stun_addr {};
    socklen_t stun_addr_len = 0;
    if (!resolve_udp_endpoint(stun_host, stun_port, stun_addr, stun_addr_len)) {
        return std::nullopt;
    }

    std::array<std::uint8_t, 12> transaction_id {};
    const auto packet = make_stun_binding_request(transaction_id);
    if (socket.send_to(stun_addr, stun_addr_len, packet.data(), packet.size()) <= 0) {
        return std::nullopt;
    }

    pollfd pfd {};
    pfd.fd = socket.native_handle();
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, 250) <= 0 || !(pfd.revents & POLLIN)) {
        return std::nullopt;
    }

    sockaddr_storage response_addr {};
    socklen_t response_addr_len = sizeof(response_addr);
    std::array<std::uint8_t, 512> response {};
    const int size = socket.receive(response.data(), response.size(), response_addr, response_addr_len);
    if (size <= 0) {
        return std::nullopt;
    }
    return parse_stun_success_response(response.data(), static_cast<std::size_t>(size), transaction_id, local_port);
}

NetworkCandidate CandidateGatherer::make_remote_host_candidate(const sockaddr_storage& addr, socklen_t addr_len) const {
    NetworkCandidate candidate;
    candidate.type = CandidateType::Host;
    candidate.foundation = "remote:signaled";
    candidate.interface_name = "signaled";
    candidate.addr = addr;
    candidate.addr_len = addr_len;
    candidate.is_ipv6 = addr.ss_family == AF_INET6;
    candidate.is_loopback = is_loopback_addr(reinterpret_cast<const sockaddr*>(&addr));
    candidate.priority = candidate_priority(candidate.type, candidate.is_ipv6, candidate.is_loopback);
    return candidate;
}

}  // namespace peerjam
