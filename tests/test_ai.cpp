// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// AiPlayer tests: Target-mode pursuit behavior (adjacent hunting / straight-
// line extension), parity restriction (Hard), never re-firing an
// already-shot cell, board-consistency of chosen shots, and headless AI vs
// AI statistical self-play tests validating that difficulty tiers produce a
// measurable, significant strength gap (PLAN.md §2.3 / Phase 3a spec).

#include "core/ai.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <set>

#include "core/fleet.h"
#include "core/game.h"

using namespace battleship::core;
using battleship::core::ai::AiPlayer;
using battleship::core::ai::Difficulty;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool adjacent4(Coord a, Coord b) {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    return (dx == 1 && dy == 0) || (dx == 0 && dy == 1);
}

// Places a standard fleet, all horizontal, starting at row `row_offset`
// (mirrors the helper used in test_game.cpp).
void place_standard_fleet(Board& board, int row_offset = 0) {
    board.place_ship(Ship(ShipType::Carrier, Coord(0, row_offset + 0), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Battleship, Coord(0, row_offset + 1), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Cruiser, Coord(0, row_offset + 2), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Submarine, Coord(0, row_offset + 3), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, row_offset + 4), Orientation::Horizontal));
}

// ---------------------------------------------------------------------------
// Target-mode: adjacency after an isolated hit
// ---------------------------------------------------------------------------

TEST(Ai, TargetModeFiresAdjacentToIsolatedHit) {
    TrackingBoard tracking;
    // A single isolated hit, no misses/sinks around it yet.
    tracking.record_shot(Coord(5, 5), ShotOutcome::Hit);

    AiPlayer ai(Difficulty::Hard, /*seed=*/1);
    std::vector<ShipType> remaining(std::begin(kAllShipTypes), std::end(kAllShipTypes));

    for (int trial = 0; trial < 20; ++trial) {
        Coord shot = ai.next_shot(tracking, remaining);
        EXPECT_TRUE(adjacent4(shot, Coord(5, 5))) << "shot=(" << shot.x << "," << shot.y << ")";
    }
}

TEST(Ai, TargetModeExtendsStraightLineAfterTwoColinearHits) {
    TrackingBoard tracking;
    // Two horizontally-adjacent unresolved hits -> extension candidates must
    // be exactly (3,5) and (6,5), never a perpendicular neighbor.
    tracking.record_shot(Coord(4, 5), ShotOutcome::Hit);
    tracking.record_shot(Coord(5, 5), ShotOutcome::Hit);

    AiPlayer ai(Difficulty::Hard, /*seed=*/2);
    std::vector<ShipType> remaining(std::begin(kAllShipTypes), std::end(kAllShipTypes));

    for (int trial = 0; trial < 20; ++trial) {
        Coord shot = ai.next_shot(tracking, remaining);
        const bool is_low_ext = (shot == Coord(3, 5));
        const bool is_high_ext = (shot == Coord(6, 5));
        EXPECT_TRUE(is_low_ext || is_high_ext) << "shot=(" << shot.x << "," << shot.y << ")";
    }
}

TEST(Ai, TargetModeNeverPicksAlreadyShotCell) {
    TrackingBoard tracking;
    tracking.record_shot(Coord(5, 5), ShotOutcome::Hit);
    // Pre-shoot 3 of the 4 neighbors as Miss, leaving only (5,6) open.
    tracking.record_shot(Coord(4, 5), ShotOutcome::Miss);
    tracking.record_shot(Coord(6, 5), ShotOutcome::Miss);
    tracking.record_shot(Coord(5, 4), ShotOutcome::Miss);

    AiPlayer ai(Difficulty::Normal, /*seed=*/3);
    std::vector<ShipType> remaining(std::begin(kAllShipTypes), std::end(kAllShipTypes));

    for (int trial = 0; trial < 10; ++trial) {
        Coord shot = ai.next_shot(tracking, remaining);
        EXPECT_EQ(shot, Coord(5, 6));
    }
}

// ---------------------------------------------------------------------------
// Hunt mode: general board-consistency, never re-firing a shot cell
// ---------------------------------------------------------------------------

