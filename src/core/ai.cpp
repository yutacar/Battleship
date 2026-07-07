/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "core/ai.h"

#include <algorithm>
#include <array>

#include "core/ship.h"

namespace battleship::core::ai {
namespace {

using Grid = std::array<std::array<int, kBoardSize>, kBoardSize>;

/// True when placing a ship of `length` at `origin`/`orientation` would not
/// contradict anything already known on `tracking`: every cell must fall in
/// bounds, must not be a confirmed Miss, and must not be a Sunk cell that
/// doesn't belong to this hypothetical placement (Sunk cells are dead ends --
/// no remaining ship can occupy them). Hit cells are fine to place over: a
/// Hit cell must belong to *some* remaining ship, so any hypothesis
/// consistent with covering it is plausible.
bool placement_is_consistent(const TrackingBoard& tracking, Coord origin, Orientation orientation,
                              int length) {
    for (int i = 0; i < length; ++i) {
        const Coord c = orientation == Orientation::Horizontal ? Coord(origin.x + i, origin.y)
                                                                 : Coord(origin.x, origin.y + i);
        if (!c.in_bounds()) return false;
        const CellMark mark = tracking.mark_at(c);
        if (mark == CellMark::Miss || mark == CellMark::Sunk) return false;
    }
    return true;
}

/// Adds +1 density to every cell of a hypothetical placement.
void accumulate_placement(Grid& density, Coord origin, Orientation orientation, int length) {
    for (int i = 0; i < length; ++i) {
        const Coord c = orientation == Orientation::Horizontal ? Coord(origin.x + i, origin.y)
                                                                 : Coord(origin.x, origin.y + i);
        ++density[c.y][c.x];
    }
}

/// Computes a probability-density heatmap: for every remaining ship type, try
/// every origin x orientation on the board and accumulate +1 per covered
/// cell for every placement consistent with `tracking`'s known Miss/Sunk
/// cells. Cells already shot at (Hit included -- firing again is invalid)
/// are left in the grid as computed but the caller is responsible for
/// excluding already-shot cells from candidate selection.
Grid compute_density(const TrackingBoard& tracking, const std::vector<ShipType>& remaining_ship_types) {
    Grid density{};
    for (ShipType type : remaining_ship_types) {
        const int length = ship_length(type);
        for (int y = 0; y < kBoardSize; ++y) {
            for (int x = 0; x < kBoardSize; ++x) {
                for (Orientation orientation : {Orientation::Horizontal, Orientation::Vertical}) {
                    if (placement_is_consistent(tracking, Coord(x, y), orientation, length)) {
                        accumulate_placement(density, Coord(x, y), orientation, length);
                    }
                }
            }
        }
    }
    return density;
}

/// True when `c` is a currently-unresolved Hit (hit but not part of a
/// confirmed-Sunk ship yet) -- i.e. a live thread the Target mode should
/// pursue.
bool is_unresolved_hit(const TrackingBoard& tracking, Coord c) {
    return c.in_bounds() && tracking.mark_at(c) == CellMark::Hit;
}

/// Collects every cell not yet shot at, matching `predicate`, ranked by
/// `density`; returns the subset achieving the maximum density value found
/// (so callers can then apply a random tie-break). If no candidate matches
/// `predicate`, returns an empty vector.
template <typename Predicate>
std::vector<Coord> best_candidates(const TrackingBoard& tracking, const Grid& density, Predicate predicate) {
    std::vector<Coord> best;
    int best_score = -1;
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            const Coord c(x, y);
            if (tracking.was_shot_at(c)) continue;
            if (!predicate(c)) continue;
            const int score = density[y][x];
            if (score > best_score) {
                best_score = score;
                best.clear();
                best.push_back(c);
            } else if (score == best_score) {
                best.push_back(c);
            }
        }
    }
    return best;
}

/// All not-yet-shot cells on the board (Hunt mode's full candidate pool).
std::vector<Coord> all_unshot_cells(const TrackingBoard& tracking) {
    std::vector<Coord> cells;
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            const Coord c(x, y);
            if (!tracking.was_shot_at(c)) cells.push_back(c);
        }
    }
    return cells;
}

