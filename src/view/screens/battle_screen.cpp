/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "battle_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "ship_pattern.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <string>

namespace screen {
namespace {

// Layout (320x170), redesigned per user feedback ("show live score during
// battle; the own-fleet board may shrink to half size to make room; key
// guide always visible; the board labels must not overlap the grids"):
//
//   y 0..17   top strip: "Targets" label | screen title | "Your Fleet" label
//   y 18..138 left:  tracking board, 10 x 12px cells (unchanged size -- it
//                    is the interactive board and needs the largest cells)
//             right: own fleet board at HALF cell size (10 x 7px = 70px,
//                    display-only), live score lines to its left, and the
//                    key-guide lines underneath both
//   y 139..   commentary row (VsAi/network) + status line (unchanged)
//
// The top strip is a full 18px tall because the CJK FreeType fonts have a
// ~17px line box at nominal size 12: the previous layout put the board-label
// row at y=4 with the board top at y=16, which the JA/ZH labels
// ("攻撃盤"/"攻击盘") visibly clipped into (docs/screenshots/battle_dense_ja
// .png pre-fix) -- labels now sit at y=0 and every board/panel starts at 18.
constexpr int kCellSize = 12;
constexpr int kCellGap = 1;
constexpr int kBoardPixels = battleship::core::kBoardSize * kCellSize;

// Own-fleet board: half-size cells (user-approved shrink). 7px cells keep a
// 1px gap + 6px of content, which the fine ship textures (see
// ship_pattern.h) and the hit/miss overlay colors were verified to survive
// at -- see docs/screenshots/battle_dense_*.png.
constexpr int kFleetCellSize = 7;
constexpr int kFleetBoardPixels = battleship::core::kBoardSize * kFleetCellSize;

constexpr int kTopStrip = 18;      ///< label row height / boards' top edge
constexpr int kLeftMargin = 4;
constexpr int kRightMargin = 4;
/// Right column: everything between the tracking board and the right edge.
constexpr int kRightColumnLeft = kLeftMargin + kBoardPixels + 6;  // 130
constexpr int kFleetLeft = view::kScreenWidth - kRightMargin - kFleetBoardPixels;  // 246
/// Per-line pitch for the score/guide labels (CJK line box at 12px is ~17px).
constexpr int kLinePitch = 17;

struct BoardVisual {
    lv_obj_t* cells[battleship::core::kBoardSize][battleship::core::kBoardSize] = {};
};

struct BattleUi {
    viewmodel::BaseViewModel* view_model = nullptr;
    lv_obj_t* tracking_board = nullptr;  ///< left: focusable, cursor-driven
    BoardVisual tracking_grid;
    BoardVisual fleet_grid;  ///< right: own fleet, display-only, half-size cells
    lv_obj_t* stats_you = nullptr;   ///< live score: own hits/shots (rate)
    lv_obj_t* stats_foe = nullptr;   ///< live score: opponent hits/shots (rate)
    lv_obj_t* stats_sunk = nullptr;  ///< live score: enemy ships sunk / own lost
    lv_observer_t* board_dirty_observer = nullptr;
};

BattleUi* ui_for(lv_event_t* event) {
    return static_cast<BattleUi*>(lv_event_get_user_data(event));
}

lv_color_t tracking_cell_color(const view::ThemePalette& palette, battleship::core::CellMark mark) {
    switch (mark) {
        case battleship::core::CellMark::Unknown:
            return palette.surface;
        case battleship::core::CellMark::Miss:
            return palette.text_disabled;
        case battleship::core::CellMark::Hit:
            return lv_color_hex(0xe8a33d);  // orange
        case battleship::core::CellMark::Sunk:
            return lv_color_hex(0xd23c3c);  // red
    }
    return palette.surface;
}

std::string stats_line(const char* who, const battleship::core::PlayerSnapshot& snapshot) {
    const double rate = snapshot.shots_fired > 0
                             ? (100.0 * static_cast<double>(snapshot.hits_recorded) /
                                static_cast<double>(snapshot.shots_fired))
                             : 0.0;
    return i18n::trf(i18n::StrId::BattleStatsLine, who, snapshot.hits_recorded,
                     snapshot.shots_fired, rate);
}

void refresh_ui(BattleUi* ui) {
    if (!ui || !ui->view_model) {
        return;
    }
    auto& view_model = *ui->view_model;
    const auto palette = view::palette(view_model.is_dark_mode());
    // viewing_player(): the current-turn player in hotseat (this screen is
    // rebuilt on every Handover -> Battle transition), but always the human
    // (One) in vs-AI mode, where this same screen instance stays loaded for
    // the whole match and must never flip to the CPU's perspective while
    // the CPU's reply is pending.
    // battle_tracking_board()/battle_own_board() dispatch on match mode:
    // Game boards for hotseat/VsAi, the NetSession PlayerView's boards for
    // network play (Phase 4).
    const auto& tracking = view_model.battle_tracking_board();
    const auto& fleet = view_model.battle_own_board();
    const auto cursor = view_model.battle_cursor();

    // Precompute own-fleet ship occupancy for the right-hand board.
    bool has_ship[battleship::core::kBoardSize][battleship::core::kBoardSize] = {};
    for (const auto& ship : fleet.ships()) {
        for (int i = 0; i < ship.length(); ++i) {
            const auto c = ship.cell_at(i);
            if (c.in_bounds()) {
                has_ship[c.y][c.x] = true;
            }
        }
    }

    for (int y = 0; y < battleship::core::kBoardSize; ++y) {
        for (int x = 0; x < battleship::core::kBoardSize; ++x) {
            const battleship::core::Coord c{x, y};

            // Left: tracking board (attacks on the opponent).
            lv_obj_t* tcell = ui->tracking_grid.cells[y][x];
            if (tcell) {
                const auto mark = tracking.mark_at(c);
                lv_obj_set_style_bg_color(tcell, tracking_cell_color(palette, mark), 0);

                const bool is_cursor = (x == cursor.x && y == cursor.y);
                lv_obj_set_style_border_color(tcell, is_cursor ? palette.text : palette.border, 0);
                lv_obj_set_style_border_width(tcell, is_cursor ? 2 : 1, 0);
            }

            // Right: own fleet board (ships + damage taken). Ships keep
            // their per-type fine texture even once hit/sunk (drawn darker
            // relative to whatever base color the cell has, so it reads on
            // top of the red hit overlay too) -- otherwise a hit would erase
            // the only cue for which ship's hull a cell belongs to, right
            // when the player most wants to see which ship is taking damage.
            lv_obj_t* fcell = ui->fleet_grid.cells[y][x];
            if (fcell) {
                const bool shot_at = fleet.was_shot_at(c);
                const bool ship_here = has_ship[y][x];
                lv_color_t color = palette.surface;
                if (ship_here && shot_at) {
                    color = lv_color_hex(0xd23c3c);  // own ship, hit
                } else if (ship_here) {
                    color = palette.primary;  // own ship, undamaged
                } else if (shot_at) {
                    color = palette.text_disabled;  // opponent's miss
                }
                lv_obj_set_style_bg_color(fcell, color, 0);
                const view::ShipPattern pattern =
                    ship_here ? view::ship_pattern_at(fleet, c) : view::ShipPattern::None;
                view::apply_ship_pattern(fcell, pattern, color, kFleetCellSize - kCellGap);
            }
        }
    }

    // ---- Live score panel (user feedback: show the Result-style stats
    // during the battle, not only at the end). result_snapshot() already
    // works mid-match in every mode: hotseat/VsAi return the live Game
    // snapshot, network synthesizes both sides from the local PlayerView
    // (player_one is always the local player there). ----
    if (ui->stats_you && ui->stats_foe && ui->stats_sunk) {
        const auto snapshot = view_model.result_snapshot();
        // player_one is the local player in VsAi (human) and network modes;
        // hotseat renders the current-turn player's perspective.
        const bool self_is_one =
            view_model.match_mode() != viewmodel::MatchMode::Hotseat ||
            view_model.viewing_player() == battleship::core::PlayerId::One;
        const auto& self = self_is_one ? snapshot.player_one : snapshot.player_two;
        const auto& foe = self_is_one ? snapshot.player_two : snapshot.player_one;

        // "CPU" is kept as-is in all three languages (matches the existing
        // BattleCpuShotResult / ResultHitRateLineCpu wording).
        const char* foe_label = view_model.match_mode() == viewmodel::MatchMode::VsAi
                                     ? "CPU"
                                     : i18n::tr(i18n::StrId::NetFoe);
        lv_label_set_text(ui->stats_you,
                          stats_line(i18n::tr(i18n::StrId::NetYou), self).c_str());
        lv_label_set_text(ui->stats_foe, stats_line(foe_label, foe).c_str());

        const int enemy_sunk = static_cast<int>(self.enemy_ships_sunk.size());
        const int own_lost = battleship::core::kShipTypeCount -
                             static_cast<int>(self.own_ships_remaining.size());
        lv_label_set_text(
            ui->stats_sunk,
            i18n::trf(i18n::StrId::BattleStatsSunkLine, enemy_sunk, own_lost).c_str());
    }
}

void board_dirty_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* ui = static_cast<BattleUi*>(lv_observer_get_user_data(observer));
    refresh_ui(ui);
}

void board_key_cb(lv_event_t* event) {
    auto* ui = ui_for(event);
    if (!ui || !ui->view_model) {
        return;
    }
    auto& view_model = *ui->view_model;
    const uint32_t key = lv_event_get_key(event);

    switch (key) {
        case LV_KEY_LEFT:
            view_model.move_battle_cursor(-1, 0);
            break;
        case LV_KEY_RIGHT:
            view_model.move_battle_cursor(1, 0);
            break;
        case LV_KEY_UP:
            view_model.move_battle_cursor(0, -1);
            break;
        case LV_KEY_DOWN:
            view_model.move_battle_cursor(0, 1);
            break;
        case LV_KEY_ENTER:
            view_model.fire_at_cursor();
            break;
        default:
            break;
    }
    // move_battle_cursor / fire_at_cursor bump board_dirty_subject()
    // internally (when they change state), so the observer above will
    // redraw. fire_at_cursor also drives page navigation to Handover/Result
    // on its own; this screen does not need to react to that directly.
}

void board_delete_cb(lv_event_t* event) {
    auto* ui = ui_for(event);
    if (ui && ui->board_dirty_observer) {
        // Without this, the observer callback (board_dirty_cb) keeps a
        // dangling pointer to this soon-to-be-freed BattleUi registered on
        // BaseViewModel's long-lived board_dirty_subject -- the next screen
        // that bumps the subject (including this same screen type on a
        // later turn) would then invoke a use-after-free callback and crash
        // (observed as SIGSEGV when firing a shot after this screen had
        // already been rebuilt once).
        lv_observer_remove(ui->board_dirty_observer);
    }
    delete ui;
}

} // namespace

