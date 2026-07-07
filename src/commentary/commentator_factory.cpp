/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * LLM-backed commentary has been discontinued for all languages (EN/JA/ZH
 * alike) in favor of the always-available TemplateCommentator -- see
 * NOTES.md "LLM 廃止" section and PLAN.md §2.4. LlmCommentator's source is
 * kept in the tree behind BATTLESHIP_LLM (default OFF, see CMakeLists.txt)
 * in case local-LLM commentary is revisited later, but this factory no
 * longer wires it up even when that flag is enabled at build time --
 * make_default_commentator() always returns a TemplateCommentator (or
 * nullptr if commentary is disabled entirely).
 */
#include "commentary/commentator_factory.h"

#include <cstdlib>

#include "commentary/template_commentator.h"
#include "logger.h"

namespace battleship::commentary {

namespace {

bool env_flag_is_false(const char* value) {
    return value && (std::string(value) == "0" || std::string(value) == "false" ||
                    std::string(value) == "off" || std::string(value) == "OFF");
}

}  // namespace

CommentatorConfig read_commentator_config_from_env() {
    CommentatorConfig config;
    if (const char* enabled_env = std::getenv("BATTLESHIP_COMMENTARY")) {
        config.enabled = !env_flag_is_false(enabled_env);
    }
    // BATTLESHIP_LLM_MODEL is intentionally no longer read: LLM commentary
    // has been discontinued, so config.model_path stays empty and is now
    // vestigial (retained on CommentatorConfig only to avoid churning the
    // struct's public shape). See file header.
    return config;
}

std::unique_ptr<ICommentator> make_default_commentator() {
    const CommentatorConfig config = read_commentator_config_from_env();
    if (!config.enabled) {
        LOG_INFO("Commentary disabled via BATTLESHIP_COMMENTARY");
        return nullptr;
    }

    LOG_INFO("Commentary: using TemplateCommentator (LLM commentary discontinued)");
    return std::make_unique<TemplateCommentator>();
}

}  // namespace battleship::commentary
