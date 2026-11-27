/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "screen_manager.h"

#include "bindings.h"
#include "theme.h"
#include "i18n/fonts.h"
#include "i18n/strings.h"

#include "battle_screen.h"
#include "difficulty_screen.h"
#include "handover_screen.h"
#include "menu_screen.h"
#include "net_antenna_screen.h"
#include "net_direct_ip_screen.h"
#include "net_error_screen.h"
#include "net_join_screen.h"
#include "net_lobby_screen.h"
#include "net_transport_screen.h"
#include "network_menu_screen.h"
#include "placement_screen.h"
#include "result_screen.h"
#include "settings_screen.h"

namespace app {
namespace {

void flush_requested_page_async(void* user_data) {
    auto* manager = static_cast<ScreenManager*>(user_data);
    if (manager) {
        manager->flush_requested_page();
    }
}

void current_page_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (!manager) {
        return;
    }

    manager->request_page(static_cast<model::AppPage>(lv_subject_get_int(subject)));
}

void language_version_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (manager) {
        // Rebuild in place (not via request_page/lv_async_call): a language
        // switch happens from a button click already inside the LVGL event
        // loop, so an immediate rebuild is safe and avoids a one-frame flash
        // of the old-language screen that an async-deferred reload would add.
        manager->reload_current_screen();
    }
}

// ---- Interrupt-confirm modal callbacks (Esc convention, Phase 7) ----

void modal_yes_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->confirm_interrupt();
    }
}

void modal_no_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->dismiss_interrupt_confirm();
    }
}

/// Any arrow key toggles focus between the modal's two buttons (the other
/// button rides in user_data). The modal deliberately does NOT use
/// BaseScreen::enable_arrow_focus_navigation -- that helper walks the whole
/// shared group, which still contains the underlying screen's widgets, and
/// focus must never escape the modal while it is up.
void modal_arrow_cb(lv_event_t* event) {
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_UP || key == LV_KEY_DOWN) {
        auto* other = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
        if (other && lv_obj_is_valid(other)) {
            lv_group_focus_obj(other);
        }
    }
}

/// Hold duration that separates "back one level" from "safe exit"
/// (App Submission Guide: "long Esc (~1s) or Home exits safely").
constexpr uint32_t kEscLongPressMs = 1000;

/// Window for the second Esc of a double-Esc on the title (Menu). A single
/// Esc there only arms the exit, so an Esc that just brought the user back
/// to the title can never drop them straight to the launcher.
constexpr uint32_t kTitleDoubleEscWindowMs = 1500;

} // namespace

ScreenManager::ScreenManager(viewmodel::BaseViewModel& view_model, AssetManager& assets)
    : view_model_(view_model), assets_(assets) {
    input_group_ = lv_group_create();
    screen::BaseScreen::set_shared_group(input_group_);
}

ScreenManager::~ScreenManager() {
    lv_async_call_cancel(flush_requested_page_async, this);
    if (title_quit_timer_) {
        lv_timer_delete(title_quit_timer_);
        title_quit_timer_ = nullptr;
    }

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (language_observer_) {
        lv_observer_remove(language_observer_);
        language_observer_ = nullptr;
    }
    if (modal_observer_) {
        lv_observer_remove(modal_observer_);
        modal_observer_ = nullptr;
    }

    // Screens hold raw lv_obj_t*s registered into input_group_; delete them
    // before destroying the group they're registered in.
    current_screen_.reset();

    screen::BaseScreen::set_shared_group(nullptr);
    if (input_group_) {
        lv_group_delete(input_group_);
        input_group_ = nullptr;
    }
}

void ScreenManager::start() {
    if (page_observer_) {
        return;
    }

    page_observer_ = lv_subject_add_observer_with_target(view_model_.current_page_subject(),
                                                         current_page_observer,
                                                         this,
                                                         nullptr);
    language_observer_ = lv_subject_add_observer_with_target(view_model_.language_version_subject(),
                                                              language_version_observer,
                                                              this,
                                                              nullptr);
    modal_observer_ = lv_subject_add_observer_with_target(view_model_.interrupt_modal_subject(),
                                                          &ScreenManager::interrupt_modal_observer,
                                                          this,
                                                          nullptr);
    // Kick off the very first screen (Menu) without waiting for a subject
    // change notification, since the subject already holds Menu's value.
    show_screen_for_page(view_model_.current_page());
    loaded_page_ = view_model_.current_page();
    has_loaded_page_ = true;
}

void ScreenManager::request_page(model::AppPage page) {
    requested_page_ = page;

    if (page_switch_scheduled_) {
        return;
    }

    page_switch_scheduled_ = lv_async_call(flush_requested_page_async, this) == LV_RESULT_OK;
}

