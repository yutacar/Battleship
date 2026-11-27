/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_join_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace screen {
namespace {

struct JoinUi {
    viewmodel::BaseViewModel* view_model = nullptr;
    lv_obj_t* list = nullptr;             ///< container for discovered-host buttons
    lv_obj_t* empty_label = nullptr;      ///< "Searching..." placeholder
    lv_observer_t* hosts_observer = nullptr;
};

void host_button_cb(lv_event_t* event) {
    auto* ui = static_cast<JoinUi*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (!ui || !ui->view_model || !button) {
        return;
    }
    // The host's index in net_hosts() rides in the button user_data. The
    // list is rebuilt on every host-list change, so an index captured at
    // build time always matches the current vector.
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button));
    ui->view_model->join_host_at(static_cast<size_t>(index));
}

void rebuild_host_list(JoinUi* ui) {
    if (!ui || !ui->view_model || !ui->list) {
        return;
    }
    auto& view_model = *ui->view_model;
    // Transport-agnostic peer list (Phase 6): WiFi -> UDP discovery hosts,
    // BLE -> scan results. join_host_at() accepts the same index space.
    const size_t peer_count = view_model.net_peer_count();

    // Keep keyboard focus on the same row across rebuilds: the LoRa list is
    // rebuilt whenever a peer's RSSI changes, which must not yank focus.
    int focused_row = -1;
    if (lv_obj_t* focused = lv_group_get_focused(BaseScreen::shared_group());
        focused && lv_obj_get_parent(focused) == ui->list) {
        focused_row = static_cast<int>(lv_obj_get_index(focused));
    }

    lv_obj_clean(ui->list);

    if (ui->empty_label) {
        if (peer_count == 0) {
            lv_obj_clear_flag(ui->empty_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui->empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (size_t i = 0; i < peer_count; ++i) {
        auto* button = lv_button_create(ui->list);
        lv_obj_set_size(button, 280, 22);
        reactive::bind_theme(button, view_model.dark_mode_subject(), reactive::ThemeRole::Button);

        auto* label = lv_label_create(button);
        lv_label_set_text(label, view_model.net_peer_label(i).c_str());
        lv_obj_set_style_text_font(label, i18n::font_for_size(12), 0);
        lv_obj_center(label);

        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(button, host_button_cb, LV_EVENT_CLICKED, ui);
        lv_group_add_obj(BaseScreen::shared_group(), button);
        BaseScreen::enable_arrow_focus_navigation(button);
    }

    if (focused_row >= 0 && peer_count > 0) {
        const auto row = std::min(static_cast<size_t>(focused_row), peer_count - 1);
        lv_group_focus_obj(lv_obj_get_child(ui->list, static_cast<int32_t>(row)));
    }
}

void hosts_dirty_cb(lv_observer_t* observer, lv_subject_t*) {
    rebuild_host_list(static_cast<JoinUi*>(lv_observer_get_user_data(observer)));
}

void list_delete_cb(lv_event_t* event) {
    auto* ui = static_cast<JoinUi*>(lv_event_get_user_data(event));
    if (ui && ui->hosts_observer) {
        // Same dangling-observer hazard as the board screens: the subject
        // outlives this screen, so the observer must be detached before the
        // JoinUi it captures is freed.
        lv_observer_remove(ui->hosts_observer);
    }
    delete ui;
}

void direct_ip_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->show_page(model::AppPage::NetDirectIp);
    }
}

void back_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->cancel_network();
    }
}

} // namespace

NetJoinScreen::NetJoinScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void NetJoinScreen::build_content(lv_obj_t* content) {
    auto* ui = new JoinUi();
    ui->view_model = &view_model();

    auto* title = lv_label_create(content);
    lv_label_set_text(title, i18n::tr(i18n::StrId::NetJoinTitle));
    lv_obj_set_style_text_font(title, i18n::font_for_size(18), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Discovered-host list (middle strip). Up to ~4 hosts fit; realistically
    // a LAN has one or two Battleship hosts at a time.
    auto* list = lv_obj_create(content);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), 100);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    ui->list = list;

    const bool ble = view_model().net_using_ble();
    const bool lora = view_model().net_using_lora();

    auto* empty = lv_label_create(content);
    lv_label_set_text(empty, i18n::tr(lora  ? i18n::StrId::NetJoinScanningLora
                                      : ble ? i18n::StrId::NetJoinScanningBle
                                            : i18n::StrId::NetJoinSearching));
    lv_obj_set_style_text_font(empty, i18n::font_for_size(12), 0);
    lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 64);
    reactive::bind_theme(empty, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    ui->empty_label = empty;

    // Bottom row: Direct IP fallback (WiFi only -- BLE/LoRa have no address
    // to type; scan results are the only join path) + Back.
    auto* buttons = lv_obj_create(content);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_PCT(100), 28);
    lv_obj_align(buttons, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* direct_button = nullptr;
    if (!ble && !lora) {
        direct_button = lv_button_create(buttons);
        lv_obj_set_size(direct_button, 120, 24);
        reactive::bind_theme(direct_button, view_model().dark_mode_subject(),
                             reactive::ThemeRole::Button);
        auto* direct_label = lv_label_create(direct_button);
        lv_label_set_text(direct_label, i18n::tr(i18n::StrId::NetDirectIpButton));
        lv_obj_set_style_text_font(direct_label, i18n::font_for_size(12), 0);
        lv_obj_center(direct_label);
        lv_obj_add_event_cb(direct_button, direct_ip_cb, LV_EVENT_CLICKED, &view_model());
        lv_group_add_obj(BaseScreen::shared_group(), direct_button);
        BaseScreen::enable_arrow_focus_navigation(direct_button);
    }

    auto* back_button = lv_button_create(buttons);
    lv_obj_set_size(back_button, 120, 24);
    reactive::bind_theme(back_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, i18n::tr(i18n::StrId::Back));
    lv_obj_set_style_text_font(back_label, i18n::font_for_size(12), 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_button, back_cb, LV_EVENT_CLICKED, &view_model());
    lv_group_add_obj(BaseScreen::shared_group(), back_button);
    BaseScreen::enable_arrow_focus_navigation(back_button);

    // The delete hook rides on the list container (always present).
    lv_obj_add_event_cb(list, list_delete_cb, LV_EVENT_DELETE, ui);
    ui->hosts_observer =
        reactive::observe(view_model().net_hosts_dirty_subject(), hosts_dirty_cb, ui);

    rebuild_host_list(ui);
    lv_group_focus_obj(direct_button ? direct_button : back_button);
}

} // namespace screen
