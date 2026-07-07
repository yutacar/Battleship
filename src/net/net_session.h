/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * NetSession: the game-level protocol state machine for networked play
 * (PLAN.md §2.2). Combines a ReliableLink (framing + stop-and-wait
 * reliability over any ITransport) with a core::PlayerView (single-sided
 * match state) and drives the full match flow:
 *
 *   HELLO exchange (version check + random tie-breaker -> who fires first)
 *     -> READY sync (both peers finish placement)
 *       -> alternating SHOT / RESULT
 *         -> GAME_OVER
 *
 * plus periodic PING for liveness and disconnect detection (PING/receive
 * silence beyond a timeout, transport death, or retransmit exhaustion).
 *
 * LVGL-free and clock-injected (poll(now_ms)) so the whole state machine is
 * unit-testable headlessly over LoopbackTransport pairs, including with
 * loss/duplication/fragmentation fault injection (see tests/test_net_*.cpp).
 * The UI layer drives it from an LVGL timer and merely mirrors its state.
 */
#pragma once

#include <cstdint>
#include <optional>

#include "core/player_view.h"
#include "net/messages.h"
#include "net/reliable_link.h"
#include "net/transport.h"

namespace battleship::net {

/// Which side of the connection this peer is. Used only (a) as the
/// deterministic tie-break when both HELLO tie_breaker bytes are equal and
/// (b) for diagnostics; gameplay is otherwise fully symmetric. Maps to TCP
/// listen/connect today and to a designated-initiator convention on a future
/// half-duplex LoRa link.
enum class SessionRole { Host, Client };

/// High-level phase of the networked match, layered over PlayerView's own
/// MatchStatus (which only knows Placement/InProgress/Finished).
enum class SessionPhase {
    Handshake,      ///< HELLO sent, waiting for peer's HELLO
    Placement,      ///< connected; local player still placing ships
    WaitPeerReady,  ///< local READY sent; waiting for peer's READY
    Battle,         ///< match in progress (see my_turn())
    Finished,       ///< match over (see player_view().i_won())
    Disconnected,   ///< unrecoverable: peer silent/unreachable or version mismatch
};

/// Why the session ended up Disconnected (valid only in that phase).
enum class DisconnectReason {
    None,
    VersionMismatch,   ///< peer HELLO carried an incompatible protocol version
    PeerSilent,        ///< nothing received for longer than disconnect_timeout_ms
    SendFailed,        ///< retransmit budget exhausted on an outgoing frame
    TransportClosed,   ///< underlying transport reported Disconnected
};

struct SessionConfig {
    uint32_t ping_interval_ms = 2000;
    uint32_t disconnect_timeout_ms = 10000;
    ReliabilityConfig reliability{};
};

class NetSession {
public:
    /// `tie_breaker_seed` seeds the RNG that draws the HELLO tie-breaker
    /// byte -- injectable for reproducible tests.
    NetSession(ITransport& transport, SessionRole role, uint32_t tie_breaker_seed,
               SessionConfig config = {});

    /// Drives the whole stack: transport poll, reliability layer, incoming
    /// message dispatch, PING scheduling, and disconnect detection. Call
    /// frequently (e.g. every LVGL timer tick / every test step) with a
    /// monotonic millisecond clock.
    void poll(uint32_t now_ms);

    [[nodiscard]] SessionPhase phase() const { return phase_; }
    [[nodiscard]] DisconnectReason disconnect_reason() const { return disconnect_reason_; }
    [[nodiscard]] SessionRole role() const { return role_; }

    /// The single-sided match state. Place ships on
    /// player_view().own_board() while phase() == Placement, then call
    /// send_ready().
    [[nodiscard]] core::PlayerView& player_view() { return player_view_; }
    [[nodiscard]] const core::PlayerView& player_view() const { return player_view_; }

    /// True when this peer won the HELLO tie-break and fires first. Valid
    /// once phase() has advanced past Handshake.
    [[nodiscard]] bool goes_first() const { return goes_first_; }