TEST(Ai, HuntModeNeverPicksAlreadyShotCellAcrossManySeeds) {
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        TrackingBoard tracking;
        std::mt19937 board_rng(static_cast<unsigned int>(seed));
        std::uniform_int_distribution<int> coord_dist(0, kBoardSize - 1);

        // Randomly mark ~30 cells as shot (mix of Miss/Hit) to build a varied
        // partial board state, without ever creating a full ship-sunk streak
        // that would trigger Target mode filtering differently.
        std::set<std::pair<int, int>> shot_cells;
        for (int i = 0; i < 30; ++i) {
            int x = coord_dist(board_rng);
            int y = coord_dist(board_rng);
            if (shot_cells.count({x, y})) continue;
            shot_cells.insert({x, y});
            // Keep everything a Miss so we stay in pure Hunt mode for this test.
            tracking.record_shot(Coord(x, y), ShotOutcome::Miss);
        }

        AiPlayer ai(Difficulty::Hard, seed);
        std::vector<ShipType> remaining(std::begin(kAllShipTypes), std::end(kAllShipTypes));
        Coord shot = ai.next_shot(tracking, remaining);

        EXPECT_TRUE(shot.in_bounds()) << "seed=" << seed;
        EXPECT_FALSE(tracking.was_shot_at(shot)) << "seed=" << seed << " shot=(" << shot.x << "," << shot.y << ")";
    }
}

TEST(Ai, AllDifficultiesNeverPickAlreadyShotCell) {
    for (auto difficulty : {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard}) {
        TrackingBoard tracking;
        AiPlayer ai(difficulty, /*seed=*/99);
        std::vector<ShipType> remaining(std::begin(kAllShipTypes), std::end(kAllShipTypes));

        // Repeatedly fire and record outcome as Miss (keeps us in Hunt mode)
        // until only a handful of cells remain; verify no repeats occur.
        std::set<std::pair<int, int>> shot_cells;
        for (int i = 0; i < kBoardSize * kBoardSize - 1; ++i) {
            Coord shot = ai.next_shot(tracking, remaining);
            ASSERT_TRUE(shot.in_bounds());
            auto key = std::make_pair(shot.x, shot.y);
            ASSERT_FALSE(shot_cells.count(key)) << "difficulty repeated a cell at i=" << i;
            shot_cells.insert(key);
            tracking.record_shot(shot, ShotOutcome::Miss);
        }
    }
}

// ---------------------------------------------------------------------------
// Parity (Hard difficulty): pure Hunt-mode shots (no unresolved hits) must
// respect the checkerboard-with-period-min_len constraint when a
// parity-matching unshot cell exists.
// ---------------------------------------------------------------------------

TEST(Ai, HardDifficultyHuntRespectsParityWhenAvailable) {
    // Fresh board, full fleet remaining -> min ship length is 2 (Destroyer).
    // Parity requires (x + y) % 2 == 0 for the very first shot.
    TrackingBoard tracking;
    std::vector<ShipType> remaining(std::begin(kAllShipTypes), std::end(kAllShipTypes));

    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        AiPlayer ai(Difficulty::Hard, seed);
        Coord shot = ai.next_shot(tracking, remaining);
        EXPECT_EQ((shot.x + shot.y) % 2, 0) << "seed=" << seed << " shot=(" << shot.x << "," << shot.y << ")";
    }
}

TEST(Ai, HardDifficultyParityShrinksAsSmallestShipSinks) {
    // Only a Carrier (length 5) remains -> parity period becomes 5.
    TrackingBoard tracking;
    std::vector<ShipType> remaining = {ShipType::Carrier};

    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        AiPlayer ai(Difficulty::Hard, seed);
        Coord shot = ai.next_shot(tracking, remaining);
        EXPECT_EQ((shot.x + shot.y) % 5, 0) << "seed=" << seed << " shot=(" << shot.x << "," << shot.y << ")";
    }
}

// ---------------------------------------------------------------------------
// Determinism: same seed + same history -> same next shot.
// ---------------------------------------------------------------------------

TEST(Ai, SameSeedAndHistoryProducesIdenticalNextShot) {
    TrackingBoard tracking;
    tracking.record_shot(Coord(2, 2), ShotOutcome::Miss);
    std::vector<ShipType> remaining(std::begin(kAllShipTypes), std::end(kAllShipTypes));

    AiPlayer ai1(Difficulty::Normal, 123);
    AiPlayer ai2(Difficulty::Normal, 123);

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(ai1.next_shot(tracking, remaining), ai2.next_shot(tracking, remaining));
    }
}

// ---------------------------------------------------------------------------
// Headless AI vs AI statistical self-play (Phase 3a spec).
// ---------------------------------------------------------------------------

