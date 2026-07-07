/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "net/reliable_link.h"

#include "net/messages.h"

namespace battleship::net {

namespace {
/// Seq value stamped on ACK frames themselves. ACKs are never queued for
/// reliable (re)delivery -- acking an ACK would recurse -- so this sentinel
/// need not participate in the 0..255 sequence space used for dedup; the
/// AckPayload::acked_seq field is what actually identifies which send this
/// ACK is for.
constexpr uint8_t kAckFrameSeq = 0xFF;
}  // namespace

ReliableLink::ReliableLink(ITransport& transport, ReliabilityConfig config)
    : transport_(transport), config_(config) {}

uint8_t ReliableLink::enqueue_send(MessageType type, std::vector<uint8_t> payload) {
    const uint8_t seq = next_seq_++;
    Frame frame;
    frame.version = kProtocolVersion;
    frame.type = type;
    frame.seq = seq;
    frame.payload = std::move(payload);
    send_queue_.push_back(PendingSend{seq, encode_frame(frame), 0, 0, false});
    return seq;
}

void ReliableLink::send_ack(uint8_t seq) {
    Frame frame;
    frame.version = kProtocolVersion;
    frame.type = MessageType::Ack;
    frame.seq = kAckFrameSeq;
    frame.payload = encode(AckPayload{seq});
    auto encoded = encode_frame(frame);
    transport_.send(encoded);
}

void ReliableLink::poll(uint32_t now_ms) {
    transport_.poll();

    // ---- Outgoing: (re)transmit the head-of-queue frame if due. ----
    if (!send_queue_.empty()) {
        auto& head = send_queue_.front();
        const bool due = !head.sent_at_least_once ||
                          (now_ms - head.last_sent_at_ms >= config_.ack_timeout_ms);
        if (due) {
            if (head.attempts >= config_.max_retransmits) {
                last_send_failed_ = true;
                delivery_events_.push_back({DeliveryEvent::Kind::Failed, head.seq});
                send_queue_.pop_front();
            } else {
                transport_.send(head.encoded);
                head.attempts += 1;
                head.sent_at_least_once = true;
                head.last_sent_at_ms = now_ms;
            }
        }
    }

    // ---- Incoming: decode whatever arrived and dispatch. ----
    std::vector<uint8_t> raw;
    transport_.receive(raw);
    if (!raw.empty()) {
        decoder_.feed(raw);
    }

    while (auto frame = decoder_.try_extract()) {
        if (frame->type == MessageType::Ack) {
            const auto ack = decode_ack(frame->payload);
            if (ack && !send_queue_.empty() && send_queue_.front().seq == ack->acked_seq) {
                delivery_events_.push_back({DeliveryEvent::Kind::Delivered, ack->acked_seq});
                send_queue_.pop_front();
                last_send_failed_ = false;
            }
            // ACKs for a seq that isn't currently head-of-queue are stale
            // (duplicate ACK for an already-retired send, or a very late
            // arrival) and are dropped silently.
            continue;
        }

        // Every non-ACK frame gets ACKed immediately, even duplicates --
        // the peer's retransmit means our first ACK was lost, so we must
        // ACK again to stop further retransmits.
        send_ack(frame->seq);

        const bool is_duplicate = last_accepted_seq_.has_value() && *last_accepted_seq_ == frame->seq;
        if (!is_duplicate) {
            last_accepted_seq_ = frame->seq;
            received_queue_.push_back(*frame);
        }
    }
}

std::vector<Frame> ReliableLink::take_received() {
    std::vector<Frame> out;
    out.swap(received_queue_);
    return out;
}

std::vector<DeliveryEvent> ReliableLink::take_delivery_events() {
    std::vector<DeliveryEvent> out;
    out.swap(delivery_events_);
    return out;
}

}  // namespace battleship::net