void ScreenManager::flush_requested_page() {
    page_switch_scheduled_ = false;

    if (has_loaded_page_ && requested_page_ == loaded_page_) {
        return;
    }

    disarm_title_quit();
    show_screen_for_page(requested_page_);
    loaded_page_ = requested_page_;
    has_loaded_page_ = true;
}

lv_obj_t* ScreenManager::current_screen() const {
    return current_screen_ ? current_screen_->root() : nullptr;
}

void ScreenManager::reload_current_screen() {
    show_screen_for_page(has_loaded_page_ ? loaded_page_ : view_model_.current_page());
}

void ScreenManager::show_screen_for_page(model::AppPage page) {
    switch (page) {
        case model::AppPage::Placement:
            load_screen(std::make_unique<screen::PlacementScreen>(view_model_, assets_));
            break;
        case model::AppPage::Handover:
            load_screen(std::make_unique<screen::HandoverScreen>(view_model_, assets_));
            break;
        case model::AppPage::Battle:
            load_screen(std::make_unique<screen::BattleScreen>(view_model_, assets_));
            break;
        case model::AppPage::Result:
            load_screen(std::make_unique<screen::ResultScreen>(view_model_, assets_));
            break;
        case model::AppPage::Difficulty:
            load_screen(std::make_unique<screen::DifficultyScreen>(view_model_, assets_));
            break;
        case model::AppPage::NetTransport:
            load_screen(std::make_unique<screen::NetTransportScreen>(view_model_, assets_));
            break;
        case model::AppPage::NetAntennaConfirm:
            load_screen(std::make_unique<screen::NetAntennaScreen>(view_model_, assets_));
            break;
        case model::AppPage::NetworkMenu:
            load_screen(std::make_unique<screen::NetworkMenuScreen>(view_model_, assets_));
            break;
        case model::AppPage::NetLobby:
            load_screen(std::make_unique<screen::NetLobbyScreen>(view_model_, assets_));
            break;
        case model::AppPage::NetJoin:
            load_screen(std::make_unique<screen::NetJoinScreen>(view_model_, assets_));
            break;
        case model::AppPage::NetDirectIp:
            load_screen(std::make_unique<screen::NetDirectIpScreen>(view_model_, assets_));
            break;
        case model::AppPage::NetError:
            load_screen(std::make_unique<screen::NetErrorScreen>(view_model_, assets_));
            break;
        case model::AppPage::Settings:
            load_screen(std::make_unique<screen::SettingsScreen>(view_model_, assets_));
            break;
        case model::AppPage::Menu:
        default:
            load_screen(std::make_unique<screen::MenuScreen>(view_model_, assets_));
            break;
    }
}

void ScreenManager::load_screen(std::unique_ptr<screen::BaseScreen> next_screen) {
    if (!next_screen || !next_screen->root()) {
        return;
    }

    // A page switch invalidates any open interrupt-confirm modal: the
    // overlay object lives on the outgoing screen and dies with it, so the
    // view-model flag must be reset too (e.g. a network disconnect routing
    // to NetError while the modal was up).
    if (view_model_.interrupt_modal_visible()) {
        view_model_.dismiss_interrupt_confirm();
    }
    interrupt_modal_ = nullptr;
    modal_prev_focus_ = nullptr;

    lv_screen_load(next_screen->root());
    current_screen_ = std::move(next_screen);
}

// ---- Esc-key convention (Phase 7, App Submission Guide) ----

void ScreenManager::attach_esc_handling(lv_indev_t* indev) {
    if (indev) {
        lv_indev_add_event_cb(indev, &ScreenManager::indev_key_cb, LV_EVENT_KEY, this);
    }
}

void ScreenManager::indev_key_cb(lv_event_t* event) {
    auto* manager = static_cast<ScreenManager*>(lv_event_get_user_data(event));
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    if (manager && indev) {
        manager->on_indev_key(indev);
    }
}

