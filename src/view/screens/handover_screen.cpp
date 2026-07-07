/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "handover_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <string>

namespace screen {
namespace {

void confirm_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->confirm_handover();
    }
}

std::string player_label(battleship::core::PlayerId player) {
    return i18n::trf(i18n::StrId::PlacementPlayerN, player == battleship::core::PlayerId::One ? 1 : 2);
}

} // namespace

HandoverScreen::HandoverScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void HandoverScreen::build_content(lv_obj_t* content) {
    auto* group = lv_obj_create(content);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 10, 0);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(group);

    auto* headline = lv_label_create(group);
    const std::string text =
        i18n::trf(i18n::StrId::HandoverPassDeviceTo, player_label(view_model().upcoming_player()).c_str());
    lv_label_set_text(headline, text.c_str());
    lv_obj_set_style_text_font(headline, i18n::font_for_size(18), 0);
    reactive::bind_theme(headline, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* subtitle = lv_label_create(group);
    lv_label_set_text(subtitle, i18n::tr(i18n::StrId::HandoverDontPeek));
    lv_obj_set_style_text_font(subtitle, i18n::font_for_size(12), 0);
    reactive::bind_theme(subtitle, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* button = lv_button_create(group);
    lv_obj_set_size(button, 160, 24);
    reactive::bind_theme(button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* button_label = lv_label_create(button);
    lv_label_set_text(button_label, i18n::tr(i18n::StrId::HandoverReady));
    lv_obj_set_style_text_font(button_label, i18n::font_for_size(12), 0);
    lv_obj_center(button_label);
    lv_obj_add_event_cb(button, confirm_cb, LV_EVENT_CLICKED, &view_model());
    lv_group_add_obj(BaseScreen::shared_group(), button);

    // Always-visible key guide (user feedback): the Ready button already
    // names Enter, but Esc (abandon-match confirm) was undiscoverable.
    // Dimmed via text_opa so it reads as help, not as a game prompt.
    auto* guide = lv_label_create(content);
    lv_label_set_text(guide, i18n::tr(i18n::StrId::KeyGuideHandover));
    lv_obj_set_style_text_font(guide, i18n::font_for_size(12), 0);
    lv_obj_set_style_text_opa(guide, LV_OPA_70, 0);
    lv_obj_align(guide, LV_ALIGN_BOTTOM_MID, 0, -2);
    reactive::bind_theme(guide, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace screen
