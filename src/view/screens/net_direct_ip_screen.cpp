/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_direct_ip_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

namespace screen {
namespace {

struct DirectIpUi {
    viewmodel::BaseViewModel* view_model = nullptr;
    lv_obj_t* text_area = nullptr;
    lv_obj_t* hint_label = nullptr;
};

void connect_now(DirectIpUi* ui) {
    if (!ui || !ui->view_model || !ui->text_area) {
        return;
    }
    const char* text = lv_textarea_get_text(ui->text_area);
    if (!ui->view_model->join_direct(text) && ui->hint_label) {
        lv_label_set_text(ui->hint_label, i18n::tr(i18n::StrId::NetDirectIpInvalidAddress));
    }
}

void connect_cb(lv_event_t* event) {
    connect_now(static_cast<DirectIpUi*>(lv_event_get_user_data(event)));
}

void back_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->show_page(model::AppPage::NetJoin);
    }
}

void text_area_event_cb(lv_event_t* event) {
    // LV_EVENT_READY fires when Enter is pressed inside a one-line textarea:
    // treat it exactly like the Connect button so keyboard-only players
    // never need to move focus off the field.
    connect_now(static_cast<DirectIpUi*>(lv_event_get_user_data(event)));
}

void ui_delete_cb(lv_event_t* event) {
    delete static_cast<DirectIpUi*>(lv_event_get_user_data(event));
}

} // namespace

NetDirectIpScreen::NetDirectIpScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void NetDirectIpScreen::build_content(lv_obj_t* content) {
    auto* ui = new DirectIpUi();
    ui->view_model = &view_model();

    auto* title = lv_label_create(content);
    lv_label_set_text(title, i18n::tr(i18n::StrId::NetDirectIpTitle));
    lv_obj_set_style_text_font(title, i18n::font_for_size(18), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* hint = lv_label_create(content);
    lv_label_set_text(hint, i18n::tr(i18n::StrId::NetDirectIpHint));
    lv_obj_set_style_text_font(hint, i18n::font_for_size(12), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 32);
    reactive::bind_theme(hint, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    ui->hint_label = hint;

    auto* text_area = lv_textarea_create(content);
    lv_textarea_set_one_line(text_area, true);
    lv_textarea_set_placeholder_text(text_area, i18n::tr(i18n::StrId::NetDirectIpPlaceholder));
    // Only characters that can appear in "ip[:port]" -- anything else typed
    // on the 46-key keyboard is silently ignored by LVGL.
    lv_textarea_set_accepted_chars(text_area, "0123456789.:");
    lv_textarea_set_max_length(text_area, 21);  // "255.255.255.255:65535"
    lv_obj_set_size(text_area, 220, 30);
    lv_obj_align(text_area, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_text_font(text_area, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(text_area, text_area_event_cb, LV_EVENT_READY, ui);
    lv_group_add_obj(BaseScreen::shared_group(), text_area);
    ui->text_area = text_area;
    // Ownership hook for the shared DirectIpUi rides on the textarea.
    lv_obj_add_event_cb(text_area, ui_delete_cb, LV_EVENT_DELETE, ui);

    auto* buttons = lv_obj_create(content);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_PCT(100), 28);
    lv_obj_align(buttons, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    auto* connect_button = lv_button_create(buttons);
    lv_obj_set_size(connect_button, 120, 24);
    reactive::bind_theme(connect_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, i18n::tr(i18n::StrId::Connect));
    lv_obj_set_style_text_font(connect_label, i18n::font_for_size(12), 0);
    lv_obj_center(connect_label);
    lv_obj_add_event_cb(connect_button, connect_cb, LV_EVENT_CLICKED, ui);
    lv_group_add_obj(BaseScreen::shared_group(), connect_button);
    BaseScreen::enable_arrow_focus_navigation(connect_button);

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

    lv_group_focus_obj(text_area);
}

} // namespace screen
