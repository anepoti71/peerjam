#include "peerjam/logic/probe/probe_packet.hpp"

#include <cstring>

namespace peerjam {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic {'P', 'J', 'N', 'P'};
constexpr std::uint8_t kVersion = 1;

std::uint64_t host_to_be64(std::uint64_t value) {
    std::uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result = (result << 8) | ((value >> (i * 8)) & 0xFF);
    }
    return result;
}

std::uint32_t host_to_be32(std::uint32_t value) {
    return ((value & 0x000000FFU) << 24U) | ((value & 0x0000FF00U) << 8U)
         | ((value & 0x00FF0000U) >> 8U) | ((value & 0xFF000000U) >> 24U);
}

std::uint64_t be64_to_host(std::uint64_t value) {
    return host_to_be64(value);
}

std::uint32_t be32_to_host(std::uint32_t value) {
    return host_to_be32(value);
}

}  // namespace

std::vector<std::uint8_t> encode_packet(const ProbePacket& packet) {
    const std::size_t header_size = 4 + 1 + 1 + 8 + 4 + 8 + 4;
    std::vector<std::uint8_t> out(header_size + packet.payload.size());
    std::size_t offset = 0;
    std::memcpy(out.data() + offset, kMagic.data(), kMagic.size());
    offset += kMagic.size();
    out[offset++] = kVersion;
    out[offset++] = static_cast<std::uint8_t>(packet.type);
    const auto session_be = host_to_be64(packet.session_token);
    std::memcpy(out.data() + offset, &session_be, sizeof(session_be));
    offset += sizeof(session_be);
    const auto seq_be = host_to_be32(packet.sequence);
    std::memcpy(out.data() + offset, &seq_be, sizeof(seq_be));
    offset += sizeof(seq_be);
    const auto sent_be = host_to_be64(packet.sent_at_ns);
    std::memcpy(out.data() + offset, &sent_be, sizeof(sent_be));
    offset += sizeof(sent_be);
    const auto payload_size_be = host_to_be32(static_cast<std::uint32_t>(packet.payload.size()));
    std::memcpy(out.data() + offset, &payload_size_be, sizeof(payload_size_be));
    offset += sizeof(payload_size_be);
    if (!packet.payload.empty()) {
        std::memcpy(out.data() + offset, packet.payload.data(), packet.payload.size());
    }
    return out;
}

std::optional<ProbePacket> decode_packet(const std::uint8_t* data, std::size_t size) {
    const std::size_t header_size = 4 + 1 + 1 + 8 + 4 + 8 + 4;
    if (size < header_size) {
        return std::nullopt;
    }
    if (std::memcmp(data, kMagic.data(), kMagic.size()) != 0) {
        return std::nullopt;
    }
    std::size_t offset = 4;
    if (data[offset++] != kVersion) {
        return std::nullopt;
    }
    const auto type = static_cast<ProbePacketType>(data[offset++]);
    std::uint64_t session_be = 0;
    std::memcpy(&session_be, data + offset, sizeof(session_be));
    offset += sizeof(session_be);
    std::uint32_t sequence_be = 0;
    std::memcpy(&sequence_be, data + offset, sizeof(sequence_be));
    offset += sizeof(sequence_be);
    std::uint64_t sent_be = 0;
    std::memcpy(&sent_be, data + offset, sizeof(sent_be));
    offset += sizeof(sent_be);
    std::uint32_t payload_size_be = 0;
    std::memcpy(&payload_size_be, data + offset, sizeof(payload_size_be));
    offset += sizeof(payload_size_be);
    const auto payload_size = static_cast<std::size_t>(be32_to_host(payload_size_be));
    if (size != header_size + payload_size) {
        return std::nullopt;
    }
    ProbePacket packet;
    packet.type = type;
    packet.session_token = be64_to_host(session_be);
    packet.sequence = be32_to_host(sequence_be);
    packet.sent_at_ns = be64_to_host(sent_be);
    packet.payload.assign(data + offset, data + offset + payload_size);
    return packet;
}

}  // namespace peerjam
