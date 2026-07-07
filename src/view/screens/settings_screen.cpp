/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <array>

namespace screen {
namespace {

using i18n::Lang;
using i18n::StrId;
using i18n::tr;

struct LanguageEntry {
    StrId label;
    Lang lang;
};

constexpr std::array<LanguageEntry, 3> kLanguageEntries = {{
    {StrId::SettingsLanguageEnglish, Lang::EN},
    {StrId::SettingsLanguageJapanese, Lang::JA},
    {StrId::SettingsLanguageChinese, Lang::ZH},
}};

void select_language_cb(lv_event_t* event) {
    auto* entry = static_cast<const LanguageEntry*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_obj_get_user_data(button));
    if (entry && view_model) {
        // set_language() rebuilds the current screen synchronously (see
        // ScreenManager::reload_current_screen()), so this SettingsScreen
        // instance (and this very button) is destroyed by the time this
        // callback returns -- do nothing with `entry`/`button` afterward.
        view_model->set_language(entry->lang);
    }
}

void back_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->show_page(model::AppPage::Menu);
    }
}

} // namespace

SettingsScreen::SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void SettingsScreen::build_content(lv_obj_t* content) {
    auto* title = lv_label_create(content);
    lv_label_set_text(title, tr(StrId::SettingsTitle));
    lv_obj_set_style_text_font(title, i18n::font_for_size(18), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* language_label = lv_label_create(content);
    lv_label_set_text(language_label, tr(StrId::SettingsLanguageLabel));
    lv_obj_set_style_text_font(language_label, i18n::font_for_size(12), 0);
    lv_obj_align(language_label, LV_ALIGN_TOP_MID, 0, 34);
    reactive::bind_theme(language_label, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* menu = lv_obj_create(content);
    lv_obj_remove_style_all(menu);
    lv_obj_set_size(menu, LV_PCT(100), 96);
    lv_obj_align(menu, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(menu, 3, 0);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

    const Lang current = view_model().language();

    for (const auto& entry : kLanguageEntries) {
        auto* button = lv_button_create(menu);
        lv_obj_set_size(button, 180, 20);
        reactive::bind_theme(button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
        if (entry.lang == current) {
            // Visual "currently selected" cue: checked state uses the
            // theme's usual checked/primary styling with no extra style
            // rules needed here.
            lv_obj_add_state(button, LV_STATE_CHECKED);
        }

        auto* label = lv_label_create(button);
        lv_label_set_text(label, tr(entry.label));
        // Each language's own label must render in ITS OWN script (e.g.
        // "日本語" needs the JA CJK font regardless of the currently active
        // UI language) so the option is legible even before switching to
        // it -- font_for_language (not font_for_size, which follows the
        // globally active language) picks the right font per label.
        lv_obj_set_style_text_font(label, i18n::font_for_language_and_size(entry.lang, 12), 0);
        lv_obj_center(label);

        lv_obj_set_user_data(button, &view_model());
        lv_obj_add_event_cb(button, select_language_cb, LV_EVENT_CLICKED,
                            const_cast<LanguageEntry*>(&entry));
        lv_group_add_obj(BaseScreen::shared_group(), button);
        BaseScreen::enable_arrow_focus_navigation(button);
    }

    auto* back_button = lv_button_create(menu);
    lv_obj_set_size(back_button, 180, 20);
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
