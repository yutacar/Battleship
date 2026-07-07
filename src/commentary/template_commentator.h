/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * TemplateCommentator: the always-available fallback commentator. Picks a
 * varied but deterministic-given-seed canned phrase per event, optionally
 * flavored with facts pulled from the snapshot (hit rate, ships remaining).
 * No threads, no I/O, no LVGL -- request_comment() computes and delivers
 * synchronously via the callback, which is safe here (string formatting
 * only) but LlmCommentator must NOT do the same (see commentator.h).
 */
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "commentary/commentator.h"

namespace battleship::commentary {

class TemplateCommentator final : public ICommentator {
public:
    /// `seed` controls phrase-variety tie-breaks; defaults to a
    /// std::random_device draw so real gameplay doesn't repeat the same
    /// phrase pattern every match, while tests can inject a fixed seed for
    /// reproducibility.
    explicit TemplateCommentator(std::uint64_t seed = std::random_device{}());

    void request_comment(const battleship::core::MatchSnapshot& snapshot, CommentEvent event,
                         const CommentContext& context = {}) override;
    void cancel_pending() override;
    void set_callback(CommentCallback callback) override;
    void set_language(Lang lang) override;
    Lang language() const override;

    /// Pure variant used by tests: returns the phrase without needing a
    /// callback/instance round-trip. Exercises the exact same selection
    /// logic as request_comment().
    std::string comment_for(const battleship::core::MatchSnapshot& snapshot, CommentEvent event,
                            const CommentContext& context = {});

private:
    CommentCallback callback_;
    Lang lang_ = Lang::EN;
    std::mt19937_64 rng_;
};

}  // namespace battleship::commentary
