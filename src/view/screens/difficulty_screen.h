/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Single-player difficulty selection (Menu "1 Player" -> here -> Placement).
/// Three focusable buttons (Easy / Normal / Hard) plus Back to the menu;
/// Up/Down move focus, Enter starts a vs-AI match at the chosen tier.
class DifficultyScreen : public BaseScreen {
public:
    DifficultyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
