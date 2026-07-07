/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Stop-and-wait reliability layer tests over fault-injecting
 * LoopbackTransport pairs: delivery, retransmission on loss, duplicate
 * suppression, fragmentation tolerance, retransmit budget exhaustion, and a
 * randomized loss+dup+fragmentation soak (Phase 4, PLAN.md §2.2).
 */
#include <gtest/gtest.h>

#include "net/loopback_transport.h"
#include "net/messages.h"
#include "net/reliable_link.h"

namespace net = battleship::net;

namespace {

struct LinkPair {
    std::unique_ptr<net::LoopbackTransport> transport_a;
    std::unique_ptr<net::LoopbackTransport> transport_b;
    std::unique_ptr<net::ReliableLink> link_a;
    std::unique_ptr<net::ReliableLink> link_b;
};

LinkPair make_link_pair(net::LoopbackFaultConfig a_to_b = {}, net::LoopbackFaultConfig b_to_a = {},
                        net::ReliabilityConfig reliability = {}) {
    LinkPair pair;
    auto transports = net::make_loopback_pair(a_to_b, b_to_a);
    pair.transport_a = std::move(transports.first);
    pair.transport_b = std::move(transports.second);
    pair.link_a = std::make_unique<net::ReliableLink>(*pair.transport_a, reliability);
    pair.link_b = std::make_unique<net::ReliableLink>(*pair.transport_b, reliability);
    return pair;
}

}  // namespace

TEST(NetReliableLink, DeliversMessageAndReportsDelivery) {
    auto pair = make_link_pair();
    const uint8_t seq = pair.link_a->enqueue_send(net::MessageType::Shot,
                                                  net::encode(net::ShotPayload{3, 4}));

    pair.link_a->poll(0);   // writes the frame
    pair.link_b->poll(0);   // receives it, ACKs
    pair.link_a->poll(1);   // consumes the ACK

    const auto received = pair.link_b->take_received();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].type, net::MessageType::Shot);
    EXPECT_EQ(received[0].seq, seq);

    const auto events = pair.link_a->take_delivery_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, net::DeliveryEvent::Kind::Delivered);
    EXPECT_EQ(events[0].seq, seq);
    EXPECT_EQ(pair.link_a->pending_send_count(), 0u);
}

TEST(NetReliableLink, RetransmitsAfterLoss) {
    net::ReliabilityConfig reliability;
    reliability.ack_timeout_ms = 100;
    auto pair = make_link_pair({}, {}, reliability);

    // Lose the first transmission, then heal the link.
    pair.transport_a->outgoing_faults().loss_rate = 1.0;
    pair.link_a->enqueue_send(net::MessageType::Ready, {});
    pair.link_a->poll(0);   // first send -- dropped
    pair.link_b->poll(0);
    EXPECT_TRUE(pair.link_b->take_received().empty());

    pair.transport_a->outgoing_faults().loss_rate = 0.0;
    pair.link_a->poll(50);  // timeout not yet elapsed -> no retransmit
    pair.link_b->poll(50);
    EXPECT_TRUE(pair.link_b->take_received().empty());

    pair.link_a->poll(100);  // timeout elapsed -> retransmit
    pair.link_b->poll(100);
    const auto received = pair.link_b->take_received();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].type, net::MessageType::Ready);
}

TEST(NetReliableLink, DuplicateFramesAreAckedButDeliveredOnce) {
    net::LoopbackFaultConfig dup;
    dup.duplicate_rate = 1.0;  // every chunk delivered twice
    auto pair = make_link_pair(dup, {});

    pair.link_a->enqueue_send(net::MessageType::Shot, net::encode(net::ShotPayload{1, 1}));
    pair.link_a->poll(0);
    pair.link_b->poll(0);

    // The receiver saw the frame twice but must surface it exactly once.
    EXPECT_EQ(pair.link_b->take_received().size(), 1u);

    // The sender still gets its delivery confirmation.
    pair.link_a->poll(1);
    const auto events = pair.link_a->take_delivery_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, net::DeliveryEvent::Kind::Delivered);
}

TEST(NetReliableLink, RetransmittedDuplicateAfterLostAckIsSuppressed) {
    net::ReliabilityConfig reliability;
    reliability.ack_timeout_ms = 100;
    auto pair = make_link_pair({}, {}, reliability);

    // Frame reaches B, but B's ACK is lost -> A retransmits -> B must ACK
    // again yet deliver only once.
    pair.transport_b->outgoing_faults().loss_rate = 1.0;
    pair.link_a->enqueue_send(net::MessageType::Shot, net::encode(net::ShotPayload{2, 2}));
    pair.link_a->poll(0);
    pair.link_b->poll(0);  // receives, ACK lost
    EXPECT_EQ(pair.link_b->take_received().size(), 1u);

    pair.transport_b->outgoing_faults().loss_rate = 0.0;
    pair.link_a->poll(100);  // retransmit
    pair.link_b->poll(100);  // duplicate seq -> suppressed, but ACKed again
    EXPECT_TRUE(pair.link_b->take_received().empty());

    pair.link_a->poll(101);
    const auto events = pair.link_a->take_delivery_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, net::DeliveryEvent::Kind::Delivered);
}