void ScreenManager::on_indev_key(lv_indev_t* indev) {
    const uint32_t key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    const auto page_now = has_loaded_page_ ? loaded_page_ : view_model_.current_page();

    if (pressed) {
        if (key == LV_KEY_ESC) {
            if (!esc_down_) {
                esc_down_ = true;
                esc_long_fired_ = false;
                esc_indev_ = indev;
                esc_page_ = page_now;
                esc_pressed_at_ = lv_tick_get();
            } else if (indev == esc_indev_ && !esc_long_fired_ &&
                       lv_tick_elaps(esc_pressed_at_) >= kEscLongPressMs) {
                // The device keypad indev is in POLL mode, so LV_EVENT_KEY
                // keeps firing (~every read period) while the key stays
                // held -- this branch is what turns a held Esc into the
                // safe-exit path without waiting for the release.
                esc_long_fired_ = true;
                handle_safe_exit();
            }
        } else if (key == LV_KEY_HOME) {
            if (!home_down_) {
                home_down_ = true;
                home_indev_ = indev;
                handle_safe_exit();
            }
        } else if (indev == esc_indev_) {
            // A different key on the same keyboard while Esc tracking was
            // active: abandon the tracking so chorded presses never
            // register as a short Esc.
            esc_down_ = false;
            esc_indev_ = nullptr;
        }
    } else {
        // Release -- but only from the indev that pressed Esc. Other keypad
        // indevs report RELEASED on every idle read (see screen_manager.h);
        // treating those as the Esc release fired a "short Esc" while the
        // key was still held, then re-armed on the next poll and fired
        // again on the screen it had just navigated to -- e.g. Result ->
        // title -> straight out to the launcher. The SDL desktop driver's
        // synthetic release read reports key==0, so the key value itself is
        // not checked, only which indev it came from.
        if (esc_down_ && indev == esc_indev_) {
            const bool was_short =
                !esc_long_fired_ && lv_tick_elaps(esc_pressed_at_) < kEscLongPressMs;
            const bool same_page = page_now == esc_page_;
            esc_down_ = false;
            esc_long_fired_ = false;
            esc_indev_ = nullptr;
            if (was_short && same_page) {
                handle_short_esc();
            }
        }
        if (home_down_ && indev == home_indev_) {
            home_down_ = false;
            home_indev_ = nullptr;
        }
    }
}

void ScreenManager::arm_title_quit() {
    view_model_.set_title_quit_armed(true);
    if (title_quit_timer_) {
        lv_timer_reset(title_quit_timer_);
        return;
    }
    title_quit_timer_ = lv_timer_create(&ScreenManager::title_quit_timeout_cb,
                                        kTitleDoubleEscWindowMs, this);
    lv_timer_set_repeat_count(title_quit_timer_, 1);
}

void ScreenManager::disarm_title_quit() {
    if (title_quit_timer_) {
        lv_timer_delete(title_quit_timer_);
        title_quit_timer_ = nullptr;
    }
    view_model_.set_title_quit_armed(false);
}

void ScreenManager::title_quit_timeout_cb(lv_timer_t* timer) {
    auto* manager = static_cast<ScreenManager*>(lv_timer_get_user_data(timer));
    if (!manager) {
        return;
    }
    // repeat_count 1: LVGL deletes the timer itself after this call.
    manager->title_quit_timer_ = nullptr;
    manager->view_model_.set_title_quit_armed(false);
}

void ScreenManager::handle_short_esc() {
    // Modal first: short Esc while the interrupt-confirm overlay is up
    // closes the overlay (App Submission Guide: "short Esc ... closes a
    // modal") without touching the match underneath.
    if (view_model_.interrupt_modal_visible()) {
        view_model_.dismiss_interrupt_confirm();
        return;
    }

    const auto page = has_loaded_page_ ? loaded_page_ : view_model_.current_page();
    switch (page) {
        case model::AppPage::Menu:
            // Root screen: "one level up" from the title is the launcher's
            // home screen. Require a double Esc so a single Esc that just
            // returned the user to the title can't also exit; the first
            // one swaps the Menu hint to "Press Esc again to quit".
            if (view_model_.title_quit_armed()) {
                disarm_title_quit();
                handle_safe_exit();
            } else {
                arm_title_quit();
            }
            break;
        case model::AppPage::Settings:
        case model::AppPage::Difficulty:
        case model::AppPage::NetTransport:
        case model::AppPage::NetError:
        case model::AppPage::Result:
            // Same transition as each screen's on-screen Back/OK/Title
            // button (show_page(Menu) tears down network state if any).
            view_model_.show_page(model::AppPage::Menu);
            break;
        case model::AppPage::NetworkMenu:
        case model::AppPage::NetAntennaConfirm:
            // Antenna check: back WITHOUT persisting the LoRa choice.
            view_model_.show_page(model::AppPage::NetTransport);
            break;
        case model::AppPage::NetLobby:
        case model::AppPage::NetJoin:
            view_model_.cancel_network();  // back to the Host/Join menu
            break;
        case model::AppPage::NetDirectIp:
            view_model_.show_page(model::AppPage::NetJoin);
            break;
        case model::AppPage::Placement:
        case model::AppPage::Handover:
        case model::AppPage::Battle:
            if (view_model_.is_match_interruptible()) {
                view_model_.request_interrupt_confirm();
            } else {
                view_model_.show_page(model::AppPage::Menu);
            }
            break;
    }
}