// Simulates one complete AI vs AI match with both sides using `difficulty`,
// returns the number of shots taken (turn_number at game end). Both fleets
// are placed randomly via place_fleet_randomly using per-match seeded RNGs
// derived from `match_seed`, so results are fully reproducible.
int simulate_self_play_match(Difficulty difficulty, std::uint64_t match_seed) {
    std::mt19937 placement_rng(static_cast<unsigned int>(match_seed * 2 + 1));
    Game game;
    place_fleet_randomly(game.board(PlayerId::One), placement_rng);
    place_fleet_randomly(game.board(PlayerId::Two), placement_rng);
    game.start(PlayerId::One);

    AiPlayer ai_one(difficulty, match_seed * 10 + 1);
    AiPlayer ai_two(difficulty, match_seed * 10 + 2);

    // Safety cap: a 10x10/5-ship board can never take more than 100 shots
    // total (each cell shot at most once per side), so 500 is a generous
    // guard against an infinite loop bug without masking one.
    constexpr int kMaxTotalShots = 500;
    int total_shots = 0;
    while (game.status() == MatchStatus::InProgress && total_shots < kMaxTotalShots) {
        const PlayerId shooter = game.current_turn();
        AiPlayer& ai = shooter == PlayerId::One ? ai_one : ai_two;
        const TrackingBoard& tracking = game.tracking_board(shooter);
        const Coord shot = ai.next_shot(tracking, tracking.remaining_ship_types());
        ShotResult result = game.shoot(shooter, shot);
        // EXPECT (not ASSERT): gtest ASSERT_* macros require a void-returning
        // function; EXPECT still fails the test while letting this helper
        // return a value. The break prevents an infinite loop on failure.
        EXPECT_TRUE(result.is_valid_input()) << "AI fired at an invalid cell (" << shot.x << "," << shot.y << ")";
        if (!result.is_valid_input()) break;
        ++total_shots;
    }
    return total_shots;
}

// Returns {winner_is_ai_a_count, total_games, avg_shots_per_game} for a
// series of matches where side A uses `difficulty_a` and side B uses
// `difficulty_b`.
struct MatchupStats {
    int a_wins = 0;
    int games = 0;
    double avg_total_shots = 0.0;
};

MatchupStats simulate_matchup(Difficulty difficulty_a, Difficulty difficulty_b, int num_games,
                               std::uint64_t seed_base) {
    MatchupStats stats;
    stats.games = num_games;
    long long shot_sum = 0;

    for (int i = 0; i < num_games; ++i) {
        const std::uint64_t match_seed = seed_base + static_cast<std::uint64_t>(i);
        std::mt19937 placement_rng(static_cast<unsigned int>(match_seed * 2 + 1));
        Game game;
        place_fleet_randomly(game.board(PlayerId::One), placement_rng);
        place_fleet_randomly(game.board(PlayerId::Two), placement_rng);
        game.start(PlayerId::One);

        AiPlayer ai_one(difficulty_a, match_seed * 10 + 1);
        AiPlayer ai_two(difficulty_b, match_seed * 10 + 2);

        constexpr int kMaxTotalShots = 500;
        int total_shots = 0;
        while (game.status() == MatchStatus::InProgress && total_shots < kMaxTotalShots) {
            const PlayerId shooter = game.current_turn();
            AiPlayer& ai = shooter == PlayerId::One ? ai_one : ai_two;
            const TrackingBoard& tracking = game.tracking_board(shooter);
            const Coord shot = ai.next_shot(tracking, tracking.remaining_ship_types());
            ShotResult result = game.shoot(shooter, shot);
            EXPECT_TRUE(result.is_valid_input());
            if (!result.is_valid_input()) break;
            ++total_shots;
        }

        EXPECT_EQ(game.status(), MatchStatus::Finished) << "match_seed=" << match_seed << " did not finish";
        if (game.winner() == PlayerId::One) ++stats.a_wins;
        shot_sum += total_shots;
    }

    stats.avg_total_shots = static_cast<double>(shot_sum) / static_cast<double>(num_games);
    return stats;
}

constexpr int kStatGames = 250;

TEST(AiSelfPlay, AllGamesFinishInBoundedTurnsForEveryDifficulty) {
    for (auto difficulty : {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard}) {
        for (std::uint64_t seed = 0; seed < 60; ++seed) {
            int shots = simulate_self_play_match(difficulty, seed);
            // Two fully-random shooters can in the worst case each need
            // close to 100 shots to hit every cell; 200 combined is a safe
            // upper bound that still catches a runaway/infinite-loop bug.
            EXPECT_LT(shots, 200) << "difficulty=" << static_cast<int>(difficulty) << " seed=" << seed;
            EXPECT_GT(shots, 0) << "difficulty=" << static_cast<int>(difficulty) << " seed=" << seed;
        }
    }
}

