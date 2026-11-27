/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_lobby_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

namespace screen {
namespace {

void cancel_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->cancel_network();
    }
}

} // namespace

NetLobbyScreen::NetLobbyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void NetLobbyScreen::build_content(lv_obj_t* content) {
    const bool hosting = view_model().net_is_host();

    auto* title = lv_label_create(content);
    lv_label_set_text(title, i18n::tr(hosting ? i18n::StrId::NetLobbyHostingTitle
                                              : i18n::StrId::NetLobbyJoiningTitle));
    lv_obj_set_style_text_font(title, i18n::font_for_size(18), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Host: "Advertising as BSHIP-<host>". Client: "Connecting to <peer>".
    // Kept live via the subject so e.g. an error message can replace it.
    auto* status = lv_label_create(content);
    // Wrapped (and centered): "Recruiting over LoRa as BSHIP-<host> ..." can
    // exceed one line with a long host name.
    lv_label_set_long_mode(status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(status, 300);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status, i18n::font_for_size(12), 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, -14);
    reactive::bind_theme(status, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    reactive::bind_label_text(status, view_model().net_status_subject());

    auto* waiting = lv_label_create(content);
    lv_label_set_text(waiting, i18n::tr(hosting ? i18n::StrId::NetLobbyWaitingForOpponent
                                                : i18n::StrId::NetLobbyWaitingForHost));
    lv_obj_set_style_text_font(waiting, i18n::font_for_size(12), 0);
    lv_obj_align(waiting, LV_ALIGN_CENTER, 0, 8);
    reactive::bind_theme(waiting, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* spinner = lv_spinner_create(content);
    lv_obj_set_size(spinner, 22, 22);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 34);

    auto* cancel_button = lv_button_create(content);
    lv_obj_set_size(cancel_button, 120, 24);
    lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_MID, 0, -8);
    reactive::bind_theme(cancel_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* cancel_label = lv_label_create(cancel_button);
    lv_label_set_text(cancel_label, i18n::tr(i18n::StrId::Cancel));
    lv_obj_set_style_text_font(cancel_label, i18n::font_for_size(12), 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_button, cancel_cb, LV_EVENT_CLICKED, &view_model());
    lv_group_add_obj(BaseScreen::shared_group(), cancel_button);
    lv_group_focus_obj(cancel_button);
}

} // namespace screen