/// Target-mode candidate set: cells that continue an unresolved hit streak.
/// - A single isolated unresolved hit: its 4-neighborhood (any orientation
///   still possible).
/// - Two or more colinear unresolved hits: only the two extension points
///   beyond either end of the line (the ship must be a straight run, so once
///   orientation is known from 2 hits, perpendicular neighbors are no longer
///   plausible extensions).
/// Falls back to the 4-neighborhood of every unresolved hit if the hits
/// found are not colinear (can happen with multiple ships hit in the same
/// stretch of turns before either sinks) -- still much better than blind
/// Hunt, and never worse than examining every unresolved hit's neighbors.
std::vector<Coord> target_candidates(const TrackingBoard& tracking) {
    std::vector<Coord> hits;
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            const Coord c(x, y);
            if (is_unresolved_hit(tracking, c)) hits.push_back(c);
        }
    }
    if (hits.empty()) return {};

    // Group into runs of colinear, contiguous unresolved hits, then propose
    // the extension points beyond each run's ends. This naturally handles
    // "two separate wounded ships" by producing extensions for each run.
    std::vector<Coord> candidates;
    std::vector<bool> consumed(hits.size(), false);

    for (size_t i = 0; i < hits.size(); ++i) {
        if (consumed[i]) continue;
        // Try to build a maximal horizontal run starting from hits[i].
        std::vector<Coord> run = {hits[i]};
        consumed[i] = true;

        auto try_extend = [&](int dx, int dy) {
            Coord cur = run.back();
            while (true) {
                Coord next(cur.x + dx, cur.y + dy);
                auto it = std::find(hits.begin(), hits.end(), next);
                if (it == hits.end()) break;
                size_t idx = static_cast<size_t>(it - hits.begin());
                if (consumed[idx]) break;
                consumed[idx] = true;
                run.push_back(next);
                cur = next;
            }
        };

        // First see if a horizontal neighbor exists; else vertical. A single
        // hit with no neighbors yet remains a run of length 1 (isolated).
        bool horizontal_neighbor = std::find(hits.begin(), hits.end(), Coord(hits[i].x + 1, hits[i].y)) !=
                                        hits.end() ||
                                    std::find(hits.begin(), hits.end(), Coord(hits[i].x - 1, hits[i].y)) !=
                                        hits.end();
        if (horizontal_neighbor) {
            try_extend(1, 0);
            try_extend(-1, 0);
        } else {
            try_extend(0, 1);
            try_extend(0, -1);
        }

        if (run.size() == 1) {
            // Isolated hit: propose all 4 neighbors.
            const Coord c = run.front();
            for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                Coord n(c.x + dx, c.y + dy);
                if (n.in_bounds() && !tracking.was_shot_at(n)) candidates.push_back(n);
            }
        } else {
            // Sort run to find true extremes (run may have been built out of
            // order relative to board coordinates).
            std::sort(run.begin(), run.end(), [](const Coord& a, const Coord& b) {
                return (a.x + a.y * kBoardSize) < (b.x + b.y * kBoardSize);
            });
            const bool is_horizontal = run.front().y == run.back().y;
            Coord low_ext = is_horizontal ? Coord(run.front().x - 1, run.front().y)
                                           : Coord(run.front().x, run.front().y - 1);
            Coord high_ext = is_horizontal ? Coord(run.back().x + 1, run.back().y)
                                            : Coord(run.back().x, run.back().y + 1);
            if (low_ext.in_bounds() && !tracking.was_shot_at(low_ext)) candidates.push_back(low_ext);
            if (high_ext.in_bounds() && !tracking.was_shot_at(high_ext)) candidates.push_back(high_ext);
        }
    }

    // De-duplicate (two separate runs could propose the same cell in
    // contrived layouts).
    std::sort(candidates.begin(), candidates.end(), [](const Coord& a, const Coord& b) {
        return (a.x + a.y * kBoardSize) < (b.x + b.y * kBoardSize);
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                  [](const Coord& a, const Coord& b) { return a == b; }),
                      candidates.end());
    return candidates;
}