    /// True when it is this player's turn AND no shot of ours is still
    /// awaiting its RESULT -- i.e. the UI may fire now.
    [[nodiscard]] bool can_fire() const;

    /// The coordinate of our in-flight shot (SHOT sent, RESULT not yet
    /// received), if any. UI can render a "pending" marker there.
    [[nodiscard]] std::optional<core::Coord> pending_shot() const { return pending_shot_; }

    /// Declares local placement complete: sends READY. Requires phase() ==
    /// Placement and a fully placed own board. Returns false otherwise.
    bool send_ready();

    /// Fires at `target` on the opponent's board: sends SHOT(x,y) and
    /// enters the awaiting-RESULT state. Requires can_fire() and an
    /// un-fired-upon target cell. Returns false otherwise (no state change).
    bool fire(core::Coord target);

    /// Voluntary local exit mid-match (Esc "abandon battle?" confirmation,
    /// App Submission Guide short-Esc requirement -- see PLAN.md Phase 7).
    /// Best-effort notifies the peer with an explicit GAME_OVER
    /// (winner_is_sender=0, i.e. "I quit") and immediately pumps the link
    /// once so the frame reaches transport_.send() before the caller tears
    /// the transport down, then transitions locally to Finished/Disconnected
    /// without waiting for an ACK -- the local UI must not block on network
    /// round-trip just to leave the screen. No-op if the session is already
    /// Finished or Disconnected. Safe to call from any phase (Handshake/
    /// Placement/Battle): the peer's handle_frame(GameOver) unconditionally
    /// accepts the message and moves itself to Finished so it never hangs
    /// waiting for a partner that already left.
    void abandon(uint32_t now_ms);

    /// Diagnostic counters (tests / status UI).
    [[nodiscard]] int shots_sent() const { return shots_sent_; }
    [[nodiscard]] int shots_received() const { return shots_received_; }

    /// A coordinate + resolved result pair, for UI status reporting.
    struct ShotEvent {
        core::Coord target;
        core::ShotResult result;
    };

    /// Most recent resolved outcome of a shot THIS player fired (RESULT
    /// received), and most recent opponent shot applied to our own board.
    /// UI-facing conveniences: poll change_counter() to notice updates.
    [[nodiscard]] const std::optional<ShotEvent>& last_own_shot_result() const {
        return last_own_result_event_;
    }
    [[nodiscard]] const std::optional<ShotEvent>& last_incoming_shot() const {
        return last_incoming_event_;
    }

    /// Monotonic counter bumped on every externally observable state change
    /// (phase transition, shot applied, result applied, peer ready, ...).
    /// A UI poll loop can compare this against its last-seen value instead
    /// of diffing individual fields.
    [[nodiscard]] uint32_t change_counter() const { return change_counter_; }

private:
    void handle_frame(const Frame& frame, uint32_t now_ms);
    void enter_disconnected(DisconnectReason reason);
    void maybe_start_battle();

    ITransport& transport_;
    ReliableLink link_;
    SessionRole role_;
    SessionConfig config_;

    core::PlayerView player_view_;

    SessionPhase phase_ = SessionPhase::Handshake;
    DisconnectReason disconnect_reason_ = DisconnectReason::None;

    uint8_t my_tie_breaker_ = 0;
    bool hello_sent_ = false;
    std::optional<HelloPayload> peer_hello_;
    bool goes_first_ = false;

    bool local_ready_sent_ = false;
    bool peer_ready_ = false;
    bool battle_started_ = false;

    std::optional<core::Coord> pending_shot_;
    bool game_over_sent_ = false;
    std::optional<ShotEvent> last_own_result_event_;
    std::optional<ShotEvent> last_incoming_event_;
    uint32_t change_counter_ = 0;

    uint32_t last_rx_ms_ = 0;
    uint32_t last_ping_ms_ = 0;
    bool clock_initialized_ = false;

    int shots_sent_ = 0;
    int shots_received_ = 0;
};

}  // namespace battleship::net
