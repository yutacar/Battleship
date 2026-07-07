/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Single seam that decides which ICommentator implementation the app wires
 * up, so BaseViewModel (and every other caller) stays free of #ifdef noise.
 *
 * LLM commentary has been discontinued for all languages (EN/JA/ZH) --
 * make_default_commentator() always resolves to TemplateCommentator now.
 * See NOTES.md "LLM 廃止" section and PLAN.md §2.4. LlmCommentator's source
 * is kept in the tree behind BATTLESHIP_LLM (default OFF) for a possible
 * future revival, but this factory does not wire it up even when that flag
 * is enabled.
 */
#pragma once

#include <memory>

#include "commentary/commentator.h"

namespace battleship::commentary {

/// Feature-flag state read at startup, exposed for logging/diagnostics and
/// for the Settings screen this will grow into during Phase 5.
struct CommentatorConfig {
    bool enabled = true;       ///< BATTLESHIP_COMMENTARY=0 disables entirely (returns nullptr).
    std::string model_path;    ///< Vestigial: LLM commentary is discontinued, always empty now.
};

/// Reads the current environment (BATTLESHIP_COMMENTARY) into a
/// CommentatorConfig. Exposed separately from make_default_commentator() so
/// tests / diagnostics can inspect the resolved config without constructing
/// a commentator.
CommentatorConfig read_commentator_config_from_env();

/// Builds the commentator the running app should use:
///   - nullptr if commentary is disabled (BATTLESHIP_COMMENTARY=0).
///   - TemplateCommentator otherwise (LLM commentary discontinued -- see
///     file header).
std::unique_ptr<ICommentator> make_default_commentator();

}  // namespace battleship::commentary
