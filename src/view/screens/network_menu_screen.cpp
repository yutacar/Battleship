/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "network_menu_screen.h"

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

using MenuAction = void (*)(viewmodel::BaseViewModel&);

void host_game(viewmodel::BaseViewModel& view_model) { view_model.start_network_host(); }
void join_game(viewmodel::BaseViewModel& view_model) { view_model.start_network_join(); }
void back_to_menu(viewmodel::BaseViewModel& view_model) {
    // Back re-enters the connection-type select (Phase 6 flow: Menu ->
    // NetTransport -> NetworkMenu), not the main menu.
    view_model.show_page(model::AppPage::NetTransport);
}

struct Entry {
    i18n::StrId label;
    MenuAction action;
};

constexpr std::array<Entry, 3> kEntries = {{
    {i18n::StrId::NetworkHostGame, &host_game},
    {i18n::StrId::NetworkJoinGame, &join_game},
    {i18n::StrId::Back, &back_to_menu},
}};

void activate_cb(lv_event_t* event) {
    auto* entry = static_cast<const Entry*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_obj_get_user_data(button));
    if (entry && entry->action && view_model) {
        entry->action(*view_model);
    }
}

} // namespace

NetworkMenuScreen::NetworkMenuScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void NetworkMenuScreen::build_content(lv_obj_t* content) {
    auto* title = lv_label_create(content);
    lv_label_set_text(title, i18n::tr(i18n::StrId::NetworkPlayTitle));
    lv_obj_set_style_text_font(title, i18n::font_for_size(18), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* hint = lv_label_create(content);
    // Restates the selected method ("Method: Bluetooth (BLE)" / LoRa). A BLE
    // preference on a build without BLE still reads BLE; Host/Join then
    // report that it is unavailable.
    const std::string hint_text = i18n::trf(
        i18n::StrId::NetTransportMethodLine,
        i18n::tr(view_model().net_using_lora() ? i18n::StrId::NetTransportLora
                                               : i18n::StrId::NetTransportBle));
    lv_label_set_text(hint, hint_text.c_str());
    lv_obj_set_style_text_font(hint, i18n::font_for_size(12), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 34);
    reactive::bind_theme(hint, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* menu = lv_obj_create(content);
    lv_obj_remove_style_all(menu);
    lv_obj_set_size(menu, LV_PCT(100), 96);
    lv_obj_align(menu, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(menu, 4, 0);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

    for (const auto& entry : kEntries) {
        auto* button = lv_button_create(menu);
        lv_obj_set_size(button, 180, 24);
        reactive::bind_theme(button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);

        auto* label = lv_label_create(button);
        lv_label_set_text(label, i18n::tr(entry.label));
        lv_obj_set_style_text_font(label, i18n::font_for_size(12), 0);
        lv_obj_center(label);

        lv_obj_set_user_data(button, &view_model());
        lv_obj_add_event_cb(button, activate_cb, LV_EVENT_CLICKED, const_cast<Entry*>(&entry));
        lv_group_add_obj(BaseScreen::shared_group(), button);
        BaseScreen::enable_arrow_focus_navigation(button);
    }
}

} // namespace screen
