/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "result_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <cstdio>
#include <string>

namespace screen {
namespace {

void rematch_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->start_new_match();
    }
}

/// Network-mode mash guard (see below): re-enables the Title button a
/// moment after the Result screen appears.
struct TitleGuard {
    lv_timer_t* timer = nullptr;   ///< nulled by the callback before LVGL auto-deletes it
    lv_obj_t* button = nullptr;
};

void title_guard_timer_cb(lv_timer_t* timer) {
    auto* guard = static_cast<TitleGuard*>(lv_timer_get_user_data(timer));
    if (!guard) {
        return;
    }
    // One-shot (repeat_count 1): LVGL deletes the timer right after this
    // returns, so drop the handle first or the button's LV_EVENT_DELETE
    // cleanup would double-delete it later (same pattern as
    // BaseViewModel::ai_timer_cb).
    guard->timer = nullptr;
    if (guard->button) {
        lv_obj_clear_state(guard->button, LV_STATE_DISABLED);
    }
}

void title_guard_delete_cb(lv_event_t* event) {
    auto* guard = static_cast<TitleGuard*>(lv_event_get_user_data(event));
    if (guard) {
        if (guard->timer) {
            lv_timer_delete(guard->timer);  // screen died before the guard elapsed
        }
        delete guard;
    }
}

void to_title_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->show_page(model::AppPage::Menu);
    }
}

/// `line_id` is one of the ResultHitRateLine* template entries -- the
/// "who" label is baked into the template per language (word order around
/// the numbers differs across EN/JA/ZH, so the whole line is one template
/// rather than a separately translated label + fixed English scaffold).
std::string hit_rate_line(i18n::StrId line_id, const battleship::core::PlayerSnapshot& snapshot) {
    const double rate = snapshot.shots_fired > 0
                             ? (100.0 * static_cast<double>(snapshot.hits_recorded) /
                                static_cast<double>(snapshot.shots_fired))
                             : 0.0;
    return i18n::trf(line_id, snapshot.hits_recorded, snapshot.shots_fired, rate);
}

} // namespace

