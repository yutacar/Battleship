/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * AiPlayer: classic probability-density Battleship AI (PLAN.md §2.3). Pure
 * C++, zero LVGL/platform dependency -- only ever sees a TrackingBoard (its
 * own record of shots fired at the opponent), the same information a human
 * player has via the Battle screen's left-hand board. It never touches the
 * opponent's real Board.
 *
 * Algorithm summary:
 *   - Hunt mode (no unresolved hit streak): for every remaining ship type,
 *     try placing it at every origin/orientation on the board; each cell of
 *     each placement that would not contradict already-known Miss/Sunk cells
 *     gets +1 "density" score. The AI fires at the highest-scoring
 *     not-yet-shot cell (random tie-break via the seeded RNG).
 *   - Target mode (there is at least one Hit cell not yet part of a fully
 *     Sunk ship): restrict candidates to cells adjacent to (single hit) or
 *     colinear with (2+ aligned hits) the unresolved hit streak, still
 *     scored by the same density function so multiple candidate directions
 *     are ranked sensibly.
 *   - Difficulty tunes two knobs: how often the AI ignores the computed best
 *     cell and fires uniformly at random instead (except it still always
 *     pursues Target mode once it has landed a hit -- even a weak AI does
 *     not "forget" a wounded ship), and whether Hunt mode restricts itself to
 *     a parity/checkerboard subset of cells sized to the smallest remaining
 *     ship (Hard only).
 *
 * Deterministic given a seed: the same (Difficulty, seed, TrackingBoard
 * history) always produces the same next shot, so AI vs AI statistical tests
 * are reproducible.
 */
#pragma once

#include <random>
#include <vector>

#include "core/tracking_board.h"
#include "core/types.h"

namespace battleship::core::ai {

/// Three difficulty tiers, see file header and NOTES.md Phase 3a for the
/// measured strength differences between them.
enum class Difficulty {
    Easy,
    Normal,
    Hard,
};

/// Stateless-ish probability-density AI opponent. Holds only an RNG (for
/// randomized tie-breaks / difficulty-driven randomness); all game knowledge
/// is passed in per-call via TrackingBoard, so one AiPlayer instance can
/// safely be reused across many matches (e.g. in statistical tests) simply
/// by feeding it a fresh TrackingBoard each time.
class AiPlayer {
public:
    AiPlayer(Difficulty difficulty, std::uint64_t seed);

    Difficulty difficulty() const { return difficulty_; }

    /// Computes the next cell to fire at, given `tracking` (this AI's record
    /// of shots already fired at the opponent). `remaining_ship_types` must
    /// list the opponent ship types not yet confirmed sunk (i.e.
    /// tracking.remaining_ship_types()); passed explicitly so callers that
    /// already have it don't recompute, and so tests can exercise partial
    /// fleets directly.
    ///
    /// Precondition: at least one cell of `tracking` is still Unknown (i.e.
    /// the match isn't already fully resolved for this board). Behavior is
    /// undefined (may assert / return an out-of-bounds-adjacent guess) if
    /// called with a fully-shot board.
    [[nodiscard]] Coord next_shot(const TrackingBoard& tracking,
                                   const std::vector<ShipType>& remaining_ship_types);

private:
    Difficulty difficulty_;
    std::mt19937_64 rng_;
};

}  // namespace battleship::core::ai
