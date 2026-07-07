/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * PlayerView: single-sided match state for one player, suitable for
 * networked play where the opponent's real Board never exists locally (see
 * PLAN.md 2.2 -- shots/results are exchanged over the wire, never board
 * contents). Progresses purely from the local player's own Board plus the
 * SHOT/RESULT primitives; it never needs to see an opponent Board object.
 *
 * This mirrors exactly what Game does internally for one side, so driving
 * two PlayerView instances by shuttling ShotResult values between them
 * produces identical state to a single Game instance (see the
 * PlayerViewMatchesGame test).
 */
#pragma once

#include "core/board.h"
#include "core/snapshot.h"
#include "core/tracking_board.h"

namespace battleship::core {

/// Single-sided match state driven by local shot outcomes only. Use this for
/// remote/networked play: each peer runs its own PlayerView, applies
/// `apply_incoming_shot` when the wire delivers an opponent SHOT against the
/// local Board, and applies `apply_own_shot_result` when the wire delivers a
/// RESULT for a shot the local player fired.
class PlayerView {
public:
    PlayerView() = default;

    /// This player's own fleet board. Place ships here during setup.
    [[nodiscard]] Board& own_board() { return own_board_; }
    [[nodiscard]] const Board& own_board() const { return own_board_; }

    /// This player's tracking board (record of shots fired at the opponent).
    [[nodiscard]] const TrackingBoard& tracking_board() const { return tracking_board_; }

    /// Marks setup complete and, if provided, records whether this player
    /// goes first. Requires own_board() to be fully placed.
    bool start(bool goes_first);

    [[nodiscard]] MatchStatus status() const { return status_; }
    /// True when it is currently this player's turn to fire (only meaningful
    /// while status() == InProgress).
    [[nodiscard]] bool is_my_turn() const { return is_my_turn_; }
    [[nodiscard]] bool i_won() const { return i_won_; }

    /// Applies an opponent's shot against this player's own board (called
    /// when the transport delivers an incoming SHOT(x,y) message). Returns
    /// the ShotResult to send back to the opponent as RESULT. Flips turn to
    /// the opponent unless this shot ends the game.
    ShotResult apply_incoming_shot(Coord target);

    /// Records the outcome of a shot THIS player fired at the opponent
    /// (called when the transport delivers the RESULT for a SHOT this player
    /// sent). `target` must be the same coordinate that was fired at. Flips
    /// turn back to the opponent unless this shot ends the game. Note the
    /// caller (GameController/transport layer) is responsible for actually
    /// sending the SHOT message; PlayerView only tracks resulting state.
    void apply_own_shot_result(Coord target, ShotResult result);

    /// Structured snapshot of this player's own public state (own board +
    /// tracking board). For a two-sided view, combine both peers' snapshots
    /// externally (e.g. for a spectator or LLM commentary running on a host
    /// that has access to both, which is not the networked-play case).
    [[nodiscard]] PlayerSnapshot snapshot() const;

private:
    Board own_board_;
    TrackingBoard tracking_board_;
    MatchStatus status_ = MatchStatus::Placement;
    bool is_my_turn_ = false;
    bool i_won_ = false;
};

}  // namespace battleship::core
