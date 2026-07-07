/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "menu_screen.h"

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

using i18n::StrId;
using i18n::tr;

using MenuAction = void (*)(viewmodel::BaseViewModel&);

void start_hotseat(viewmodel::BaseViewModel& view_model) {
    view_model.start_hotseat_match();
}

void open_difficulty_select(viewmodel::BaseViewModel& view_model) {
    view_model.show_page(model::AppPage::Difficulty);
}

void open_network_play(viewmodel::BaseViewModel& view_model) {
    view_model.open_network_menu();
}

void open_settings(viewmodel::BaseViewModel& view_model) {
    view_model.show_page(model::AppPage::Settings);
}

struct MenuEntry {
    StrId label;
    bool enabled;
    MenuAction action;  ///< nullptr for disabled placeholders
};

constexpr std::array<MenuEntry, 4> kMenuEntries = {{
    {StrId::MenuTwoPlayerHotseat, true, &start_hotseat},
    {StrId::MenuOnePlayer, true, &open_difficulty_select},
    {StrId::MenuNetworkPlay, true, &open_network_play},
    {StrId::MenuSettings, true, &open_settings},
}};

void activate_cb(lv_event_t* event) {
    // Entry rides in the event user_data (points into the static constexpr
    // table, so its lifetime is unbounded); the BaseViewModel pointer rides
    // on the button object itself.
    auto* entry = static_cast<const MenuEntry*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_obj_get_user_data(button));
    if (entry && entry->action && view_model) {
        entry->action(*view_model);
    }
}

} // namespace

MenuScreen::MenuScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void MenuScreen::build_content(lv_obj_t* content) {
    // "BATTLESHIP" is the product's brand-name logo, kept in English in
    // every language (same treatment as the app_title()/window title -- see
    // PLAN.md Phase 5 notes: ship names are translated, but the game's own
    // name is not). Rendered as a pre-generated 3D ASCII-art PNG
    // (tools/generate_title_logo.py) rather than an lv_label: LVGL's
    // built-in fonts are proportional and have no bevel/shading support, so
    // the isometric-block look is produced offline with Pillow and shipped
    // as assets/images/title_logo.png. The outlined green-on-transparent
    // design reads on both the light (0xffffff) and dark (0x101014) theme
    // bodies, so no per-theme swap is needed here (unlike the label it
    // replaces, which used reactive::bind_theme for text color).
    auto* title = lv_image_create(content);
    auto logo_path = assets().resolve("images/title_logo.png");
    if (!logo_path.empty()) {
        // lv_image_set_src() lv_strdup()s file-path sources internally
        // (src/widgets/image/lv_image.c), so this local string does not
        // need to outlive the call.
        std::string logo_path_str = "A:" + logo_path.string();
        lv_image_set_src(title, logo_path_str.c_str());
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    } else {
        // Fallback in case the PNG asset failed to resolve (e.g. missing
        // from a custom deployment) -- keep the title readable instead of
        // showing a blank/broken image.
        lv_obj_del(title);
        title = lv_label_create(content);
        lv_label_set_text(title, "BATTLESHIP");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
        reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    }

    auto* menu = lv_obj_create(content);
    lv_obj_remove_style_all(menu);
    lv_obj_set_size(menu, LV_PCT(100), 96);
    lv_obj_align(menu, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(menu, 2, 0);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

    for (const auto& entry : kMenuEntries) {
        auto* button = lv_button_create(menu);
        lv_obj_set_size(button, 180, 20);
        reactive::bind_theme(button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);

        auto* label = lv_label_create(button);
        std::string text = tr(entry.label);
        if (!entry.enabled) {
            text += tr(StrId::ComingSoon);
        }
        lv_label_set_text(label, text.c_str());
        lv_obj_set_style_text_font(label, i18n::font_for_size(12), 0);
        lv_obj_center(label);

        if (entry.enabled) {
            lv_obj_set_user_data(button, &view_model());
            lv_obj_add_event_cb(button, activate_cb, LV_EVENT_CLICKED,
                                const_cast<MenuEntry*>(&entry));
            lv_group_add_obj(BaseScreen::shared_group(), button);
            BaseScreen::enable_arrow_focus_navigation(button);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
    }
}

} // namespace screen
