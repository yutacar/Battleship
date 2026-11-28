/*
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_antenna_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <string>

namespace screen {
namespace {

void yes_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->confirm_lora_antenna();
    }
}

} // namespace

NetAntennaScreen::NetAntennaScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void NetAntennaScreen::build_content(lv_obj_t* content) {
    auto* title = lv_label_create(content);
    const std::string title_text =
        std::string(LV_SYMBOL_WARNING " ") + i18n::tr(i18n::StrId::NetTransportLora);
    lv_label_set_text(title, title_text.c_str());
    lv_obj_set_style_text_font(title, i18n::font_for_size(18), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // The question is the longest LoRa string (two to three lines in every
    // language), so it wraps inside a fixed-width box.
    auto* question = lv_label_create(content);
    lv_label_set_long_mode(question, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(question, 296);
    lv_label_set_text(question, i18n::tr(i18n::StrId::NetLoraAntennaQuestion));
    lv_obj_set_style_text_font(question, i18n::font_for_size(12), 0);
    lv_obj_set_style_text_align(question, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(question, LV_ALIGN_TOP_MID, 0, 36);
    reactive::bind_theme(question, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* yes_button = lv_button_create(content);
    lv_obj_set_size(yes_button, 140, 24);
    lv_obj_align(yes_button, LV_ALIGN_BOTTOM_MID, 0, -26);
    reactive::bind_theme(yes_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* yes_label = lv_label_create(yes_button);
    lv_label_set_text(yes_label, i18n::tr(i18n::StrId::ConfirmYes));
    lv_obj_set_style_text_font(yes_label, i18n::font_for_size(12), 0);
    lv_obj_center(yes_label);
    lv_obj_add_event_cb(yes_button, yes_cb, LV_EVENT_CLICKED, &view_model());
    lv_group_add_obj(BaseScreen::shared_group(), yes_button);
    lv_group_focus_obj(yes_button);

    // Always-visible key guide, dimmed like the other screens' guides.
    auto* guide = lv_label_create(content);
    lv_label_set_text(guide, i18n::tr(i18n::StrId::NetLoraAntennaConfirm));
    lv_obj_set_style_text_font(guide, i18n::font_for_size(12), 0);
    lv_obj_set_style_text_opa(guide, LV_OPA_70, 0);
    lv_obj_align(guide, LV_ALIGN_BOTTOM_MID, 0, -4);
    reactive::bind_theme(guide, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace screen
