/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "net/loopback_transport.h"

namespace battleship::net {

void LoopbackTransport::poll() {
    // Nothing async to progress: send() delivers directly into the peer's
    // inbox_ (see below). Kept as a real method (not defaulted away) so
    // NetSession can uniformly poll() every ITransport regardless of type.
}

bool LoopbackTransport::send(const uint8_t* data, size_t len) {
    if (state_ != TransportState::Connected) return false;
    if (!outbox_ || !outgoing_faults_ || !rng_) return false;

    std::uniform_real_distribution<double> unit(0.0, 1.0);
    if (unit(*rng_) < outgoing_faults_->loss_rate) {
        return true;  // silently dropped, as a real lossy link would do
    }

    std::vector<uint8_t> chunk(data, data + len);
    const int copies = (unit(*rng_) < outgoing_faults_->duplicate_rate) ? 2 : 1;
    for (int c = 0; c < copies; ++c) {
        if (outgoing_faults_->fragment_bytes) {
            for (uint8_t b : chunk) {
                outbox_->push_back({b});
            }
        } else {
            outbox_->push_back(chunk);
        }
    }
    return true;
}

size_t LoopbackTransport::receive(std::vector<uint8_t>& out) {
    if (!inbox_) return 0;
    size_t total = 0;
    while (!inbox_->empty()) {
        auto& chunk = inbox_->front();
        out.insert(out.end(), chunk.begin(), chunk.end());
        total += chunk.size();
        inbox_->pop_front();
    }
    return total;
}

void LoopbackTransport::close() { state_ = TransportState::Disconnected; }

std::pair<std::unique_ptr<LoopbackTransport>, std::unique_ptr<LoopbackTransport>>
make_loopback_pair(LoopbackFaultConfig a_to_b, LoopbackFaultConfig b_to_a) {
    auto a = std::make_unique<LoopbackTransport>();
    auto b = std::make_unique<LoopbackTransport>();

    auto queue_a_to_b = std::make_shared<std::deque<std::vector<uint8_t>>>();
    auto queue_b_to_a = std::make_shared<std::deque<std::vector<uint8_t>>>();

    a->outbox_ = queue_a_to_b;
    a->inbox_ = queue_b_to_a;
    a->outgoing_faults_ = std::make_shared<LoopbackFaultConfig>(a_to_b);
    a->rng_ = std::make_shared<std::mt19937>(a_to_b.seed);

    b->outbox_ = queue_b_to_a;
    b->inbox_ = queue_a_to_b;
    b->outgoing_faults_ = std::make_shared<LoopbackFaultConfig>(b_to_a);
    b->rng_ = std::make_shared<std::mt19937>(b_to_a.seed);

    a->state_ = TransportState::Connected;
    b->state_ = TransportState::Connected;

    return {std::move(a), std::move(b)};
}

}  // namespace battleship::net
