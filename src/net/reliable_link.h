/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * ReliableLink: stop-and-wait reliability layer on top of an ITransport
 * (PLAN.md §2.2 "信頼性層をアプリ側に持つ"). One frame is in flight at a
 * time per direction; the sender waits for an ACK carrying the same seq
 * before it will send the next frame, retransmitting on timeout up to a
 * bounded number of attempts. This is redundant over TCP (which already
 * guarantees ordered, reliable delivery) but is deliberately implemented at
 * this layer anyway so the exact same code works unmodified over a future
 * lossy, low-bandwidth, half-duplex LoRa transport (PLAN.md §2.2, §7) --
 * only the ITransport underneath changes.
 *
 * Design:
 *   - Outgoing non-ACK messages get the next seq (0..255, wrapping) and are
 *     buffered until acknowledged. Only one is "in flight" at a time
 *     (stop-and-wait); callers queue further sends and they go out in order
 *     once the in-flight one is ACKed.
 *   - ACK frames themselves are never queued/retransmitted (acking an ACK
 *     would recurse forever) and carry seq 0xFF as a sentinel since their
 *     payload (AckPayload::acked_seq) already identifies what they ack.
 *   - Every received non-ACK frame is ACKed immediately (best-effort; if the
 *     ACK itself is lost, the sender's retransmit will cause a duplicate
 *     receive, which dedup below absorbs).
 *   - Duplicate receives (same seq as the last one accepted from this peer)
 *     are detected and dropped silently (still ACKed, so the sender's
 *     retransmit timer stops) -- this is what makes retransmission safe for
 *     the message layer above, which never sees a message twice.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

#include "net/frame.h"
#include "net/transport.h"

namespace battleship::net {

/// Tunables for the stop-and-wait sender.
struct ReliabilityConfig {
    uint32_t ack_timeout_ms = 400;   ///< time to wait for an ACK before retransmitting
    int max_retransmits = 5;         ///< give up (report failure) after this many retries
};

/// Result of polling the link for delivery/failure events.
struct DeliveryEvent {
    enum class Kind { Delivered, Failed } kind;
    uint8_t seq = 0;
};

class ReliableLink {
public:
    explicit ReliableLink(ITransport& transport, ReliabilityConfig config = {});

    /// Enqueues `type`/`payload` for reliable delivery. Returns the seq
    /// number assigned (useful for tests correlating DeliveryEvents).
    /// Frames are sent strictly in the order enqueued (stop-and-wait: the
    /// next one is not written to the transport until the previous is ACKed
    /// or exhausts its retransmits).
    uint8_t enqueue_send(MessageType type, std::vector<uint8_t> payload);

    /// Services the transport: writes the head-of-queue frame (or a
    /// retransmit if its timeout elapsed), reads+decodes incoming bytes,
    /// dispatches ACKs to the send queue, and ACKs incoming non-ACK frames.
    /// `now_ms` is an injectable clock (monotonic milliseconds) so tests
    /// don't depend on wall-clock timing.
    void poll(uint32_t now_ms);

    /// Drains frames received from the peer that are new (not a duplicate
    /// of the last-accepted seq from that peer) and are not ACK frames
    /// themselves (ACKs are consumed internally by poll()). Callers
    /// (NetSession) should call this after poll() to get application
    /// messages.
    std::vector<Frame> take_received();

    /// Drains delivery outcome events (successful ACK, or retransmit budget
    /// exhausted) for frames previously enqueued via enqueue_send.
    std::vector<DeliveryEvent> take_delivery_events();

    /// True once the head-of-queue frame has exhausted max_retransmits
    /// without an ACK -- a strong signal the peer is unreachable (used
    /// alongside PING timeout for disconnect detection, PLAN.md §2.2).
    [[nodiscard]] bool last_send_failed() const { return last_send_failed_; }

    /// Number of frames currently queued/in-flight for sending (diagnostic).
    [[nodiscard]] size_t pending_send_count() const { return send_queue_.size(); }

private:
    struct PendingSend {
        uint8_t seq;
        std::vector<uint8_t> encoded;
        int attempts = 0;
        uint32_t last_sent_at_ms = 0;
        bool sent_at_least_once = false;
    };

    void send_ack(uint8_t seq);

    ITransport& transport_;
    ReliabilityConfig config_;
    FrameDecoder decoder_;

    uint8_t next_seq_ = 0;
    std::deque<PendingSend> send_queue_;
    bool last_send_failed_ = false;

    std::optional<uint8_t> last_accepted_seq_;  ///< dedup state for incoming frames
    std::vector<Frame> received_queue_;
    std::vector<DeliveryEvent> delivery_events_;
};

}  // namespace battleship::net
