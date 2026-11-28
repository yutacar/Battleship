/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_transport_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <array>
#include <string>

namespace screen {
namespace {

using i18n::NetTransportPref;
using i18n::StrId;
using i18n::tr;

struct TransportEntry {
    StrId label;
    NetTransportPref pref;   ///< meaningful only when selectable
    bool selectable;         ///< false renders a disabled "(Coming soon)" row
};

constexpr std::array<TransportEntry, 2> kTransportEntries = {{
    {StrId::NetTransportBle, NetTransportPref::Ble, true},    // default, top row
    // Enabled on every build, independent of the BLE device-only gating
    // below (desktop builds use the UDP radio simulator).
    {StrId::NetTransportLora, NetTransportPref::Lora, true},
}};

void select_cb(lv_event_t* event) {
    auto* entry = static_cast<const TransportEntry*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_obj_get_user_data(button));
    if (entry && entry->selectable && view_model) {
        view_model->select_net_transport(entry->pref);
    }
}

void back_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->show_page(model::AppPage::Menu);
    }
}

} // namespace

NetTransportScreen::NetTransportScreen(viewmodel::BaseViewModel& view_model,
                                       app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void NetTransportScreen::build_content(lv_obj_t* content) {
    auto* title = lv_label_create(content);
    lv_label_set_text(title, tr(StrId::NetTransportTitle));
    lv_obj_set_style_text_font(title, i18n::font_for_size(18), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* menu = lv_obj_create(content);
    lv_obj_remove_style_all(menu);
    lv_obj_set_size(menu, LV_PCT(100), 130);
    lv_obj_align(menu, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(menu, 4, 0);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

    const NetTransportPref current = view_model().net_transport_pref();
    lv_obj_t* focus_target = nullptr;

    for (const auto& entry : kTransportEntries) {
        // BLE is only usable on Linux/BlueZ builds; elsewhere the row stays
        // visible (so the desktop developer sees the product's real default)
        // but is disabled with a "(device only)" suffix.
        const bool is_ble = entry.selectable && entry.pref == NetTransportPref::Ble;
        const bool enabled =
            entry.selectable && (!is_ble || viewmodel::BaseViewModel::ble_available());

        auto* button = lv_button_create(menu);
        lv_obj_set_size(button, 240, 24);
        reactive::bind_theme(button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);

        auto* label = lv_label_create(button);
        std::string text = tr(entry.label);
        if (!entry.selectable) {
            text += tr(StrId::ComingSoon);
        } else if (is_ble && !enabled) {
            text += tr(StrId::NetTransportDeviceOnly);
        }
        lv_label_set_text(label, text.c_str());
        lv_obj_set_style_text_font(label, i18n::font_for_size(12), 0);
        lv_obj_center(label);

        if (enabled) {
            if (entry.pref == current) {
                // "Currently persisted default" cue, same treatment as the
                // language rows on the Settings screen.
                lv_obj_add_state(button, LV_STATE_CHECKED);
                focus_target = button;
            }
            lv_obj_set_user_data(button, &view_model());
            lv_obj_add_event_cb(button, select_cb, LV_EVENT_CLICKED,
                                const_cast<TransportEntry*>(&entry));
            lv_group_add_obj(BaseScreen::shared_group(), button);
            BaseScreen::enable_arrow_focus_navigation(button);
            if (!focus_target) {
                focus_target = button;  // first enabled row (BLE where available)
            }
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
    }

    auto* back_button = lv_button_create(menu);
    lv_obj_set_size(back_button, 240, 22);
    reactive::bind_theme(back_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, tr(StrId::Back));
    lv_obj_set_style_text_font(back_label, i18n::font_for_size(12), 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_button, back_cb, LV_EVENT_CLICKED, &view_model());
    lv_group_add_obj(BaseScreen::shared_group(), back_button);
    BaseScreen::enable_arrow_focus_navigation(back_button);

    if (focus_target) {
        lv_group_focus_obj(focus_target);
    }
}

} // namespace screen