TEST(NetReliableLink, SurvivesByteWiseFragmentation) {
    net::LoopbackFaultConfig frag;
    frag.fragment_bytes = true;
    auto pair = make_link_pair(frag, frag);

    pair.link_a->enqueue_send(net::MessageType::Result,
                              net::encode(net::ResultPayload{}));
    pair.link_a->poll(0);
    pair.link_b->poll(0);
    ASSERT_EQ(pair.link_b->take_received().size(), 1u);

    pair.link_a->poll(1);  // ACK also arrives fragmented
    const auto events = pair.link_a->take_delivery_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, net::DeliveryEvent::Kind::Delivered);
}

TEST(NetReliableLink, GivesUpAfterRetransmitBudget) {
    net::ReliabilityConfig reliability;
    reliability.ack_timeout_ms = 100;
    reliability.max_retransmits = 3;
    net::LoopbackFaultConfig black_hole;
    black_hole.loss_rate = 1.0;
    auto pair = make_link_pair(black_hole, {}, reliability);

    const uint8_t seq = pair.link_a->enqueue_send(net::MessageType::Ping, {});
    uint32_t now = 0;
    for (int i = 0; i < 10; ++i) {
        pair.link_a->poll(now);
        now += 100;
    }

    EXPECT_TRUE(pair.link_a->last_send_failed());
    const auto events = pair.link_a->take_delivery_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, net::DeliveryEvent::Kind::Failed);
    EXPECT_EQ(events[0].seq, seq);
    EXPECT_EQ(pair.link_a->pending_send_count(), 0u);
}

TEST(NetReliableLink, QueuedMessagesGoOutInOrderOneAtATime) {
    auto pair = make_link_pair();
    for (uint8_t i = 0; i < 5; ++i) {
        pair.link_a->enqueue_send(net::MessageType::Shot, net::encode(net::ShotPayload{i, i}));
    }
    EXPECT_EQ(pair.link_a->pending_send_count(), 5u);

    // Stop-and-wait: each poll round trips exactly one frame.
    uint32_t now = 0;
    std::vector<net::Frame> all;
    for (int round = 0; round < 5; ++round) {
        pair.link_a->poll(now);
        pair.link_b->poll(now);
        const auto got = pair.link_b->take_received();
        EXPECT_EQ(got.size(), 1u) << "round " << round;
        all.insert(all.end(), got.begin(), got.end());
        pair.link_a->poll(now + 1);  // consume ACK, unblock next frame
        now += 10;
    }

    ASSERT_EQ(all.size(), 5u);
    for (uint8_t i = 0; i < 5; ++i) {
        const auto shot = net::decode_shot(all[i].payload);
        ASSERT_TRUE(shot.has_value());
        EXPECT_EQ(shot->x, i);
    }
}

TEST(NetReliableLink, LossyDupFragmentedSoakDeliversEverythingInOrderOnce) {
    // The whole point of the reliability layer (PLAN.md §2.2): over a LoRa-
    // grade link that loses, duplicates, and fragments chunks, every message
    // still arrives exactly once, in order.
    net::ReliabilityConfig reliability;
    reliability.ack_timeout_ms = 50;
    reliability.max_retransmits = 100;  // soak test: never give up

    for (uint32_t seed = 1; seed <= 5; ++seed) {
        net::LoopbackFaultConfig faults;
        faults.loss_rate = 0.3;
        faults.duplicate_rate = 0.2;
        faults.fragment_bytes = true;
        faults.seed = seed;
        net::LoopbackFaultConfig faults_back = faults;
        faults_back.seed = seed + 1000;

        auto pair = make_link_pair(faults, faults_back, reliability);

        constexpr int kMessageCount = 40;
        for (int i = 0; i < kMessageCount; ++i) {
            pair.link_a->enqueue_send(net::MessageType::Shot,
                                      net::encode(net::ShotPayload{static_cast<uint8_t>(i % 10),
                                                                   static_cast<uint8_t>(i / 10)}));
        }

        std::vector<net::Frame> received;
        uint32_t now = 0;
        while (received.size() < kMessageCount && now < 1000 * 1000) {
            pair.link_a->poll(now);
            pair.link_b->poll(now);
            const auto got = pair.link_b->take_received();
            received.insert(received.end(), got.begin(), got.end());
            now += 25;
        }

        ASSERT_EQ(received.size(), static_cast<size_t>(kMessageCount)) << "seed " << seed;
        EXPECT_FALSE(pair.link_a->last_send_failed()) << "seed " << seed;
        for (int i = 0; i < kMessageCount; ++i) {
            const auto shot = net::decode_shot(received[static_cast<size_t>(i)].payload);
            ASSERT_TRUE(shot.has_value());
            EXPECT_EQ(shot->x, i % 10) << "seed " << seed << " msg " << i;
            EXPECT_EQ(shot->y, i / 10) << "seed " << seed << " msg " << i;
        }
    }
}