void ScreenManager::handle_safe_exit() {
    view_model_.request_safe_quit();
}

void ScreenManager::interrupt_modal_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (!manager) {
        return;
    }
    if (lv_subject_get_int(subject)) {
        manager->show_interrupt_modal();
    } else {
        manager->hide_interrupt_modal();
    }
}

void ScreenManager::show_interrupt_modal() {
    if (interrupt_modal_) {
        return;
    }
    lv_obj_t* parent = current_screen();
    if (!parent) {
        parent = lv_screen_active();
    }
    if (!parent) {
        return;
    }

    modal_prev_focus_ = lv_group_get_focused(input_group_);

    auto* overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    auto* box = lv_obj_create(overlay);
    lv_obj_remove_style_all(box);
    const bool network = view_model_.match_mode() == viewmodel::MatchMode::Network;
    lv_obj_set_size(box, 250, network ? 104 : 88);
    lv_obj_center(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, view::palette(view_model_.is_dark_mode()).border, 0);
    reactive::bind_theme(box, view_model_.dark_mode_subject(), reactive::ThemeRole::Surface);

    auto* title = lv_label_create(box);
    lv_label_set_text(title, i18n::tr(i18n::StrId::ConfirmAbandonTitle));
    // Size 12, not 14: i18n::font_for_size() only has CJK (JA/ZH) glyph sets
    // baked at 12/18 (see i18n/fonts.cpp CjkFontSet) -- 14 silently fell back
    // to the Latin-only Montserrat bitmap font, rendering the JA/ZH title as
    // tofu boxes while every other label on this modal (size 12) was fine.
    lv_obj_set_style_text_font(title, i18n::font_for_size(12), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    reactive::bind_theme(title, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);

    if (network) {
        auto* hint = lv_label_create(box);
        lv_label_set_text(hint, i18n::tr(i18n::StrId::ConfirmAbandonNetHint));
        lv_obj_set_style_text_font(hint, i18n::font_for_size(12), 0);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 36);
        reactive::bind_theme(hint, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
    }

    auto* yes_button = lv_button_create(box);
    lv_obj_set_size(yes_button, 96, 24);
    lv_obj_align(yes_button, LV_ALIGN_BOTTOM_LEFT, 16, -10);
    reactive::bind_theme(yes_button, view_model_.dark_mode_subject(), reactive::ThemeRole::Button);
    auto* yes_label = lv_label_create(yes_button);
    lv_label_set_text(yes_label, i18n::tr(i18n::StrId::ConfirmYes));
    lv_obj_set_style_text_font(yes_label, i18n::font_for_size(12), 0);
    lv_obj_center(yes_label);

    auto* no_button = lv_button_create(box);
    lv_obj_set_size(no_button, 96, 24);
    lv_obj_align(no_button, LV_ALIGN_BOTTOM_RIGHT, -16, -10);
    reactive::bind_theme(no_button, view_model_.dark_mode_subject(), reactive::ThemeRole::Button);
    auto* no_label = lv_label_create(no_button);
    lv_label_set_text(no_label, i18n::tr(i18n::StrId::ConfirmNo));
    lv_obj_set_style_text_font(no_label, i18n::font_for_size(12), 0);
    lv_obj_center(no_label);

    lv_obj_add_event_cb(yes_button, modal_yes_cb, LV_EVENT_CLICKED, &view_model_);
    lv_obj_add_event_cb(no_button, modal_no_cb, LV_EVENT_CLICKED, &view_model_);
    lv_obj_add_event_cb(yes_button, modal_arrow_cb, LV_EVENT_KEY, no_button);
    lv_obj_add_event_cb(no_button, modal_arrow_cb, LV_EVENT_KEY, yes_button);
    lv_group_add_obj(input_group_, yes_button);
    lv_group_add_obj(input_group_, no_button);
    // Default focus on "No": Enter mashed through the modal must never
    // abandon a match by accident.
    lv_group_focus_obj(no_button);

    interrupt_modal_ = overlay;
}

void ScreenManager::hide_interrupt_modal() {
    if (!interrupt_modal_) {
        return;
    }
    if (lv_obj_is_valid(interrupt_modal_)) {
        // Deleting the overlay also removes its buttons from input_group_.
        lv_obj_delete(interrupt_modal_);
    }
    interrupt_modal_ = nullptr;
    if (modal_prev_focus_ && lv_obj_is_valid(modal_prev_focus_)) {
        lv_group_focus_obj(modal_prev_focus_);
    }
    modal_prev_focus_ = nullptr;
}

} // namespace app
