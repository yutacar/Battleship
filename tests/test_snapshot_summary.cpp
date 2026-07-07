// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// Tests for commentary::summarize_side / summarize_match_for_prompt --
// pure snapshot-to-text helpers, no LLM/commentator instance required. See
// NOTES.md Phase 3b: "プロンプト生成（snapshot → テキスト要約）の内容検証
// （LLM 実体不要のテスト）".
//
// LLM commentary has since been discontinued (NOTES.md "LLM 廃止" section);
// the build_llm_prompt()-specific tests that used to live here are now
// isolated behind #if BATTLESHIP_LLM at the bottom of this file (default
// OFF, so they do not run/count in the default ctest suite). event_key()
// is still exercised above/elsewhere since TemplateCommentator-adjacent
// tests use it for assertion messages, independent of the LLM flag.

#include "commentary/snapshot_summary.h"

#include <gtest/gtest.h>

#include "core/game.h"

using namespace battleship::core;
using namespace battleship::commentary;

namespace {

void place_standard_fleet(Board& board, int row_offset = 0) {
    board.place_ship(Ship(ShipType::Carrier, Coord(0, row_offset + 0), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Battleship, Coord(0, row_offset + 1), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Cruiser, Coord(0, row_offset + 2), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Submarine, Coord(0, row_offset + 3), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, row_offset + 4), Orientation::Horizontal));
}

}  // namespace

TEST(SnapshotSummary, SummarizeSideHandlesZeroShotsWithoutDivideByZero) {
    PlayerSnapshot side;
    side.shots_fired = 0;
    side.hits_recorded = 0;

    const auto facts = summarize_side(side);
    EXPECT_EQ(facts.shots_fired, 0);
    EXPECT_EQ(facts.hits_recorded, 0);
    EXPECT_EQ(facts.hit_rate_pct, 0);
}

TEST(SnapshotSummary, SummarizeSideComputesRoundedHitRate) {
    PlayerSnapshot side;
    side.shots_fired = 3;
    side.hits_recorded = 2;  // 66.67% -> rounds to 67

    const auto facts = summarize_side(side);
    EXPECT_EQ(facts.hit_rate_pct, 67);
}

TEST(SnapshotSummary, SummarizeSideCountsRemainingAndSunkShips) {
    PlayerSnapshot side;
    side.own_ships_remaining = {ShipType::Carrier, ShipType::Battleship};
    side.enemy_ships_sunk = {ShipType::Destroyer};

    const auto facts = summarize_side(side);
    EXPECT_EQ(facts.ships_remaining, 2);
    EXPECT_EQ(facts.ships_sunk, 1);
}

TEST(SnapshotSummary, MatchSummaryMentionsTurnNumberAndBothSides) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);
    game.shoot(PlayerId::One, Coord(0, 0));  // hits player two's carrier

    const auto snapshot = game.snapshot();
    const std::string summary = summarize_match_for_prompt(snapshot);

    EXPECT_NE(summary.find("Turn 1"), std::string::npos);
    EXPECT_NE(summary.find("You:"), std::string::npos);
    EXPECT_NE(summary.find("CPU:"), std::string::npos);
    EXPECT_NE(summary.find("1/1 shots hit (100%)"), std::string::npos);
}

TEST(SnapshotSummary, EventKeyIsStableAsciiIdentifierPerEvent) {
    EXPECT_EQ(event_key(CommentEvent::MatchStart), "match_start");
    EXPECT_EQ(event_key(CommentEvent::HumanMiss), "human_miss");
    EXPECT_EQ(event_key(CommentEvent::HumanHit), "human_hit");
    EXPECT_EQ(event_key(CommentEvent::HumanSunk), "human_sunk");
    EXPECT_EQ(event_key(CommentEvent::CpuMiss), "cpu_miss");
    EXPECT_EQ(event_key(CommentEvent::CpuHit), "cpu_hit");
    EXPECT_EQ(event_key(CommentEvent::CpuSunk), "cpu_sunk");
    EXPECT_EQ(event_key(CommentEvent::MatchOver), "match_over");
}

// LLM-prompt-specific tests: discontinued along with LlmCommentator (see
// commentator_factory.cpp/NOTES.md "LLM 廃止"). build_llm_prompt() itself
// is still compiled unconditionally (small pure function in
// snapshot_summary.cpp, harmless to keep), but it is only ever called from
// LlmCommentator, which is now source-only and gated behind
// BATTLESHIP_LLM=OFF-by-default. Isolating these tests behind the same
// flag keeps the default ctest suite from asserting on a code path the
// product no longer ships.
#if BATTLESHIP_LLM

TEST(SnapshotSummary, BuildLlmPromptIncludesFactsEventAndLanguageInstruction) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);
    const auto snapshot = game.snapshot();

    CommentContext context;
    context.sunk_ship = ShipType::Destroyer;

    const std::string prompt = build_llm_prompt(snapshot, CommentEvent::CpuSunk, context, Lang::EN);

    EXPECT_NE(prompt.find("Reply in English."), std::string::npos);
    EXPECT_NE(prompt.find("Facts:"), std::string::npos);
    EXPECT_NE(prompt.find("Event:"), std::string::npos);
    EXPECT_NE(prompt.find("Destroyer"), std::string::npos);
    // ChatML wrapping (matches SmolLM2-Instruct's trained chat template).
    EXPECT_NE(prompt.find("<|im_start|>system"), std::string::npos);
    EXPECT_NE(prompt.find("<|im_start|>user"), std::string::npos);
    EXPECT_NE(prompt.find("<|im_start|>assistant"), std::string::npos);
}

TEST(SnapshotSummary, BuildLlmPromptUsesTargetLanguageInstructionForJaZh) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);
    const auto snapshot = game.snapshot();

    // Phase 5: the output-language instruction is phrased in the target
    // language itself (SmolLM2-360M follows that more reliably than an
    // English "Reply in Japanese." -- see snapshot_summary.cpp).
    const std::string ja_prompt = build_llm_prompt(snapshot, CommentEvent::MatchStart, {}, Lang::JA);
    const std::string zh_prompt = build_llm_prompt(snapshot, CommentEvent::MatchStart, {}, Lang::ZH);

    EXPECT_NE(ja_prompt.find("日本語"), std::string::npos);
    EXPECT_EQ(ja_prompt.find("Reply in English."), std::string::npos);
    EXPECT_NE(zh_prompt.find("简体中文"), std::string::npos);
    EXPECT_EQ(zh_prompt.find("Reply in English."), std::string::npos);
}

#endif  // BATTLESHIP_LLM
