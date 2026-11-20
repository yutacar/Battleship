/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "difficulty_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <array>

namespace screen {
namespace {

using battleship::core::ai::Difficulty;
using i18n::StrId;
using i18n::tr;

struct DifficultyEntry {
    StrId label;
    StrId description;
    Difficulty difficulty;
};

constexpr std::array<DifficultyEntry, 3> kDifficultyEntries = {{
    {StrId::DifficultyEasyLabel, StrId::DifficultyEasyDesc, Difficulty::Easy},
    {StrId::DifficultyNormalLabel, StrId::DifficultyNormalDesc, Difficulty::Normal},
    {StrId::DifficultyHardLabel, StrId::DifficultyHardDesc, Difficulty::Hard},
}};

void select_cb(lv_event_t* event) {
    auto* entry = static_cast<const DifficultyEntry*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_obj_get_user_data(button));
    if (entry && view_model) {
        view_model->start_ai_match(entry->difficulty);
    }
}

void back_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->show_page(model::AppPage::Menu);
    }
}

} // namespace

DifficultyScreen::DifficultyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void DifficultyScreen::build_content(lv_obj_t* content) {
    auto* title = lv_label_create(content);
    lv_label_set_text(title, tr(StrId::DifficultyTitle));
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

    for (const auto& entry : kDifficultyEntries) {
        auto* button = lv_button_create(menu);
        lv_obj_set_size(button, 280, 24);
        reactive::bind_theme(button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);

        auto* label = lv_label_create(button);
        char text[96];
        lv_snprintf(text, sizeof(text), "%s - %s", tr(entry.label), tr(entry.description));
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, i18n::font_for_size(12), 0);
        lv_obj_center(label);

        lv_obj_set_user_data(button, &view_model());
        lv_obj_add_event_cb(button, select_cb, LV_EVENT_CLICKED,
                            const_cast<DifficultyEntry*>(&entry));
        lv_group_add_obj(BaseScreen::shared_group(), button);
        BaseScreen::enable_arrow_focus_navigation(button);
    }

    auto* back_button = lv_button_create(menu);
    lv_obj_set_size(back_button, 280, 22);
    reactive::bind_theme(back_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, tr(StrId::Back));
    lv_obj_set_style_text_font(back_label, i18n::font_for_size(12), 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_button, back_cb, LV_EVENT_CLICKED, &view_model());
    lv_group_add_obj(BaseScreen::shared_group(), back_button);
    BaseScreen::enable_arrow_focus_navigation(back_button);
}

} // namespace screen