BattleScreen::BattleScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void BattleScreen::build_content(lv_obj_t* content) {
    auto* ui = new BattleUi();
    ui->view_model = &view_model();

    // NOTE: `content` is BaseScreen's root object, already sized to the full
    // 320x170, non-scrollable, and theme-bound (ThemeRole::Screen background)
    // by BaseScreen::init(). Do NOT lv_obj_remove_style_all() it here: doing
    // so strips the theme-applied background style and renders this screen on
    // a black void with unreadable labels (this actually shipped once and was
    // caught in the docs/screenshots/ review).

    // ---- Top strip (y 0..17): board labels + screen title. All on one row
    // at y=0 so the CJK line box (~17px at nominal 12) ends above the boards
    // at y=18 -- the old layout drew the labels at y=4 over a board top at
    // y=16 and clipped (see the layout comment at the top of this file). ----
    auto* left_label = lv_label_create(content);
    lv_label_set_text(left_label, i18n::tr(i18n::StrId::BattleTargetsLabel));
    lv_obj_set_style_text_font(left_label, i18n::font_for_size(12), 0);
    lv_obj_align(left_label, LV_ALIGN_TOP_LEFT, kLeftMargin, 0);
    reactive::bind_theme(left_label, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Title. Hotseat: rebuilt on every Handover -> Battle transition, so the
    // shooter computed at build time is always current. Vs AI: the screen
    // stays loaded for the whole match (no Handover), so the title is a
    // static "You vs CPU" and per-shot feedback lives in the bottom status
    // line instead. Centered (nudged 8px left) between the "Targets" label
    // (ends ~x=60) and the "Your Fleet" label (starts at x=246); all title
    // variants are kept short enough to fit that span.
    auto* title = lv_label_create(content);
    if (view_model().match_mode() == viewmodel::MatchMode::Network) {
        // Per-turn state ("Your turn - fire!" / "Waiting for opponent...")
        // lives in the info row above the status line instead.
        lv_label_set_text(title, i18n::tr(i18n::StrId::BattleTitleNet));
    } else if (view_model().match_mode() == viewmodel::MatchMode::VsAi) {
        // The chosen difficulty is shown in the status line (set by
        // start_new_match) instead.
        lv_label_set_text(title, i18n::tr(i18n::StrId::BattleTitleVsCpu));
    } else {
        const bool p1_turn = view_model().game().current_turn() == battleship::core::PlayerId::One;
        lv_label_set_text(title, i18n::tr(p1_turn ? i18n::StrId::BattleTitleP1Fire
                                                   : i18n::StrId::BattleTitleP2Fire));
    }
    lv_obj_set_style_text_font(title, i18n::font_for_size(12), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, -8, 0);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* right_label = lv_label_create(content);
    lv_label_set_text(right_label, i18n::tr(i18n::StrId::BattleYourFleetLabel));
    lv_obj_set_style_text_font(right_label, i18n::font_for_size(12), 0);
    lv_obj_align(right_label, LV_ALIGN_TOP_LEFT, kFleetLeft, 0);
    reactive::bind_theme(right_label, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // ---- Left board: tracking board (focusable, cursor-driven) ----
    auto* tracking_board = lv_obj_create(content);
    lv_obj_remove_style_all(tracking_board);
    lv_obj_set_size(tracking_board, kBoardPixels, kBoardPixels);
    lv_obj_align(tracking_board, LV_ALIGN_TOP_LEFT, kLeftMargin, kTopStrip);
    lv_obj_clear_flag(tracking_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tracking_board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(tracking_board, LV_OPA_TRANSP, 0);
    ui->tracking_board = tracking_board;

    for (int y = 0; y < battleship::core::kBoardSize; ++y) {
        for (int x = 0; x < battleship::core::kBoardSize; ++x) {
            auto* cell = lv_obj_create(tracking_board);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, kCellSize - kCellGap, kCellSize - kCellGap);
            lv_obj_set_pos(cell, x * kCellSize, y * kCellSize);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_radius(cell, 0, 0);
            ui->tracking_grid.cells[y][x] = cell;
        }
    }

    lv_obj_add_event_cb(tracking_board, board_key_cb, LV_EVENT_KEY, ui);
    lv_obj_add_event_cb(tracking_board, board_delete_cb, LV_EVENT_DELETE, ui);
    lv_group_add_obj(BaseScreen::shared_group(), tracking_board);

    // ---- Right board: own fleet at half-size cells (display-only, no
    // cursor). Shrunk from 12px to 7px cells (user-approved) to free the
    // rest of the right column for the live score panel + key guide. ----
    auto* fleet_board = lv_obj_create(content);
    lv_obj_remove_style_all(fleet_board);
    lv_obj_set_size(fleet_board, kFleetBoardPixels, kFleetBoardPixels);
    lv_obj_align(fleet_board, LV_ALIGN_TOP_LEFT, kFleetLeft, kTopStrip);
    lv_obj_clear_flag(fleet_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(fleet_board, LV_OPA_TRANSP, 0);

    const auto initial_palette = view::palette(view_model().is_dark_mode());
    for (int y = 0; y < battleship::core::kBoardSize; ++y) {
        for (int x = 0; x < battleship::core::kBoardSize; ++x) {
            auto* cell = lv_obj_create(fleet_board);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, kFleetCellSize - kCellGap, kFleetCellSize - kCellGap);
            lv_obj_set_pos(cell, x * kFleetCellSize, y * kFleetCellSize);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, initial_palette.border, 0);
            lv_obj_set_style_radius(cell, 0, 0);
            ui->fleet_grid.cells[y][x] = cell;
        }
    }

    // ---- Live score panel: between the tracking board and the fleet board
    // (x 130..242), three lines. Text set by refresh_ui() on every
    // board-dirty bump so it tracks each shot in real time. Fixed width +
    // CLIP so a worst-case line ("Opponent 17/100 (17%)") can never wrap
    // into the fleet board (DOTS is banned: see NOTES.md §11.4 render-thread
    // hang). ----
    const int stats_width = kFleetLeft - kRightColumnLeft - 4;
    lv_obj_t** stats_labels[] = {&ui->stats_you, &ui->stats_foe, &ui->stats_sunk};
    for (int i = 0; i < 3; ++i) {
        auto* stats = lv_label_create(content);
        lv_label_set_text(stats, "");
        lv_obj_set_size(stats, stats_width, 16);
        lv_label_set_long_mode(stats, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_font(stats, i18n::font_for_size(12), 0);
        lv_obj_align(stats, LV_ALIGN_TOP_LEFT, kRightColumnLeft, kTopStrip + i * kLinePitch);
        reactive::bind_theme(stats, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
        *stats_labels[i] = stats;
    }

    // ---- Key guide: always-visible key help (user feedback), dimmed via
    // text_opa so it reads as help rather than game state. Two lines in the
    // right column under the fleet board / score panel. The arrow glyphs are
    // LV_SYMBOL_* (Montserrat FontAwesome range); the CJK FreeType fonts
    // fall back to Montserrat per-glyph, so they render in JA/ZH too. ----
    const int guide_top = kTopStrip + kFleetBoardPixels + 8;  // 96
    const std::string guide_lines[] = {
        i18n::trf(i18n::StrId::KeyGuideBattleAim,
                  LV_SYMBOL_LEFT LV_SYMBOL_UP LV_SYMBOL_DOWN LV_SYMBOL_RIGHT),
        i18n::tr(i18n::StrId::KeyGuideBattleEsc),
    };
    for (int i = 0; i < 2; ++i) {
        auto* guide = lv_label_create(content);
        lv_label_set_text(guide, guide_lines[i].c_str());
        lv_obj_set_style_text_font(guide, i18n::font_for_size(12), 0);
        lv_obj_set_style_text_opa(guide, LV_OPA_70, 0);
        lv_obj_align(guide, LV_ALIGN_TOP_LEFT, kRightColumnLeft, guide_top + i * kLinePitch);
        reactive::bind_theme(guide, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    }

    // ---- Bottom strip ----
    // Two stacked rows fit in the ~32px left below the boards (170 - 138):
    // the commentary row (Phase 3b, VsAi only) sits just under the boards,
    // the shot-status row stays pinned to the very bottom edge as before.
    // Kept as two separate labels/subjects rather than sharing one line --
    // NOTES.md §8.5 flagged that the status line already gets rewritten on
    // every shot ("You ... | CPU ..."), so a shared line would mean the
    // commentary and the shot result constantly overwrite each other.
    constexpr int kCommentaryY = kTopStrip + kBoardPixels + 1;  // just under the boards

    auto* status = lv_label_create(content);
    lv_label_set_text(status, "");
    lv_obj_set_style_text_font(status, i18n::font_for_size(12), 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -1);
    reactive::bind_theme(status, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    reactive::bind_label_text(status, view_model().status_text_subject());

    if (view_model().match_mode() != viewmodel::MatchMode::Hotseat) {
        // VsAi: LLM/template commentary. Network: turn indicator +
        // confirmed-sunk list (same subject, see BaseViewModel::
        // net_update_battle_status). Commentary is otherwise a VsAi-only
        // feature (PLAN.md §2.4); hotseat never populates
        // commentary_text_subject(), so this row would just sit empty
        // forever there -- skip creating it entirely in that mode.
        auto* commentary = lv_label_create(content);
        lv_label_set_text(commentary, "");
        // LV_LABEL_LONG_MODE_DOTS only single-line-truncates (rather than
        // wrapping to a 2nd line that would overlap the status row below)
        // when the label has a FIXED height, not LV_SIZE_CONTENT -- a plain
        // lv_obj_set_width() alone still let long comments wrap to 2+ lines
        // and collide with the status row (caught via screenshot review of
        // an actual LLM-generated multi-sentence comment).
        // 16px tall (was 14 for Montserrat-only): the CJK FreeType fonts
        // have a taller line box at nominal 12px and the glyphs' bottom row
        // of pixels was at risk of clipping. 16px still clears the status
        // row pinned to the very bottom edge (kCommentaryY=139, 139+16=155,
        // status row occupies ~155-169 and its glyph ink starts a few px
        // into that box).
        lv_obj_set_size(commentary, view::kScreenWidth - 2 * kLeftMargin, 16);
        lv_obj_set_style_text_font(commentary, i18n::font_for_size(12), 0);
        // CLIP, not DOTS: LV_LABEL_LONG_MODE_DOTS sent the LVGL software
        // render thread into an infinite loop inside
        // lv_draw_label_iterate_characters() when this line's text width
        // landed near the label width (reproduced with "Your turn - fire!
        // Enemy sunk: Carrier, Battleship" during Phase 4's 2-process runs;
        // main thread then blocks forever in lv_thread_sync_wait, freezing
        // the whole app -- caught via `sample` of the hung process). Plain
        // clipping loses the "..." but cannot hang.
        lv_label_set_long_mode(commentary, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_align(commentary, LV_ALIGN_TOP_MID, 0, kCommentaryY);
        reactive::bind_theme(commentary, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
        reactive::bind_label_text(commentary, view_model().commentary_text_subject());
    }

    ui->board_dirty_observer = reactive::observe(view_model().board_dirty_subject(), board_dirty_cb, ui);

    refresh_ui(ui);
    lv_group_focus_obj(tracking_board);
}

} // namespace screen
