/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Hardware bring-up CLI for the Cap LoRa-1262 (SX1262) on CardputerZero.
 * Not installed: it is pushed to the device by hand (see NOTES.md).
 *
 *   battleship_lora_probe info
 *   battleship_lora_probe ping [--count N]      (default 20)
 *   battleship_lora_probe pong                  (until SIGINT/SIGTERM)
 *   battleship_lora_probe listen [--seconds S]  (default 30)
 *   battleship_lora_probe blast [--seconds S]   (default 10)
 *
 * Every transmission goes through Japan920Policy (airtime <= 400 ms, 50 ms
 * pause after each emission); the driver adds the >= 5 ms carrier sense.
 */

#include "net/lora/cap_power.h"
#include "net/lora/japan920_policy.h"
#include "net/lora/lora_packet.h"
#include "net/lora/radio_iface.h"
#include "net/lora/sx1262_radio.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace battleship::net::lora;
using Clock = std::chrono::steady_clock;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

constexpr std::uint16_t kSidPing = 0x1001;
constexpr std::uint16_t kSidPong = 0x1002;
constexpr std::uint16_t kSidBlast = 0x1003;

std::uint64_t now_ms() {
    static const auto t0 = Clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
}

void sleep_ms(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

const char* init_error_name(RadioInitError e) {
    switch (e) {
    case RadioInitError::None: return "None";
    case RadioInitError::PowerFailed: return "PowerFailed";
    case RadioInitError::SpiOpenFailed: return "SpiOpenFailed";
    case RadioInitError::GpioFailed: return "GpioFailed";
    case RadioInitError::ExpanderMissing: return "ExpanderMissing";
    case RadioInitError::RadioNoResponse: return "RadioNoResponse";
    case RadioInitError::ConfigFailed: return "ConfigFailed";
    }
    return "?";
}

std::string read_brightness() {
    std::string out = "unreadable";
    if (FILE* f = std::fopen(kCapPowerPath, "r")) {
        char buf[32] = {};
        if (std::fgets(buf, sizeof(buf), f)) {
            out = buf;
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        }
        std::fclose(f);
    }
    return out;
}

// Power + radio with the ownership rule: radio down first, then power off.
struct Session {
    CapPower power;
    std::unique_ptr<Sx1262Radio> radio;
    RadioInitError error = RadioInitError::None;
    bool power_ok = false;

    Session() {
        power_ok = power.acquire();
        if (!power_ok) {
            error = RadioInitError::PowerFailed;
            return;
        }
        radio = std::make_unique<Sx1262Radio>(Sx1262Config{});
        error = radio->init_error();
    }
    bool ok() const { return power_ok && radio && error == RadioInitError::None; }
    void close() {
        if (radio) {
            radio->shutdown();
            radio.reset();
        }
        power.release();
    }
    ~Session() { close(); }
};

struct Rx {
    std::vector<std::uint8_t> bytes;
    std::int16_t rssi = 0;
    double snr = 0.0;
    std::uint64_t t_ms = 0;
};

// Policy-paced transmit + receive queue around the radio.
struct Link {
    Sx1262Radio& radio;
    Japan920Policy policy;
    std::deque<Rx> rxq;
    unsigned busy = 0;
    unsigned sent = 0;
    std::vector<double> tx_start_us;
    std::vector<double> rx_rearm_us;

    explicit Link(Sx1262Radio& r) : radio(r) {}

    void pump() {
        std::uint8_t buf[kMaxRadioPacket];
        const RadioPoll p = radio.poll(buf, sizeof(buf));
        if (p.event == RadioEvent::Received) {
            Rx rx;
            rx.bytes.assign(buf, buf + p.size);
            rx.rssi = p.rssi_dbm;
            rx.snr = p.snr_quarter_db / 4.0;
            rx.t_ms = now_ms();
            rxq.push_back(std::move(rx));
            rx_rearm_us.push_back(radio.last_rx_rearm_us());
        } else if (p.event == RadioEvent::Failed) {
            std::fprintf(stderr, "radio poll: Failed\n");
        }
    }

    // Returns true once the packet has started; false on hard failure/stop.
    // busy_here (optional) counts Busy results of this call.
    bool send(const std::vector<std::uint8_t>& bytes, unsigned* busy_here = nullptr,
              unsigned timeout_ms = 5000) {
        const std::uint64_t deadline = now_ms() + timeout_ms;
        while (!g_stop && now_ms() < deadline) {
            pump();
            const TxDecision d = policy.evaluate(now_ms(), bytes.size());
            if (d.status == TxDecisionStatus::Invalid || d.status == TxDecisionStatus::Locked) {
                std::fprintf(stderr, "policy rejected transmit\n");
                return false;
            }
            if (d.status == TxDecisionStatus::Deferred) {
                sleep_ms(1);
                continue;
            }
            const RadioStart s = radio.try_start_transmit(bytes.data(), bytes.size());
            if (s == RadioStart::Started) {
                policy.record(now_ms(), bytes.size());
                tx_start_us.push_back(radio.last_tx_start_us());
                ++sent;
                return true;
            }
            if (s == RadioStart::Busy) {
                ++busy;
                if (busy_here) ++*busy_here;
                sleep_ms(4);
                continue;
            }
            std::fprintf(stderr, "try_start_transmit: Failed\n");
            return false;
        }
        return false;
    }
};

std::vector<std::uint8_t> make_data(std::uint16_t sid, const std::vector<std::uint8_t>& payload) {
    Packet p;
    p.type = PacketType::Data;
    p.session_id = sid;
    p.data = payload;
    return encode_packet(p);
}

std::vector<std::uint8_t> seq_payload(std::uint16_t seq) {
    return {static_cast<std::uint8_t>(seq & 0xFF), static_cast<std::uint8_t>(seq >> 8), 'P', 'R', 'B'};
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}
double vmax(const std::vector<double>& v) { return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()); }
double vmin(const std::vector<double>& v) { return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end()); }