ResultScreen::ResultScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void ResultScreen::build_content(lv_obj_t* content) {
    const auto snapshot = view_model().result_snapshot();
    const bool player_one_won = snapshot.winner == battleship::core::PlayerId::One;
    const bool vs_ai = view_model().match_mode() == viewmodel::MatchMode::VsAi;
    const bool network = view_model().match_mode() == viewmodel::MatchMode::Network;

    auto* group = lv_obj_create(content);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 3, 0);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(group, LV_ALIGN_TOP_MID, 0, 6);

    auto* headline = lv_label_create(group);
    i18n::StrId headline_id;
    if (vs_ai) {
        headline_id = player_one_won ? i18n::StrId::ResultYouWin : i18n::StrId::ResultCpuWins;
    } else if (network) {
        // player_one is always the local player in a network match.
        headline_id = player_one_won ? i18n::StrId::ResultYouWin : i18n::StrId::ResultOpponentWins;
    } else {
        headline_id = player_one_won ? i18n::StrId::ResultPlayer1Wins : i18n::StrId::ResultPlayer2Wins;
    }
    lv_label_set_text(headline, i18n::tr(headline_id));
    lv_obj_set_style_text_font(headline, i18n::font_for_size(18), 0);
    reactive::bind_theme(headline, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* p1_stats = lv_label_create(group);
    lv_label_set_text(p1_stats,
                      hit_rate_line((vs_ai || network) ? i18n::StrId::ResultHitRateLineYou
                                                        : i18n::StrId::ResultHitRateLineP1,
                                    snapshot.player_one)
                          .c_str());
    lv_obj_set_style_text_font(p1_stats, i18n::font_for_size(12), 0);
    reactive::bind_theme(p1_stats, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* p2_stats = lv_label_create(group);
    lv_label_set_text(p2_stats,
                      hit_rate_line(vs_ai ? i18n::StrId::ResultHitRateLineCpu
                                          : (network ? i18n::StrId::ResultHitRateLineOpponent
                                                      : i18n::StrId::ResultHitRateLineP2),
                                    snapshot.player_two)
                          .c_str());
    lv_obj_set_style_text_font(p2_stats, i18n::font_for_size(12), 0);
    reactive::bind_theme(p2_stats, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* turns = lv_label_create(group);
    lv_label_set_text(turns, i18n::trf(i18n::StrId::ResultTotalTurns, snapshot.turn_number).c_str());
    lv_obj_set_style_text_font(turns, i18n::font_for_size(12), 0);
    reactive::bind_theme(turns, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    if (vs_ai) {
        // Phase 3b overall-review line: request_commentary(MatchOver) was
        // fired from BaseViewModel right before navigating here (see
        // fire_at_cursor()/play_ai_turn()), but LLM generation can still be
        // in flight (measured 400-900ms) by the time this screen builds --
        // so bind directly to the subject rather than reading a snapshot of
        // it now, and simply show nothing until/unless it arrives (matches
        // the "生成が間に合った場合のみ" requirement in PLAN.md §2.4).
        // Fixed 2-line height (not LV_SIZE_CONTENT) so an overlong comment
        // clips instead of wrapping to as many lines as needed and pushing
        // the Rematch/Title buttons off the bottom of the 170px screen
        // (same class of bug as the Battle screen's commentary row -- see
        // its comment for how this was caught via screenshot review).
        //
        // CLIP, not DOTS (Phase 5): this line previously used
        // LV_LABEL_LONG_MODE_DOTS, but NOTES.md §11.4 documents DOTS sending
        // the LVGL render thread into an infinite loop when the text width
        // lands near the label width -- and flagged this exact label as
        // "same treatment if it reproduces". With JA/ZH commentary the label
        // is far likelier to hit that width-boundary case (CJK glyphs are
        // uniformly wide), so the switch to CLIP is made preemptively rather
        // than waiting for a hang in the field.
        auto* review = lv_label_create(group);
        lv_obj_set_size(review, view::kScreenWidth - 24, 30);
        lv_label_set_long_mode(review, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(review, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(review, i18n::font_for_size(12), 0);
        reactive::bind_theme(review, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
        reactive::bind_label_text(review, view_model().commentary_text_subject());
    }

    auto* buttons = lv_obj_create(content);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_PCT(100), 28);
    lv_obj_align(buttons, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    if (!network) {
        // No Rematch in network mode: restarting a remote match would need a
        // fresh READY/turn-order re-sync protocol round (out of Phase 4's
        // scope) -- both players return to the title and host/join again.
        auto* rematch_button = lv_button_create(buttons);
        lv_obj_set_size(rematch_button, 100, 24);
        reactive::bind_theme(rematch_button, view_model().dark_mode_subject(),
                             reactive::ThemeRole::Button);
        auto* rematch_label = lv_label_create(rematch_button);
        lv_label_set_text(rematch_label, i18n::tr(i18n::StrId::ResultRematch));
        lv_obj_set_style_text_font(rematch_label, i18n::font_for_size(12), 0);
        lv_obj_center(rematch_label);
        lv_obj_add_event_cb(rematch_button, rematch_cb, LV_EVENT_CLICKED, &view_model());
        lv_group_add_obj(BaseScreen::shared_group(), rematch_button);
        BaseScreen::enable_arrow_focus_navigation(rematch_button);
    }

    auto* title_button = lv_button_create(buttons);
    lv_obj_set_size(title_button, 100, 24);
    reactive::bind_theme(title_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* title_label = lv_label_create(title_button);
    lv_label_set_text(title_label, i18n::tr(i18n::StrId::ResultTitle));
    lv_obj_set_style_text_font(title_label, i18n::font_for_size(12), 0);
    lv_obj_center(title_label);
    lv_obj_add_event_cb(title_button, to_title_cb, LV_EVENT_CLICKED, &view_model());
    lv_group_add_obj(BaseScreen::shared_group(), title_button);
    BaseScreen::enable_arrow_focus_navigation(title_button);

    if (network) {
        // Mash guard: a network match ends on the OPPONENT's schedule, so a
        // player still hammering the fire key when the final RESULT lands
        // would instantly "click" the freshly-focused Title button and dump
        // themselves to the menu without ever seeing the result. Keep the
        // button disabled for a moment; Title is the only exit from a
        // network result so it's also the auto-focused object.
        lv_obj_add_state(title_button, LV_STATE_DISABLED);
        auto* guard = new TitleGuard();
        guard->button = title_button;
        guard->timer = lv_timer_create(title_guard_timer_cb, 2500, guard);
        lv_timer_set_repeat_count(guard->timer, 1);
        lv_obj_add_event_cb(title_button, title_guard_delete_cb, LV_EVENT_DELETE, guard);
    }
}

} // namespace screen
