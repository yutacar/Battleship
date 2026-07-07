/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * LAN peer discovery via UDP broadcast (PLAN.md §2.2 TcpTransport row +
 * Phase 4 spec). The host periodically broadcasts a small text announce
 * datagram on kDiscoveryPort; joining players listen on the same port and
 * build a host list from what they hear (sender IP comes from the datagram's
 * source address, so the announce payload itself never needs to know the
 * host's own IP). Direct-IP entry remains the UI fallback for networks that
 * filter broadcast (AP client isolation etc., PLAN.md §4 risk table).
 *
 * Announce payload (ASCII, colon-separated):
 *   BSHIP:<protocol_version>:<tcp_port>:<host_name>
 *
 * Like everything in net/, this is poll-driven and non-blocking so it can be
 * serviced from an LVGL timer without stalling the UI.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace battleship::net {

/// UDP port used for discovery announcements.
constexpr uint16_t kDiscoveryPort = 35353;

/// Prefix identifying our announce datagrams among unrelated broadcast noise.
inline constexpr const char* kDiscoveryMagic = "BSHIP";

/// One discovered host as seen by a joining player.
struct DiscoveredHost {
    std::string ip;         ///< IPv4 dotted-quad of the announcing host
    uint16_t tcp_port = 0;  ///< TCP port the host is listening on
    std::string name;       ///< human-readable host name from the announce
    uint32_t last_seen_ms = 0;
};

/// Host side: periodically broadcasts an announce datagram.
class UdpAnnouncer {
public:
    UdpAnnouncer() = default;
    ~UdpAnnouncer();
    UdpAnnouncer(const UdpAnnouncer&) = delete;
    UdpAnnouncer& operator=(const UdpAnnouncer&) = delete;

    /// Starts announcing "`host_name` is hosting on TCP `tcp_port`".
    /// Returns false if the UDP socket could not be created.
    bool start(const std::string& host_name, uint16_t tcp_port);

    /// Broadcasts one announce if `interval_ms` has elapsed since the last.
    /// Call from a timer tick with a monotonic millisecond clock.
    void poll(uint32_t now_ms, uint32_t interval_ms = 1000);

    void stop();

private:
    int fd_ = -1;
    std::string payload_;
    uint32_t last_announce_ms_ = 0;
    bool announced_once_ = false;
};

/// Join side: listens for announce datagrams and keeps a de-duplicated,
/// freshness-tracked host list.
class UdpDiscoverer {
public:
    UdpDiscoverer() = default;
    ~UdpDiscoverer();
    UdpDiscoverer(const UdpDiscoverer&) = delete;
    UdpDiscoverer& operator=(const UdpDiscoverer&) = delete;

    /// Binds the discovery port. Returns false if binding failed (e.g. the
    /// port is held exclusively by another process).
    bool start();

    /// Drains any received announce datagrams and updates the host list.
    /// Hosts unheard-from for longer than `expiry_ms` are dropped.
    void poll(uint32_t now_ms, uint32_t expiry_ms = 5000);

    [[nodiscard]] const std::vector<DiscoveredHost>& hosts() const { return hosts_; }

    void stop();

private:
    int fd_ = -1;
    std::vector<DiscoveredHost> hosts_;
};

/// Best-effort primary IPv4 address of this machine on the LAN (first
/// non-loopback, up interface), or "?" if none found. Shown on the Host
/// screen so the opponent can direct-IP join even without broadcast.
std::string local_ipv4_address();

/// This machine's hostname (short form), or "battleship" as fallback.
std::string local_host_name();

}  // namespace battleship::net
