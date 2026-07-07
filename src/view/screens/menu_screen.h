/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Title/menu screen: game logo-ish title + a vertical menu. "2 Player
/// (Hotseat)" starts a hotseat match; "1 Player" (Phase 3a) opens the
/// difficulty-select screen for a vs-AI match. "Network Play" and "Settings"
/// remain disabled ("Coming soon") until Phase 4+. Up/Down move the
/// selection, Enter activates.
class MenuScreen : public BaseScreen {
public:
    MenuScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