/// Hard-only parity optimization: with the smallest remaining ship length
/// `min_len`, any ship must occupy at least one cell of every `min_len`-cell
/// stretch, so it suffices to only ever *hunt* (not target -- target mode
/// bypasses parity entirely, see next_shot) cells on a checkerboard-style
/// subset with period `min_len` -- i.e. (x + y) % min_len == 0. This is the
/// classic Battleship "parity" trick: it does not reduce information, only
/// skips guesses that a smallest-min_len ship could never occupy exclusively
/// (some cell of the ship will always land on the parity set as long as the
/// ship is >= min_len long and the parity set is applied with period
/// min_len).
bool matches_parity(Coord c, int min_len) {
    if (min_len <= 1) return true;  // no useful parity constraint possible
    return ((c.x + c.y) % min_len + min_len) % min_len == 0;
}

int min_remaining_length(const std::vector<ShipType>& remaining_ship_types) {
    int min_len = kBoardSize;  // sentinel larger than any real ship
    for (ShipType type : remaining_ship_types) {
        min_len = std::min(min_len, ship_length(type));
    }
    return min_len;
}

}  // namespace

AiPlayer::AiPlayer(Difficulty difficulty, std::uint64_t seed) : difficulty_(difficulty), rng_(seed) {}

Coord AiPlayer::next_shot(const TrackingBoard& tracking, const std::vector<ShipType>& remaining_ship_types) {
    // ---- Target mode: always pursue a wounded ship regardless of difficulty
    // (even Easy "remembers" a hit it just landed -- see file header) ----
    const std::vector<Coord> targets = target_candidates(tracking);
    if (!targets.empty()) {
        const Grid density = compute_density(tracking, remaining_ship_types);
        std::vector<Coord> best;
        int best_score = -1;
        for (const Coord& c : targets) {
            const int score = density[c.y][c.x];
            if (score > best_score) {
                best_score = score;
                best.clear();
                best.push_back(c);
            } else if (score == best_score) {
                best.push_back(c);
            }
        }
        if (best.empty()) {
            // All proposed targets already shot at somehow (shouldn't happen
            // given target_candidates() filters was_shot_at, but guard
            // anyway) -- fall through to Hunt logic below.
        } else {
            std::uniform_int_distribution<size_t> pick(0, best.size() - 1);
            return best[pick(rng_)];
        }
    }

    // ---- Hunt mode ----
    // Difficulty-driven random-shot mixing: Easy hunts entirely at random,
    // Normal mixes in some randomness, Hard always plays the computed best
    // cell (no randomness beyond tie-breaks).
    double random_shot_probability = 0.0;
    switch (difficulty_) {
        case Difficulty::Easy:
            // Pure uniform-random hunting (spec: "未射撃セルから一様ランダム。
            // ただしヒット後の追撃だけは行う" -- Target mode above still
            // pursues wounded ships, but hunting never consults the density
            // map at this tier).
            random_shot_probability = 1.0;
            break;
        case Difficulty::Normal:
            random_shot_probability = 0.35;
            break;
        case Difficulty::Hard:
            random_shot_probability = 0.0;
            break;
    }

    std::uniform_real_distribution<double> chance(0.0, 1.0);
    if (random_shot_probability > 0.0 && chance(rng_) < random_shot_probability) {
        const std::vector<Coord> all = all_unshot_cells(tracking);
        std::uniform_int_distribution<size_t> pick(0, all.size() - 1);
        return all[pick(rng_)];
    }

    const Grid density = compute_density(tracking, remaining_ship_types);

    std::vector<Coord> best;
    if (difficulty_ == Difficulty::Hard) {
        // Parity-restricted candidate pool; if (in some late-game edge case)
        // no unshot cell matches parity, fall back to the unrestricted pool.
        const int min_len = min_remaining_length(remaining_ship_types);
        best = best_candidates(tracking, density, [&](Coord c) { return matches_parity(c, min_len); });
    }
    if (best.empty()) {
        best = best_candidates(tracking, density, [](Coord) { return true; });
    }

    std::uniform_int_distribution<size_t> pick(0, best.size() - 1);
    return best[pick(rng_)];
}

}  // namespace battleship::core::ai
