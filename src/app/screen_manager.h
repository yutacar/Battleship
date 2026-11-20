/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asset_manager.h"
#include "base_screen.h"
#include "base_viewmodel.h"
#include "lvgl.h"

#include <memory>

namespace app {

class ScreenManager {
public:
    ScreenManager(viewmodel::BaseViewModel& view_model, AssetManager& assets);
    ~ScreenManager();

    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;

    void start();
    void request_page(model::AppPage page);
    void flush_requested_page();
    lv_obj_t* current_screen() const;

    /// Rebuilds whatever screen is currently loaded from scratch (same page,
    /// brand-new BaseScreen instance). Used by BaseViewModel::set_language()
    /// so every label re-reads i18n::tr() immediately after a language
    /// switch (PLAN.md Phase 5: "選択即反映") -- request_page() alone would
    /// no-op here since the page value itself doesn't change.
    void reload_current_screen();

    /// Shared keypad focus group used by every screen (arrow keys move focus
    /// between focusable widgets, Enter/OK activates them). Must be attached
    /// to the keypad indev by the caller (see app.cpp) so it works
    /// regardless of which platform key input backend is active.
    lv_group_t* input_group() const { return input_group_; }

    // ---- Esc-key convention (Phase 7, App Submission Guide) ----
    /// Handles the Esc/Home key convention centrally, independent of which
    /// widget currently has keypad focus. Installed as an LV_EVENT_KEY
    /// callback on every keypad indev (see app.cpp):
    ///   - short Esc: go back one level (or close the interrupt-confirm
    ///     modal when it is showing);
    ///   - Esc held ~1s, or Home: safe exit from anywhere (clean network
    ///     close + normal process termination via quit_requested).
    /// Long-press detection is done here (press timestamp + repeated
    /// LV_EVENT_KEY deliveries while the key stays pressed) because LVGL's
    /// built-in keypad long-press events only fire for LV_KEY_ENTER
    /// (lv_indev.c indev_keypad_proc), never for Esc.
    void attach_esc_handling(lv_indev_t* indev);

    /// Back one level from the current screen -- the same page transition
    /// the screen's own on-screen Back/Cancel button performs. Public so
    /// the desktop APP_SCRIPT harness can drive it; normally invoked only
    /// from the indev key callback.
    void handle_short_esc();
    /// Safe exit: best-effort notifies a network peer, tears the session
    /// down and requests a clean main-loop exit.
    void handle_safe_exit();

private:
    void load_screen(std::unique_ptr<screen::BaseScreen> next_screen);
    void show_screen_for_page(model::AppPage page);
    void on_indev_key(lv_indev_t* indev);
    void show_interrupt_modal();
    void hide_interrupt_modal();
    static void indev_key_cb(lv_event_t* event);
    static void interrupt_modal_observer(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    AssetManager& assets_;
    std::unique_ptr<screen::BaseScreen> current_screen_;
    lv_group_t* input_group_ = nullptr;
    lv_observer_t* page_observer_ = nullptr;
    lv_observer_t* language_observer_ = nullptr;
    lv_observer_t* modal_observer_ = nullptr;
    model::AppPage requested_page_ = model::AppPage::Menu;
    model::AppPage loaded_page_ = model::AppPage::Menu;
    bool has_loaded_page_ = false;
    bool page_switch_scheduled_ = false;

    // ---- Esc/Home key tracking (see attach_esc_handling) ----
    // The device exposes several keypad-capable evdev nodes (vc4-hdmi CEC,
    // gpio_ir_recv, tca8418c keyboard), each its own POLL-mode indev that
    // reports LV_EVENT_KEY with state RELEASED on every read while idle.
    // Tracking is therefore bound to the indev that pressed Esc/Home: an
    // idle node's RELEASED report must never count as the Esc release.
    bool esc_down_ = false;
    bool esc_long_fired_ = false;
    lv_indev_t* esc_indev_ = nullptr;
    model::AppPage esc_page_ = model::AppPage::Menu;
    bool home_down_ = false;
    lv_indev_t* home_indev_ = nullptr;
    uint32_t esc_pressed_at_ = 0;

    // ---- Double-Esc to leave the title (Menu) ----
    void arm_title_quit();
    void disarm_title_quit();
    static void title_quit_timeout_cb(lv_timer_t* timer);
    lv_timer_t* title_quit_timer_ = nullptr;

    // ---- Interrupt-confirm modal (overlay on the current screen) ----
    lv_obj_t* interrupt_modal_ = nullptr;
    lv_obj_t* modal_prev_focus_ = nullptr;
};

} // namespace app
