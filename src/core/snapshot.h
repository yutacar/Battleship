/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Structured, serialization-friendly snapshots of match state. Consumed by
 * the (future) LLM commentator and the result screen -- both need "what does
 * the board look like right now" without reaching into Board/TrackingBoard
 * internals or coupling to LVGL.
 */
#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/tracking_board.h"
#include "core/types.h"

namespace battleship::core {

/// Public per-cell view of a player's OWN fleet board, safe to reveal to that
/// player (and, post-game, to spectators/results). `has_ship` is only
/// meaningful to reveal for the owning player or after the match ends.
struct OwnBoardCellView {
    bool has_ship = false;
    bool was_hit = false;
};

/// A flattened kBoardSize x kBoardSize view, row-major (index = y * kBoardSize + x).
using OwnBoardView = std::array<OwnBoardCellView, kBoardSize * kBoardSize>;
using TrackingBoardView = std::array<CellMark, kBoardSize * kBoardSize>;

/// Everything one player publicly knows/owns at a point in time: their own
/// fleet's state and their tracking board of shots fired at the opponent.
struct PlayerSnapshot {
    OwnBoardView own_board{};
    TrackingBoardView tracking_board{};
    std::vector<ShipType> own_ships_remaining;   ///< own ships still afloat
    std::vector<ShipType> enemy_ships_sunk;      ///< confirmed sunk on tracking board
    int shots_fired = 0;
    int hits_recorded = 0;
};

/// Whose turn it is / who has won, shared vocabulary between Game and the
/// single-sided PlayerView state machine.
enum class MatchStatus {
    Placement,   ///< Fleets are still being placed.
    InProgress,  ///< Game is being played, see `MatchSnapshot::current_turn`.
    Finished,    ///< Game is over, see `MatchSnapshot::winner`.
};

/// Identifies a side in a 2-player match. Neutral naming (not "host/client"
/// or "human/AI") so it fits hotseat, AI, and networked play alike.
enum class PlayerId { One, Two };

constexpr PlayerId other_player(PlayerId p) {
    return p == PlayerId::One ? PlayerId::Two : PlayerId::One;
}

/// Full match snapshot: both players' public state plus turn/outcome
/// metadata. This is the input to Phase 3's LLM commentary prompt and to the
/// result screen.
struct MatchSnapshot {
    MatchStatus status = MatchStatus::Placement;
    PlayerId current_turn = PlayerId::One;
    /// Valid only when status == Finished.
    PlayerId winner = PlayerId::One;
    int turn_number = 0;  ///< number of shots taken so far across both players

    PlayerSnapshot player_one;
    PlayerSnapshot player_two;
};

}  // namespace battleship::core
