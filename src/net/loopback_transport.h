/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * LoopbackTransport: an in-process pair of ITransport endpoints for testing
 * (PLAN.md §2.2 "LoopbackTransport"). Two instances share a pair of byte
 * queues; bytes sent on one appear (subject to fault injection, see below) on
 * the other's next receive()/poll().
 *
 * Fault injection hooks let tests exercise the reliability layer's actual
 * job -- surviving a transport that behaves like a lossy/low-bandwidth radio
 * link rather than a reliable local socket:
 *   - packet loss: a byte chunk is dropped with probability `loss_rate`
 *   - duplication: a byte chunk is delivered twice with probability
 *     `duplicate_rate`
 *   - fragmentation: a byte chunk is split into several single-byte (or
 *     otherwise arbitrarily small) deliveries instead of arriving whole,
 *     exercising FrameDecoder's incremental parsing
 * All faults are applied per `send()` call (i.e. per chunk handed to the
 * transport, typically one encoded frame at a time) using a caller-supplied
 * seeded RNG so tests are reproducible.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <vector>

#include "net/transport.h"

namespace battleship::net {

/// Shared fault-injection configuration + seeded RNG for a LoopbackTransport
/// pair. Both endpoints of a pair reference the same FaultConfig instance
/// (one per direction, see make_loopback_pair) so tests can tune loss/dup/
/// fragmentation independently per direction if desired.
struct LoopbackFaultConfig {
    double loss_rate = 0.0;        ///< [0,1] probability a chunk is dropped entirely
    double duplicate_rate = 0.0;   ///< [0,1] probability a chunk is delivered twice
    bool fragment_bytes = false;   ///< if true, split each chunk into single-byte pieces
    uint32_t seed = 1;
};

class LoopbackTransport : public ITransport {
public:
    /// Constructs one endpoint. Not usable standalone -- use
    /// make_loopback_pair() to get two endpoints wired to each other.
    LoopbackTransport() = default;

    void poll() override;
    [[nodiscard]] TransportState state() const override { return state_; }
    using ITransport::send;  // keep the std::vector convenience overload visible
    bool send(const uint8_t* data, size_t len) override;
    size_t receive(std::vector<uint8_t>& out) override;
    void close() override;

    /// Test-only: force the state (e.g. simulate a peer disconnecting).
    void set_state_for_testing(TransportState state) { state_ = state; }

    /// Test-only: live handle to this endpoint's outgoing-direction fault
    /// config, so a test can e.g. start lossless, then raise loss_rate
    /// mid-scenario to force retransmission of a specific frame.
    [[nodiscard]] LoopbackFaultConfig& outgoing_faults() { return *outgoing_faults_; }

private:
    friend std::pair<std::unique_ptr<LoopbackTransport>, std::unique_ptr<LoopbackTransport>>
    make_loopback_pair(LoopbackFaultConfig a_to_b, LoopbackFaultConfig b_to_a);

    // The queue this endpoint reads incoming bytes from, and the fault
    // config applied to writes going the *other* direction (peer_ reads from
    // outgoing_, subject to outgoing_faults_ applied at send() time).
    std::shared_ptr<std::deque<std::vector<uint8_t>>> inbox_;
    std::shared_ptr<std::deque<std::vector<uint8_t>>> outbox_;
    std::shared_ptr<LoopbackFaultConfig> outgoing_faults_;
    std::shared_ptr<std::mt19937> rng_;

    TransportState state_ = TransportState::Connected;
};

/// Builds a connected pair of LoopbackTransport endpoints. `a_to_b` faults
/// apply to bytes sent by the first endpoint (delivered to the second);
/// `b_to_a` faults apply to the reverse direction. Both endpoints start in
/// TransportState::Connected (loopback has no real handshake).
std::pair<std::unique_ptr<LoopbackTransport>, std::unique_ptr<LoopbackTransport>>
make_loopback_pair(LoopbackFaultConfig a_to_b = {}, LoopbackFaultConfig b_to_a = {});

}  // namespace battleship::net
