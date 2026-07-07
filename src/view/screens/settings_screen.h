/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Settings screen (Phase 5, i18n): Menu "Settings" -> here -> back to Menu.
/// One row of language choices (English / 日本語 / 简体中文); selecting one
/// applies + persists it immediately (BaseViewModel::set_language()) and
/// rebuilds the current screen so this same Settings screen re-renders in
/// the newly chosen language right away. Up/Down move focus, Enter selects.
class SettingsScreen : public BaseScreen {
public:
    SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
