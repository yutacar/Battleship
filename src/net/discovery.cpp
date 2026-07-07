/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "net/discovery.h"

#include "net/frame.h"  // kProtocolVersion

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace battleship::net {

namespace {

bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

// ---------------------------------------------------------------- announcer

UdpAnnouncer::~UdpAnnouncer() { stop(); }

bool UdpAnnouncer::start(const std::string& host_name, uint16_t tcp_port) {
    stop();

    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;

    int one = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) != 0 || !set_nonblocking(fd_)) {
        stop();
        return false;
    }

    // Colons delimit fields, so strip any from the (user-influenced) host
    // name to keep the payload parseable.
    std::string safe_name;
    for (char c : host_name) {
        if (c != ':' && c != '\n' && c != '\r') safe_name.push_back(c);
    }
    if (safe_name.empty()) safe_name = "battleship";

    payload_ = std::string(kDiscoveryMagic) + ":" + std::to_string(kProtocolVersion) + ":" +
               std::to_string(tcp_port) + ":" + safe_name;
    announced_once_ = false;
    return true;
}

void UdpAnnouncer::poll(uint32_t now_ms, uint32_t interval_ms) {
    if (fd_ < 0) return;
    if (announced_once_ && now_ms - last_announce_ms_ < interval_ms) return;

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    dest.sin_port = htons(kDiscoveryPort);
    // Best-effort: broadcast failures (no network, filtered) are silently
    // ignored -- Direct IP join remains available as the fallback.
    ::sendto(fd_, payload_.data(), payload_.size(), 0, reinterpret_cast<sockaddr*>(&dest),
             sizeof(dest));

    // Also announce on loopback: limited-broadcast datagrams are not looped
    // back to listeners on the sending machine (observed on macOS), so
    // without this a second local instance -- the 2-process desktop test
    // setup -- never sees the host in its Join list. Real two-device LAN
    // discovery uses the broadcast path above; this extra ~40-byte datagram
    // is harmless there.
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(kDiscoveryPort);
    ::sendto(fd_, payload_.data(), payload_.size(), 0, reinterpret_cast<sockaddr*>(&local),
             sizeof(local));
    last_announce_ms_ = now_ms;
    announced_once_ = true;
}

void UdpAnnouncer::stop() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// --------------------------------------------------------------- discoverer

UdpDiscoverer::~UdpDiscoverer() { stop(); }

bool UdpDiscoverer::start() {
    stop();

    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;

    int one = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    // Lets a Join screen coexist with another local instance (2-process
    // desktop testing on one Mac) both bound to kDiscoveryPort.
    setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kDiscoveryPort);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || !set_nonblocking(fd_)) {
        stop();
        return false;
    }
    return true;
}

void UdpDiscoverer::poll(uint32_t now_ms, uint32_t expiry_ms) {
    if (fd_ < 0) return;

    char buf[512];
    while (true) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        const ssize_t got = ::recvfrom(fd_, buf, sizeof(buf) - 1, 0,
                                       reinterpret_cast<sockaddr*>(&from), &from_len);
        if (got <= 0) break;  // EAGAIN/empty -- done for this tick
        buf[got] = '\0';

        // Parse "BSHIP:<ver>:<tcp_port>:<name>".
        const std::string text(buf);
        const std::string magic_prefix = std::string(kDiscoveryMagic) + ":";
        if (text.compare(0, magic_prefix.size(), magic_prefix) != 0) continue;

        const size_t ver_start = magic_prefix.size();
        const size_t ver_end = text.find(':', ver_start);
        if (ver_end == std::string::npos) continue;
        const size_t port_end = text.find(':', ver_end + 1);
        if (port_end == std::string::npos) continue;

        int version = 0;
        int tcp_port = 0;
        try {
            version = std::stoi(text.substr(ver_start, ver_end - ver_start));
            tcp_port = std::stoi(text.substr(ver_end + 1, port_end - ver_end - 1));
        } catch (const std::exception&) {
            continue;
        }
        if (version != kProtocolVersion) continue;  // incompatible peer; hide it
        if (tcp_port <= 0 || tcp_port > 65535) continue;

        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));

        DiscoveredHost host;
        host.ip = ip;
        host.tcp_port = static_cast<uint16_t>(tcp_port);
        host.name = text.substr(port_end + 1);
        host.last_seen_ms = now_ms;

        bool updated = false;
        for (auto& existing : hosts_) {
            if (existing.ip == host.ip && existing.tcp_port == host.tcp_port) {
                existing = host;
                updated = true;
                break;
            }
        }
        if (!updated) hosts_.push_back(host);
    }

    // Expire hosts that stopped announcing (host cancelled / match started).
    hosts_.erase(std::remove_if(hosts_.begin(), hosts_.end(),
                                [&](const DiscoveredHost& h) {
                                    return now_ms - h.last_seen_ms > expiry_ms;
                                }),
                 hosts_.end());
}

void UdpDiscoverer::stop() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    hosts_.clear();
}

// ------------------------------------------------------------------ helpers

std::string local_ipv4_address() {
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0 || !list) return "?";

    std::string result = "?";
    for (const ifaddrs* ifa = list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
        char ip[INET_ADDRSTRLEN] = {};
        const auto* sin = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) {
            result = ip;
            break;  // first up, non-loopback IPv4 wins (typically wlan0/en0)
        }
    }
    freeifaddrs(list);
    return result;
}

std::string local_host_name() {
    char name[128] = {};
    if (gethostname(name, sizeof(name) - 1) != 0 || name[0] == '\0') {
        return "battleship";
    }
    // Short form: strip everything after the first dot (mDNS suffixes etc.).
    std::string result(name);
    const auto dot = result.find('.');
    if (dot != std::string::npos) result.resize(dot);
    return result;
}

}  // namespace battleship::net