int arg_int(int argc, char** argv, const char* name, int def) {
    for (int i = 2; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return std::max(1, std::atoi(argv[i + 1]));
    return def;
}

int fail_init(Session& s) {
    std::fprintf(stderr, "radio init failed: %s\n", init_error_name(s.error));
    return 2;
}

int cmd_info() {
    Session s;
    std::printf("power acquire %s (owned=%d)\n", s.power_ok ? "OK" : "FAILED", s.power.owned());
    int rc = 0;
    if (s.power_ok) {
        const RadioInitError e = s.error;
        if (e == RadioInitError::ExpanderMissing) {
            std::printf("expander 0x43 FAILED\n");
        } else if (e == RadioInitError::PowerFailed || e == RadioInitError::SpiOpenFailed ||
                   e == RadioInitError::GpioFailed) {
            std::printf("expander 0x43 not reached\n");
        } else {
            std::printf("expander 0x43 OK\n");
        }
        std::printf("radio init %s\n", init_error_name(e));
        if (e == RadioInitError::None) {
            std::uint8_t sw[2] = {};
            if (s.radio->read_register(0x0740, sw, 2)) {
                std::printf("sync word %02x %02x%s\n", sw[0], sw[1],
                            (sw[0] == 0x14 && sw[1] == 0x24) ? "" : "  (UNEXPECTED, want 14 24)");
                if (!(sw[0] == 0x14 && sw[1] == 0x24)) rc = 3;
            } else {
                std::printf("sync word read FAILED\n");
                rc = 3;
            }
            std::printf("last_rx_rearm_us %u\n", s.radio->last_rx_rearm_us());
        } else {
            rc = 2;
        }
    } else {
        rc = 2;
        std::fprintf(stderr, "radio init failed: %s\n", init_error_name(RadioInitError::PowerFailed));
    }
    const bool was_owned = s.power.owned();
    s.close();
    std::printf("power released (owned=%d) brightness=%s\n", was_owned, read_brightness().c_str());
    return rc;
}

int cmd_ping(int count) {
    Session s;
    if (!s.ok()) return fail_init(s);
    Link link(*s.radio);
    std::vector<double> rtts, rssis;
    unsigned lost = 0;
    int attempted = 0;
    for (int seq = 0; seq < count && !g_stop; ++seq) {
        ++attempted;
        link.rxq.clear();
        unsigned busy_here = 0;
        const auto t0 = Clock::now();
        if (!link.send(make_data(kSidPing, seq_payload(static_cast<std::uint16_t>(seq))), &busy_here)) {
            std::printf("%d send_failed\n", seq);
            ++lost;
            continue;
        }
        bool got = false;
        while (!g_stop && !got &&
               Clock::now() - t0 < std::chrono::milliseconds(1500)) {
            link.pump();
            while (!link.rxq.empty()) {
                Rx rx = std::move(link.rxq.front());
                link.rxq.pop_front();
                const auto pk = decode_packet(rx.bytes.data(), rx.bytes.size());
                if (!pk || pk->type != PacketType::Data || pk->session_id != kSidPong ||
                    pk->data.size() < 2)
                    continue;
                const std::uint16_t rseq = static_cast<std::uint16_t>(pk->data[0] | (pk->data[1] << 8));
                if (rseq != seq) continue;
                const double rtt =
                    std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                std::printf("%d %.1f %d %.2f%s\n", seq, rtt, rx.rssi, rx.snr,
                            busy_here ? "  (busy-retried)" : "");
                rtts.push_back(rtt);
                rssis.push_back(rx.rssi);
                got = true;
                break;
            }
            if (!got) sleep_ms(1);
        }
        if (!got && !g_stop) {
            std::printf("%d timeout%s\n", seq, busy_here ? "  (busy-retried)" : "");
            ++lost;
        }
        if (busy_here) std::printf("  seq %d: Busy x%u before start\n", seq, busy_here);
    }
    const unsigned recv = static_cast<unsigned>(rtts.size());
    std::printf("--- ping summary ---\n");
    std::printf("sent %d received %u loss %.1f%%\n", attempted, recv,
                attempted ? 100.0 * (attempted - recv) / attempted : 0.0);
    std::printf("rtt_ms min %.1f median %.1f max %.1f\n", vmin(rtts), median(rtts), vmax(rtts));
    std::printf("rssi_dbm median %.1f\n", median(rssis));
    std::printf("busy_count %u\n", link.busy);
    std::printf("last_tx_start_us median %.0f max %.0f\n", median(link.tx_start_us), vmax(link.tx_start_us));
    std::printf("last_rx_rearm_us median %.0f max %.0f\n", median(link.rx_rearm_us), vmax(link.rx_rearm_us));
    return 0;
}

int cmd_pong() {
    Session s;
    if (!s.ok()) return fail_init(s);
    Link link(*s.radio);
    std::printf("pong: waiting for pings (Ctrl-C to stop)\n");
    while (!g_stop) {
        link.pump();
        while (!link.rxq.empty() && !g_stop) {
            Rx rx = std::move(link.rxq.front());
            link.rxq.pop_front();
            const auto pk = decode_packet(rx.bytes.data(), rx.bytes.size());
            if (!pk || pk->type != PacketType::Data || pk->session_id != kSidPing) continue;
            const std::uint16_t seq =
                pk->data.size() >= 2 ? static_cast<std::uint16_t>(pk->data[0] | (pk->data[1] << 8)) : 0;
            const bool ok = link.send(make_data(kSidPong, pk->data));
            std::printf("seq %u rssi %d snr %.2f reply %s\n", seq, rx.rssi, rx.snr, ok ? "sent" : "FAILED");
            std::fflush(stdout);
        }
        sleep_ms(1);
    }
    std::printf("pong: stopped (replies %u, busy %u)\n", link.sent, link.busy);
    return 0;
}

int cmd_listen(int seconds) {
    Session s;
    if (!s.ok()) return fail_init(s);
    Link link(*s.radio);
    const std::uint64_t end = now_ms() + static_cast<std::uint64_t>(seconds) * 1000U;
    unsigned n = 0;
    while (!g_stop && now_ms() < end) {
        link.pump();
        while (!link.rxq.empty()) {
            const Rx rx = std::move(link.rxq.front());
            link.rxq.pop_front();
            ++n;
            std::printf("[%llu ms] %zu bytes rssi %d snr %.2f\n  hex:", (unsigned long long)rx.t_ms,
                        rx.bytes.size(), rx.rssi, rx.snr);
            for (auto b : rx.bytes) std::printf(" %02x", b);
            std::printf("\n  asc: ");
            for (auto b : rx.bytes) std::putchar((b >= 0x20 && b < 0x7f) ? b : '.');
            std::printf("\n");
            std::fflush(stdout);
        }
        sleep_ms(1);
    }
    std::printf("listen: %u packets\n", n);
    return 0;
}

int cmd_blast(int seconds) {
    Session s;
    if (!s.ok()) return fail_init(s);
    Link link(*s.radio);
    const std::uint64_t end = now_ms() + static_cast<std::uint64_t>(seconds) * 1000U;
    const std::vector<std::uint8_t> payload(kMaxDataLen, 0xA5);
    unsigned n = 0, fails = 0;
    while (!g_stop && now_ms() < end) {
        std::vector<std::uint8_t> pl = payload;
        pl[0] = static_cast<std::uint8_t>(n & 0xFF);
        unsigned busy_here = 0;
        const std::uint64_t t = now_ms();
        const bool ok = link.send(make_data(kSidBlast, pl), &busy_here);
        std::printf("blast %u t=%llu ms %s busy=%u\n", n, (unsigned long long)t,
                    ok ? "started" : "FAILED", busy_here);
        std::fflush(stdout);
        if (ok) ++n; else if (++fails > 5) break;
    }
    std::printf("blast: sent %u, busy total %u, failures %u\n", link.sent, link.busy, fails);
    return fails > 5 ? 1 : 0;
}

void usage() {
    std::fprintf(stderr,
                 "usage: battleship_lora_probe info | ping [--count N] | pong | "
                 "listen [--seconds S] | blast [--seconds S]\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 64;
    }
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    const std::string cmd = argv[1];
    if (cmd == "info") return cmd_info();
    if (cmd == "ping") return cmd_ping(arg_int(argc, argv, "--count", 20));
    if (cmd == "pong") return cmd_pong();
    if (cmd == "listen") return cmd_listen(arg_int(argc, argv, "--seconds", 30));
    if (cmd == "blast") return cmd_blast(arg_int(argc, argv, "--seconds", 10));
    usage();
    return 64;
}
