/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/lora_packet.h"

#include <cstring>

namespace battleship::net::lora {

IRadio::~IRadio() = default;

namespace {

constexpr std::size_t kHeaderLen = 5;
constexpr std::size_t kAdLen = sizeof(kBeaconAdText) - 1;

bool printable(char c) {
    return c >= 0x20 && c <= 0x7E;
}

} // namespace

std::vector<std::uint8_t> encode_packet(const Packet& p) {
    std::vector<std::uint8_t> out;
    out.push_back('B');
    out.push_back('S');
    out.push_back(static_cast<std::uint8_t>(p.type));
    out.push_back(static_cast<std::uint8_t>(p.session_id & 0xFF));
    out.push_back(static_cast<std::uint8_t>(p.session_id >> 8));
    switch (p.type) {
    case PacketType::Beacon: {
        out.push_back(p.flags);
        std::size_t n = 0;
        for (char c : p.host_name) {
            if (n == kMaxHostNameLen) break;
            out.push_back(static_cast<std::uint8_t>(printable(c) ? c : '?'));
            ++n;
        }
        out.push_back(0x00);
        out.insert(out.end(), kBeaconAdText, kBeaconAdText + kAdLen);
        break;
    }
    case PacketType::Join:
    case PacketType::Accept:
        out.push_back(static_cast<std::uint8_t>(p.nonce & 0xFF));
        out.push_back(static_cast<std::uint8_t>(p.nonce >> 8));
        break;
    case PacketType::Data: {
        std::size_t n = p.data.size() < kMaxDataLen ? p.data.size() : kMaxDataLen;
        out.insert(out.end(), p.data.begin(), p.data.begin() + static_cast<std::ptrdiff_t>(n));
        break;
    }
    case PacketType::Bye:
        break;
    }
    return out;
}

std::optional<Packet> decode_packet(const std::uint8_t* bytes, std::size_t len) {
    if (bytes == nullptr || len < kHeaderLen || bytes[0] != 'B' || bytes[1] != 'S') {
        return std::nullopt;
    }
    Packet p;
    p.session_id = static_cast<std::uint16_t>(bytes[3] | (bytes[4] << 8));
    const std::uint8_t* body = bytes + kHeaderLen;
    const std::size_t blen = len - kHeaderLen;
    switch (bytes[2]) {
    case static_cast<std::uint8_t>(PacketType::Beacon): {
        p.type = PacketType::Beacon;
        // flags + name + NUL + ad
        if (blen < 1 + 1 + kAdLen) return std::nullopt;
        p.flags = body[0];
        const std::size_t name_len = blen - 1 - 1 - kAdLen;
        if (name_len > kMaxHostNameLen) return std::nullopt;
        for (std::size_t i = 0; i < name_len; ++i) {
            if (!printable(static_cast<char>(body[1 + i]))) return std::nullopt;
        }
        if (body[1 + name_len] != 0x00) return std::nullopt;
        if (std::memcmp(body + 2 + name_len, kBeaconAdText, kAdLen) != 0) return std::nullopt;
        p.host_name.assign(reinterpret_cast<const char*>(body + 1), name_len);
        return p;
    }
    case static_cast<std::uint8_t>(PacketType::Join):
    case static_cast<std::uint8_t>(PacketType::Accept):
        p.type = static_cast<PacketType>(bytes[2]);
        if (blen != 2) return std::nullopt;
        p.nonce = static_cast<std::uint16_t>(body[0] | (body[1] << 8));
        return p;
    case static_cast<std::uint8_t>(PacketType::Data):
        p.type = PacketType::Data;
        if (blen < 1 || blen > kMaxDataLen) return std::nullopt;
        p.data.assign(body, body + blen);
        return p;
    case static_cast<std::uint8_t>(PacketType::Bye):
        p.type = PacketType::Bye;
        if (blen != 0) return std::nullopt;
        return p;
    default:
        return std::nullopt;
    }
}

} // namespace battleship::net::lora
