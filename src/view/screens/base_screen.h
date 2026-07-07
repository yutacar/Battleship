/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * GameScreen: lightweight full-screen base for Battleship's own screens.
 * Unlike the imported template's original BaseScreen (which reserves a 30px
 * TitleBar + 30px NavBar, 60 of 170px / 35% of the vertical budget for
 * chrome), Battleship's screens need the entire 320x170 for board/menu
 * content and drive navigation entirely via keyboard (arrow keys + Enter),
 * so there is no on-screen nav chrome to reserve space for. Kept under the
 * same file names (base_screen.h/.cpp) as the template to minimize the
 * include-path churn in CMakeLists.txt.
 */
#pragma once

#include "lvgl.h"

#include <memory>

namespace app {
class AssetManager;
}

namespace viewmodel {
class BaseViewModel;
}

namespace screen {

class BaseScreen {
public:
    BaseScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    virtual ~BaseScreen();

    BaseScreen(const BaseScreen&) = delete;
    BaseScreen& operator=(const BaseScreen&) = delete;

    /// Application-wide input group, shared by every screen instance and
    /// owned by ScreenManager (see screen_manager.cpp). Screens must add
    /// their focusable widgets to this group from build_content(); the
    /// ScreenManager clears its contents on every screen switch so screens
    /// never see stale focusable objects from the previous screen.
    static void set_shared_group(lv_group_t* group);
    static lv_group_t* shared_group();

    /// Makes arrow keys move keyboard focus while `obj` is focused: Right/
    /// Down focus the next object in the shared group, Left/Up the previous
    /// one. Needed because LVGL's keypad indev only moves group focus on
    /// LV_KEY_NEXT/PREV (Tab-style keys the CardputerZero key mapping does
    /// not produce) and otherwise forwards arrow keys to the focused widget,
    /// where plain buttons ignore them -- without this, multi-button screens
    /// (Result's Rematch/Title, Menu once more entries unlock) have exactly
    /// one keyboard-reachable button. Do NOT use on the board widgets, whose
    /// own LV_EVENT_KEY handlers consume arrows for cursor movement.
    static void enable_arrow_focus_navigation(lv_obj_t* obj);

    void init();
    lv_obj_t* root() const;

protected:
    virtual void build_content(lv_obj_t* content) = 0;

    viewmodel::BaseViewModel& view_model();
    app::AssetManager& assets();

private:
    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    lv_obj_t* root_{nullptr};
};

} // namespace screen
