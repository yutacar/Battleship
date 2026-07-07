/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * ICommentator: the "解説役" (commentator) abstraction for Phase 3b. This is
 * strictly an observer of match state -- it never influences game logic, AI
 * decisions, or turn order (see PLAN.md §2.4). Two implementations exist:
 *   - TemplateCommentator (template_commentator.h): always-available,
 *     zero-dependency canned-phrase fallback. This is the only implementation
 *     commentator_factory.cpp wires up today (LLM commentary has been
 *     discontinued for all languages -- see NOTES.md "LLM 廃止" section).
 *   - LlmCommentator (llm_commentator.h, source kept behind BATTLESHIP_LLM,
 *     default OFF): asynchronous llama.cpp-backed generator that falls back
 *     to TemplateCommentator when no model file is configured/found. Kept
 *     in the tree for a possible future revival but not built by default
 *     and never selected by commentator_factory.cpp even when the flag is
 *     enabled at build time.
 *
 * Both implementations are non-blocking from the caller's point of view:
 * `request_comment()` returns immediately, and the resulting text (if any)
 * arrives later via the callback supplied at construction/attach time. This
 * mirrors NOTES.md §8.5's requirement that generation never share the LVGL
 * thread synchronously.
 */
#pragma once

#include <functional>
#include <string>

#include "core/snapshot.h"

namespace battleship::commentary {

/// Output language for generated commentary. Only English strings are
/// implemented in Phase 3b; the enum and plumbing exist now so Phase 5
/// (i18n) can add JA/ZH string tables and LLM prompt instructions without
/// reshaping this interface (PLAN.md §Phase 5 design notes).
enum class Lang {
    EN,
    JA,
    ZH,
};

/// Discrete moments the commentator may react to. Matches PLAN.md §2.4's
/// enumerated event list: human/CPU hit/miss/sunk, match start, and the
/// final match-over summary.
enum class CommentEvent {
    MatchStart,
    HumanMiss,
    HumanHit,
    HumanSunk,
    CpuMiss,
    CpuHit,
    CpuSunk,
    MatchOver,
};

/// Extra event-specific detail that doesn't live on MatchSnapshot itself
/// (e.g. which ship type was just sunk, or the coordinate just fired at).
/// All fields are optional/best-effort; implementations should tolerate
/// defaults gracefully.
struct CommentContext {
    battleship::core::ShipType sunk_ship = battleship::core::ShipType::Destroyer;
    battleship::core::Coord last_shot{0, 0};
};

/// Callback invoked (on the caller's thread of choice -- see each
/// implementation's threading notes) with the generated comment text. Empty
/// string means "no comment this time" (e.g. generation didn't finish in
/// time) and callers should leave the previous comment displayed rather than
/// clearing it.
using CommentCallback = std::function<void(const std::string&)>;

class ICommentator {
public:
    virtual ~ICommentator() = default;

    /// Requests a short (1-2 sentence) comment for `event`, given the match
    /// state in `snapshot`. Implementations MUST return promptly (i.e. must
    /// not synchronously run LLM inference); the result is delivered later
    /// via the callback passed to the constructor or set_callback().
    ///
    /// Calling this again before a previous request finished must discard
    /// the stale in-flight request (only the latest event matters -- PLAN.md
    /// §2.4/§3 "最新のみ処理").
    virtual void request_comment(const battleship::core::MatchSnapshot& snapshot,
                                 CommentEvent event,
                                 const CommentContext& context = {}) = 0;

    /// Discards any in-flight request without invoking its callback. Called
    /// when a match resets (BaseViewModel::start_new_match()) so a stale
    /// comment from the previous game never lands after the fact.
    virtual void cancel_pending() = 0;

    /// Sets/replaces the callback used to deliver generated text. Safe to
    /// call before the first request_comment().
    virtual void set_callback(CommentCallback callback) = 0;

    /// Output language for future requests. Phase 3b implementations only
    /// ship EN text; Phase 5 fills in JA/ZH.
    virtual void set_language(Lang lang) = 0;
    virtual Lang language() const = 0;
};

}  // namespace battleship::commentary