TEST(AiSelfPlay, HardAverageShotsPerGameIsWellBelowRandomBaseline) {
    // Random-fire baseline: two purely-random shooters average close to the
    // full board being exhausted by one side (~ upper 90s total shots for
    // this board/fleet size). Hard should finish decisively faster.
    MatchupStats hard_vs_hard = simulate_matchup(Difficulty::Hard, Difficulty::Hard, kStatGames, /*seed_base=*/1000);

    // Spec: "Hard の平均決着手数が...理論値(≈95手)より十分小さい...Hard ≤ 60手/一人あたり"
    // i.e. average *total* shots (both sides combined) at game end should be
    // <= 120 (60 per player).
    EXPECT_LE(hard_vs_hard.avg_total_shots, 120.0) << "avg_total_shots=" << hard_vs_hard.avg_total_shots;

    printf("[AiSelfPlay] Hard vs Hard over %d games: avg total shots = %.2f (avg/player = %.2f)\n",
           hard_vs_hard.games, hard_vs_hard.avg_total_shots, hard_vs_hard.avg_total_shots / 2.0);
}

TEST(AiSelfPlay, DifficultyOrderingIsStatisticallySignificant) {
    // Hard vs Easy: Hard (as player One) should win a large majority.
    MatchupStats hard_vs_easy = simulate_matchup(Difficulty::Hard, Difficulty::Easy, kStatGames, /*seed_base=*/2000);
    const double hard_win_rate = static_cast<double>(hard_vs_easy.a_wins) / hard_vs_easy.games;
    EXPECT_GE(hard_win_rate, 0.75) << "hard_vs_easy win_rate=" << hard_win_rate;

    // Hard vs Normal: Hard should still win more than half decisively.
    MatchupStats hard_vs_normal =
        simulate_matchup(Difficulty::Hard, Difficulty::Normal, kStatGames, /*seed_base=*/3000);
    const double hard_vs_normal_rate = static_cast<double>(hard_vs_normal.a_wins) / hard_vs_normal.games;
    EXPECT_GE(hard_vs_normal_rate, 0.55) << "hard_vs_normal win_rate=" << hard_vs_normal_rate;

    // Normal vs Easy: Normal should win more than half.
    MatchupStats normal_vs_easy =
        simulate_matchup(Difficulty::Normal, Difficulty::Easy, kStatGames, /*seed_base=*/4000);
    const double normal_vs_easy_rate = static_cast<double>(normal_vs_easy.a_wins) / normal_vs_easy.games;
    EXPECT_GE(normal_vs_easy_rate, 0.55) << "normal_vs_easy win_rate=" << normal_vs_easy_rate;

    printf(
        "[AiSelfPlay] win rates -- Hard-vs-Easy: %.1f%%, Hard-vs-Normal: %.1f%%, "
        "Normal-vs-Easy: %.1f%% (over %d games each)\n",
        hard_win_rate * 100.0, hard_vs_normal_rate * 100.0, normal_vs_easy_rate * 100.0, kStatGames);
}

TEST(AiSelfPlay, EasyGamesTakeClearlyLongerThanHardGames) {
    // Sanity check on the strength gap from the "how fast do games end"
    // angle: Easy self-play (uniform-random hunting + hit follow-up only)
    // must need clearly more shots to finish than Hard self-play (full
    // density + parity). Note Easy still finishes far quicker than the
    // ~95-shots-per-player pure-random-no-followup baseline, because Target
    // mode's follow-up alone is a huge efficiency win -- so the meaningful
    // assertion is Easy-vs-Hard relative, not Easy-vs-blind-random absolute.
    MatchupStats easy_vs_easy = simulate_matchup(Difficulty::Easy, Difficulty::Easy, kStatGames, /*seed_base=*/5000);
    MatchupStats hard_vs_hard = simulate_matchup(Difficulty::Hard, Difficulty::Hard, kStatGames, /*seed_base=*/5000);

    EXPECT_GT(easy_vs_easy.avg_total_shots, hard_vs_hard.avg_total_shots + 10.0)
        << "easy avg=" << easy_vs_easy.avg_total_shots << " hard avg=" << hard_vs_hard.avg_total_shots;

    printf("[AiSelfPlay] Easy vs Easy over %d games: avg total shots = %.2f (avg/player = %.2f); "
           "Hard vs Hard: %.2f (avg/player = %.2f)\n",
           easy_vs_easy.games, easy_vs_easy.avg_total_shots, easy_vs_easy.avg_total_shots / 2.0,
           hard_vs_hard.avg_total_shots, hard_vs_hard.avg_total_shots / 2.0);
}

}  // namespace
