/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "commentary/snapshot_summary.h"

#include <cstdio>

#include "core/types.h"

namespace battleship::commentary {

using battleship::core::MatchSnapshot;
using battleship::core::PlayerSnapshot;

SideFacts summarize_side(const PlayerSnapshot& side) {
    SideFacts facts;
    facts.shots_fired = side.shots_fired;
    facts.hits_recorded = side.hits_recorded;
    facts.hit_rate_pct = side.shots_fired > 0
                              ? static_cast<int>(100.0 * side.hits_recorded / side.shots_fired + 0.5)
                              : 0;
    facts.ships_remaining = static_cast<int>(side.own_ships_remaining.size());
    facts.ships_sunk = static_cast<int>(side.enemy_ships_sunk.size());
    return facts;
}

std::string summarize_match_for_prompt(const MatchSnapshot& snapshot) {
    const auto you = summarize_side(snapshot.player_one);
    const auto cpu = summarize_side(snapshot.player_two);

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Turn %d. You: %d/%d shots hit (%d%%), %d ships left. "
                  "CPU: %d/%d shots hit (%d%%), %d ships left, %d sunk.",
                  snapshot.turn_number, you.hits_recorded, you.shots_fired, you.hit_rate_pct,
                  you.ships_remaining, cpu.hits_recorded, cpu.shots_fired, cpu.hit_rate_pct,
                  cpu.ships_remaining, you.ships_sunk);
    return buf;
}

std::string event_key(CommentEvent event) {
    switch (event) {
        case CommentEvent::MatchStart:
            return "match_start";
        case CommentEvent::HumanMiss:
            return "human_miss";
        case CommentEvent::HumanHit:
            return "human_hit";
        case CommentEvent::HumanSunk:
            return "human_sunk";
        case CommentEvent::CpuMiss:
            return "cpu_miss";
        case CommentEvent::CpuHit:
            return "cpu_hit";
        case CommentEvent::CpuSunk:
            return "cpu_sunk";
        case CommentEvent::MatchOver:
            return "match_over";
    }
    return "unknown";
}

namespace {

/// Output-language instruction line appended to the system prompt. The
/// instruction itself is written in the TARGET language (not English):
/// during Phase 5 quality testing, SmolLM2-360M followed a target-language
/// instruction noticeably more reliably than an English-phrased "Reply in
/// Japanese." (an English instruction sandwiched in an all-English prompt
/// often produced English output anyway). See NOTES.md Phase 5 for the
/// measured JA/ZH generation quality and the fallback decision based on it.
std::string language_instruction(Lang lang) {
    switch (lang) {
        case Lang::EN:
            return "Reply in English.";
        case Lang::JA:
            return "必ず日本語だけで1文だけ答えてください。英語は使わないでください。";
        case Lang::ZH:
            return "请只用简体中文回答，只写一句话，不要使用英文。";
    }
    return "Reply in English.";
}

std::string event_description(CommentEvent event, const CommentContext& context) {
    const std::string ship = battleship::core::to_string(context.sunk_ship);
    switch (event) {
        case CommentEvent::MatchStart:
            return "The match has just started.";
        case CommentEvent::HumanMiss:
            return "The human player just fired and missed.";
        case CommentEvent::HumanHit:
            return "The human player just scored a hit.";
        case CommentEvent::HumanSunk:
            return "The human player just sank the CPU's " + ship + ".";
        case CommentEvent::CpuMiss:
            return "The CPU just fired and missed.";
        case CommentEvent::CpuHit:
            return "The CPU just scored a hit on the human's fleet.";
        case CommentEvent::CpuSunk:
            return "The CPU just sank the human's " + ship + ".";
        case CommentEvent::MatchOver:
            return "The match has just ended.";
    }
    return "";
}

}  // namespace

std::string build_llm_prompt(const MatchSnapshot& snapshot, CommentEvent event,
                             const CommentContext& context, Lang lang) {
    // ChatML wrapping (<|im_start|>role ... <|im_end|>) matches SmolLM2-
    // Instruct's chat_template (see the GGUF metadata); using the model's
    // trained instruction format instead of a bare completion prompt cuts
    // down substantially on rambling/echoing the instructions back, which
    // plain-text prompts triggered during manual latency testing (see
    // NOTES.md Phase 3b generation quality notes).
    std::string system_msg =
        "You are a terse, upbeat naval battle commentator for a Battleship game. "
        "Given the facts below, write ONE short sentence (max ~20 words) reacting "
        "to the event. Do not invent facts not given, and do not ask questions. " +
        language_instruction(lang);

    std::string user_msg = "Facts: " + summarize_match_for_prompt(snapshot) + "\n";
    user_msg += "Event: " + event_description(event, context);

    std::string prompt = "<|im_start|>system\n" + system_msg + "<|im_end|>\n";
    prompt += "<|im_start|>user\n" + user_msg + "<|im_end|>\n";
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

}  // namespace battleship::commentary
