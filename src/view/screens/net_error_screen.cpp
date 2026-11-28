/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_error_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <string>

namespace screen {
namespace {

void ok_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        // show_page(Menu) tears down the whole network stack as a side
        // effect (see BaseViewModel::show_page).
        view_model->show_page(model::AppPage::Menu);
    }
}

} // namespace

NetErrorScreen::NetErrorScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void NetErrorScreen::build_content(lv_obj_t* content) {
    auto* title = lv_label_create(content);
    const std::string title_text = std::string(LV_SYMBOL_WARNING " ") + i18n::tr(i18n::StrId::NetErrorTitle);
    lv_label_set_text(title, title_text.c_str());
    lv_obj_set_style_text_font(title, i18n::font_for_size(18), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // "Connection lost" etc. -- whatever the viewmodel put there when it
    // routed to this page.
    auto* message = lv_label_create(content);
    // Wrapped: the LoRa "Cap not found" message spans two lines.
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(message, 296);
    lv_obj_set_style_text_font(message, i18n::font_for_size(12), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_CENTER, 0, -4);
    reactive::bind_theme(message, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    reactive::bind_label_text(message, view_model().net_status_subject());

    auto* ok_button = lv_button_create(content);
    lv_obj_set_size(ok_button, 140, 24);
    lv_obj_align(ok_button, LV_ALIGN_BOTTOM_MID, 0, -12);
    reactive::bind_theme(ok_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* ok_label = lv_label_create(ok_button);
    lv_label_set_text(ok_label, i18n::tr(i18n::StrId::NetErrorOkBackToTitle));
    lv_obj_set_style_text_font(ok_label, i18n::font_for_size(12), 0);
    lv_obj_center(ok_label);
    lv_obj_add_event_cb(ok_button, ok_cb, LV_EVENT_CLICKED, &view_model());
    lv_group_add_obj(BaseScreen::shared_group(), ok_button);
    lv_group_focus_obj(ok_button);
}

} // namespace screen
