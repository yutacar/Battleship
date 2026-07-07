/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Pure, LVGL-free helpers that turn a MatchSnapshot + CommentEvent into
 * short structured facts. Shared by TemplateCommentator (which picks a
 * canned phrase using these facts) and LlmCommentator's prompt builder
 * (which renders them into an English prompt for llama.cpp). Kept separate
 * from both so it can be unit-tested without constructing either
 * commentator implementation.
 */
#pragma once

#include <string>

#include "commentary/commentator.h"
#include "core/snapshot.h"

namespace battleship::commentary {

/// Derived, human-readable facts about one side of the match, computed from
/// PlayerSnapshot. Hit rate is rounded to the nearest percent; 0 shots fired
/// reports 0% rather than dividing by zero.
struct SideFacts {
    int shots_fired = 0;
    int hits_recorded = 0;
    int hit_rate_pct = 0;
    int ships_remaining = 0;
    int ships_sunk = 0;
};

SideFacts summarize_side(const battleship::core::PlayerSnapshot& side);

/// Renders a compact, single-paragraph English summary of `snapshot` from
/// the human's (PlayerId::One's) point of view -- e.g.
/// "Turn 14. You: 6/10 shots hit (60%), 4 ships left. CPU: 5/13 shots hit
/// (38%), 3 ships left, 2 sunk.". Used as the factual grounding block of the
/// LLM prompt and available to tests independent of any LLM being present.
std::string summarize_match_for_prompt(const battleship::core::MatchSnapshot& snapshot);

/// Short event-name fragment used both in the LLM instruction line and in
/// TemplateCommentator's lookup ("human_hit", "cpu_sunk", ...). Stable,
/// ASCII, no whitespace -- safe to use as a table key.
std::string event_key(CommentEvent event);

/// Builds the full LLM prompt: a short system-style instruction, the match
/// facts from summarize_match_for_prompt(), and the triggering event +
/// context (e.g. which ship was just sunk, or last coordinate fired at).
/// `lang` selects the output-language instruction; only EN wording is
/// implemented in Phase 3b (JA/ZH fall back to the same EN instruction with
/// a TODO marker for Phase 5, see snapshot_summary.cpp).
std::string build_llm_prompt(const battleship::core::MatchSnapshot& snapshot,
                             CommentEvent event,
                             const CommentContext& context,
                             Lang lang);

}  // namespace battleship::commentary
