#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace peerjam {

enum class ProbePacketType : std::uint8_t {
    Probe = 1,
    Echo = 2,
};

struct ProbePacket {
    std::uint64_t session_token {};
    std::uint32_t sequence {};
    std::uint64_t sent_at_ns {};
    ProbePacketType type {ProbePacketType::Probe};
    std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> encode_packet(const ProbePacket& packet);
std::optional<ProbePacket> decode_packet(const std::uint8_t* data, std::size_t size);

}  // namespace peerjam
