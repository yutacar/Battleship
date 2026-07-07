/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "net/net_session.h"

#include <random>

namespace battleship::net {

using core::Coord;
using core::ShotResult;

NetSession::NetSession(ITransport& transport, SessionRole role, uint32_t tie_breaker_seed,
                       SessionConfig config)
    : transport_(transport),
      link_(transport, config.reliability),
      role_(role),
      config_(config) {
    std::mt19937 rng(tie_breaker_seed);
    my_tie_breaker_ = static_cast<uint8_t>(rng() & 0xFF);
}

bool NetSession::can_fire() const {
    return phase_ == SessionPhase::Battle && player_view_.is_my_turn() && !pending_shot_.has_value();
}

bool NetSession::send_ready() {
    if (phase_ != SessionPhase::Placement) return false;
    if (!player_view_.own_board().is_fully_placed()) return false;

    link_.enqueue_send(MessageType::Ready, encode(ReadyPayload{}));
    local_ready_sent_ = true;
    phase_ = SessionPhase::WaitPeerReady;
    ++change_counter_;
    maybe_start_battle();
    return true;
}

bool NetSession::fire(Coord target) {
    if (!can_fire()) return false;
    if (!target.in_bounds()) return false;
    if (player_view_.tracking_board().was_shot_at(target)) return false;

    ShotPayload payload;
    payload.x = static_cast<uint8_t>(target.x);
    payload.y = static_cast<uint8_t>(target.y);
    link_.enqueue_send(MessageType::Shot, encode(payload));
    pending_shot_ = target;
    ++shots_sent_;
    ++change_counter_;
    return true;
}

void NetSession::abandon(uint32_t now_ms) {
    if (phase_ == SessionPhase::Finished || phase_ == SessionPhase::Disconnected) {
        return;
    }
    if (!game_over_sent_) {
        link_.enqueue_send(MessageType::GameOver, encode(GameOverPayload{0}));
        game_over_sent_ = true;
        // Push it onto the wire now (best-effort -- no ACK wait): poll()
        // transmits the new head-of-queue frame immediately since it has
        // never been sent before (see ReliableLink::poll's `due` check),
        // and it also pumps transport_.poll() as its first step.
        link_.poll(now_ms);
    }
    phase_ = SessionPhase::Finished;
    ++change_counter_;
}

void NetSession::poll(uint32_t now_ms) {
    if (phase_ == SessionPhase::Disconnected) return;

    if (!clock_initialized_) {
        clock_initialized_ = true;
        last_rx_ms_ = now_ms;
        last_ping_ms_ = now_ms;
    }

    transport_.poll();

    if (transport_.state() == TransportState::Disconnected) {
        enter_disconnected(DisconnectReason::TransportClosed);
        return;
    }
    if (transport_.state() == TransportState::Connecting) {
        // Still waiting for the underlying transport (TCP accept/connect in
        // progress). Suppress the liveness timeout until bytes can actually
        // flow; the UI has its own cancel affordance for this stage.
        last_rx_ms_ = now_ms;
        return;
    }

    // Transport is Connected: kick off HELLO exactly once.
    if (!hello_sent_) {
        HelloPayload hello;
        hello.protocol_version = kProtocolVersion;
        hello.tie_breaker = my_tie_breaker_;
        link_.enqueue_send(MessageType::Hello, encode(hello));
        hello_sent_ = true;
    }

    link_.poll(now_ms);

    for (const auto& event : link_.take_delivery_events()) {
        if (event.kind == DeliveryEvent::Kind::Failed) {
            enter_disconnected(DisconnectReason::SendFailed);
            return;
        }
        // A Delivered event means a fresh ACK arrived from the peer -- that
        // counts as liveness even if no application frame came with it.
        last_rx_ms_ = now_ms;
    }

    for (const auto& frame : link_.take_received()) {
        last_rx_ms_ = now_ms;
        handle_frame(frame, now_ms);
        if (phase_ == SessionPhase::Disconnected) return;
    }

    // Periodic PING for liveness, but only when the reliable-send pipe is
    // idle: queueing PINGs behind an in-flight SHOT/RESULT would only delay
    // the messages that actually matter on a slow (LoRa) link, and any
    // in-flight frame's ACK already proves the peer is alive.
    if (phase_ != SessionPhase::Finished && link_.pending_send_count() == 0 &&
        now_ms - last_ping_ms_ >= config_.ping_interval_ms) {
        link_.enqueue_send(MessageType::Ping, encode(PingPayload{}));
        last_ping_ms_ = now_ms;
    }

    if (phase_ != SessionPhase::Finished && now_ms - last_rx_ms_ >= config_.disconnect_timeout_ms) {
        enter_disconnected(DisconnectReason::PeerSilent);
    }
}

void NetSession::handle_frame(const Frame& frame, uint32_t) {
    switch (frame.type) {
        case MessageType::Hello: {
            const auto hello = decode_hello(frame.payload);
            if (!hello) return;
            if (hello->protocol_version != kProtocolVersion) {
                enter_disconnected(DisconnectReason::VersionMismatch);
                return;
            }
            if (phase_ != SessionPhase::Handshake) return;  // late duplicate; ignore
            peer_hello_ = *hello;
            // Higher tie-breaker fires first; on the (1/256) exact tie the
            // Host side wins deterministically -- both peers apply the same
            // rule to the same two bytes, so they always agree.
            if (my_tie_breaker_ != hello->tie_breaker) {
                goes_first_ = my_tie_breaker_ > hello->tie_breaker;
            } else {
                goes_first_ = (role_ == SessionRole::Host);
            }
            phase_ = SessionPhase::Placement;
            ++change_counter_;
            return;
        }
        case MessageType::Ready: {
            peer_ready_ = true;
            ++change_counter_;
            maybe_start_battle();
            return;
        }
        case MessageType::Shot: {
            if (phase_ != SessionPhase::Battle) return;
            const auto shot = decode_shot(frame.payload);
            if (!shot) return;
            const Coord target{shot->x, shot->y};
            const ShotResult result = player_view_.apply_incoming_shot(target);
            ++shots_received_;
            if (result.is_valid_input()) {
                last_incoming_event_ = ShotEvent{target, result};
            }
            ++change_counter_;

            ResultPayload reply;
            reply.x = shot->x;
            reply.y = shot->y;
            reply.outcome = result.outcome;
            reply.sunk_ship = result.sunk_ship;
            reply.game_won = result.game_won ? 1 : 0;
            link_.enqueue_send(MessageType::Result, encode(reply));

            if (result.game_won) {
                phase_ = SessionPhase::Finished;  // we lost
            }
            return;
        }
        case MessageType::Result: {
            if (!pending_shot_) return;  // unsolicited RESULT; drop
            const auto result = decode_result(frame.payload);
            if (!result) return;
            const Coord target{result->x, result->y};
            if (target != *pending_shot_) return;  // RESULT for some other shot; drop

            ShotResult applied;
            applied.outcome = result->outcome;
            applied.sunk_ship = result->sunk_ship;
            applied.game_won = result->game_won != 0;
            pending_shot_.reset();
            ++change_counter_;

            if (!applied.is_valid_input()) {
                // The peer judged our shot invalid (should not happen with a
                // well-behaved peer thanks to local was_shot_at gating +
                // dedup, but a malformed peer must not deadlock us): clear
                // the pending state so the player may fire again.
                return;
            }
            player_view_.apply_own_shot_result(target, applied);
            last_own_result_event_ = ShotEvent{target, applied};

            if (applied.game_won) {
                phase_ = SessionPhase::Finished;  // we won
                if (!game_over_sent_) {
                    link_.enqueue_send(MessageType::GameOver, encode(GameOverPayload{1}));
                    game_over_sent_ = true;
                }
            }
            return;
        }
        case MessageType::GameOver: {
            // Belt-and-braces convergence signal from the winner. Normally
            // we already reached Finished via the winning SHOT's RESULT
            // round-trip; accept it regardless so a peer that thinks the
            // match ended can never leave us stuck in Battle.
            phase_ = SessionPhase::Finished;
            return;
        }
        case MessageType::Ping:
            return;  // liveness only; the reliability layer already ACKed it
        case MessageType::Ack:
            return;  // consumed inside ReliableLink; never reaches here
    }
}

void NetSession::maybe_start_battle() {
    if (battle_started_) return;
    if (!local_ready_sent_ || !peer_ready_) return;
    if (!player_view_.start(goes_first_)) return;
    battle_started_ = true;
    phase_ = SessionPhase::Battle;
    ++change_counter_;
}

void NetSession::enter_disconnected(DisconnectReason reason) {
    phase_ = SessionPhase::Disconnected;
    disconnect_reason_ = reason;
    ++change_counter_;
}

}  // namespace battleship::net
