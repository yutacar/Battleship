/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/lora/radio_iface.h"

namespace battleship::net::lora {

enum class PacketType : std::uint8_t {
    Beacon = 0x01,
    Join = 0x02,
    Accept = 0x03,
    Data = 0x10,
    Bye = 0x11
};

// Human-readable advertisement carried verbatim in every beacon.
inline constexpr char kBeaconAdText[] = "BATTLESHIP for CardputerZero - Store code BSHP";

constexpr std::size_t kMaxHostNameLen = 12;
constexpr std::size_t kMaxDataLen = 16;

struct Packet {
    PacketType type = PacketType::Bye;
    std::uint16_t session_id = 0;
    std::uint16_t nonce = 0;  // Join / Accept
    std::uint8_t flags = 0;   // Beacon
    std::string host_name;    // Beacon (printable ASCII, <= kMaxHostNameLen)
    std::vector<std::uint8_t> data; // Data (1..kMaxDataLen)
};

// Layout: 'B' 'S' type sid_lo sid_hi, then per type:
//   Beacon: flags name(ASCII) 0x00 kBeaconAdText
//   Join/Accept: nonce_lo nonce_hi
//   Data: 1..16 bytes
//   Bye: nothing
std::vector<std::uint8_t> encode_packet(const Packet& p);
std::optional<Packet> decode_packet(const std::uint8_t* bytes, std::size_t len);

} // namespace battleship::net::lora
