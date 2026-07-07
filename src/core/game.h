/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Game: the full 2-player match state machine. Holds both players' Board and
 * TrackingBoard instances and drives turn order + win detection. Suited for
 * hotseat play and any mode where both boards live in the same process (e.g.
 * AI self-play, local vs AI). For networked play, where the opponent's real
 * Board never exists locally, see PlayerView in player_view.h instead --
 * Game is implemented in terms of the same SHOT -> ShotResult primitives so
 * both stay in lockstep.
 */
#pragma once

#include "core/board.h"
#include "core/snapshot.h"
#include "core/tracking_board.h"

namespace battleship::core {

/// Two-player match state machine: owns both players' Board + TrackingBoard,
/// enforces turn order, and reports win/loss. Neutral with respect to who or
/// what controls each player (human, AI, or a network proxy feeding shots
/// from a remote peer) -- callers decide that outside this class.
class Game {
public:
    Game() = default;

    /// Access to a player's own fleet board for placement (before the match
    /// starts) or for reveal purposes after it ends. Placement should be
    /// done via these boards directly (place_ship / place_fleet_randomly)
    /// while status() == MatchStatus::Placement.
    [[nodiscard]] Board& board(PlayerId player);
    [[nodiscard]] const Board& board(PlayerId player) const;

    /// Read-only access to a player's tracking board (their record of shots
    /// fired at the opponent).
    [[nodiscard]] const TrackingBoard& tracking_board(PlayerId player) const;

    /// Transitions from Placement to InProgress once both boards are fully
    /// placed (one ship of each type). `first_player` sets who shoots first.
    /// Returns false (no state change) if either board is not fully placed,
    /// or if the match already left the Placement phase.
    bool start(PlayerId first_player);

    [[nodiscard]] MatchStatus status() const { return status_; }
    [[nodiscard]] PlayerId current_turn() const { return current_turn_; }
    [[nodiscard]] PlayerId winner() const { return winner_; }
    [[nodiscard]] int turn_number() const { return turn_number_; }

    /// Applies a shot from the player whose turn it currently is, targeting
    /// `target` on the opponent's board. Updates the shooter's tracking
    /// board, advances turn order, and detects game-over. Returns the
    /// ShotResult as seen from the opponent's Board::receive_shot; if the
    /// input is invalid (AlreadyShot/OutOfBounds) or the match is not
    /// InProgress, no state changes and the returned result reflects why
    /// (outcome will be AlreadyShot/OutOfBounds, or Miss with game_won=false
    /// as a neutral "no-op" sentinel when the match isn't in progress -- check
    /// status() first in that case).
    ShotResult shoot(PlayerId shooter, Coord target);

    /// Structured snapshot of both players' public state, suitable for
    /// result screens or (future) LLM commentary input.
    [[nodiscard]] MatchSnapshot snapshot() const;

private:
    Board boards_[2];
    TrackingBoard tracking_boards_[2];

    MatchStatus status_ = MatchStatus::Placement;
    PlayerId current_turn_ = PlayerId::One;
    PlayerId winner_ = PlayerId::One;
    int turn_number_ = 0;

    [[nodiscard]] static int index_of(PlayerId player) { return player == PlayerId::One ? 0 : 1; }
};

}  // namespace battleship::core
