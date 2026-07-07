/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "placement_screen.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "bindings.h"
#include "ship_pattern.h"
#include "theme.h"

#include "i18n/fonts.h"
#include "i18n/strings.h"

#include <cstdio>
#include <string>

namespace screen {
namespace {

// 10x10 grid of 12px cells fits comfortably in the 320x170 frame alongside
// the ship list / status column to the right: 12*10 = 120px wide/tall board,
// leaving 320-120=200px (minus padding) for the side panel and 170-120=50px
// of headroom above/below for a status line. 12px cells were verified via
// screenshot to keep a 1px gap between cells visible while still leaving
// enough hit-testable area; 10px felt cramped, 14px would leave no room for
// the ship list without shrinking the board margins to zero.
constexpr int kCellSize = 12;
constexpr int kCellGap = 1;
constexpr int kBoardPixels = battleship::core::kBoardSize * kCellSize;

// Ship-list legend swatch: a small square next to each row showing that
// ship type's board pattern. 11px matches the board's own cell content size
// (kCellSize - kCellGap) so the fine stripe/dot texture renders identically
// to how it looks on the board itself.
constexpr int kSwatchSize = kCellSize - kCellGap;

struct CellVisual {
    lv_obj_t* cells[battleship::core::kBoardSize][battleship::core::kBoardSize] = {};
};

struct PlacementUi {
    viewmodel::BaseViewModel* view_model = nullptr;
    lv_obj_t* board = nullptr;
    CellVisual grid;
    lv_obj_t* ship_labels[battleship::core::kShipTypeCount] = {};
    lv_obj_t* ship_swatches[battleship::core::kShipTypeCount] = {};  ///< legend pattern sample per row
    lv_obj_t* status_label = nullptr;
    lv_obj_t* confirm_button = nullptr;
    lv_observer_t* board_dirty_observer = nullptr;
};

// Owned for the lifetime of the screen's content; freed via LV_EVENT_DELETE
// on the root board object (mirrors the lv_obj user-data lifetime pattern
// used elsewhere for event callbacks in this codebase).
PlacementUi* ui_for(lv_event_t* event) {
    return static_cast<PlacementUi*>(lv_event_get_user_data(event));
}

bool ship_is_placed(const battleship::core::Board& board, battleship::core::ShipType type) {
    for (const auto& ship : board.ships()) {
        if (ship.type() == type) {
            return true;
        }
    }
    return false;
}

void refresh_ui(PlacementUi* ui) {
    if (!ui || !ui->view_model) {
        return;
    }
    auto& view_model = *ui->view_model;
    const auto palette = view::palette(view_model.is_dark_mode());
    // placement_board() dispatches on match mode: the Game board for
    // hotseat/VsAi, the NetSession PlayerView's own board for network play.
    const auto& board = view_model.placement_board();
    const auto cursor = view_model.placement_cursor();
    const bool has_next = view_model.has_ship_to_place();
    const auto next_type = has_next ? view_model.next_ship_to_place() : battleship::core::ShipType::Destroyer;
    const int next_len = has_next ? battleship::core::ship_length(next_type) : 0;
    const auto orientation = view_model.placement_orientation();
    const bool preview_valid = has_next && view_model.is_placement_preview_valid();

    // Precompute which cells belong to the pending preview ship. The preview
    // always renders with next_type's pattern (the whole preview ship is one
    // type by definition), regardless of whether it's currently a valid or
    // invalid (out of bounds / overlap) placement.
    bool preview_cell[battleship::core::kBoardSize][battleship::core::kBoardSize] = {};
    const view::ShipPattern preview_pattern =
        has_next ? view::ship_pattern(next_type) : view::ShipPattern::None;
    if (has_next) {
        for (int i = 0; i < next_len; ++i) {
            const int px = orientation == battleship::core::Orientation::Horizontal ? cursor.x + i : cursor.x;
            const int py = orientation == battleship::core::Orientation::Vertical ? cursor.y + i : cursor.y;
            if (px >= 0 && px < battleship::core::kBoardSize && py >= 0 && py < battleship::core::kBoardSize) {
                preview_cell[py][px] = true;
            }
        }
    }

    // Precompute which cells are occupied by already-placed ships.
    bool placed_cell[battleship::core::kBoardSize][battleship::core::kBoardSize] = {};
    for (const auto& ship : board.ships()) {
        for (int i = 0; i < ship.length(); ++i) {
            const auto c = ship.cell_at(i);
            if (c.in_bounds()) {
                placed_cell[c.y][c.x] = true;
            }
        }
    }

    for (int y = 0; y < battleship::core::kBoardSize; ++y) {
        for (int x = 0; x < battleship::core::kBoardSize; ++x) {
            lv_obj_t* cell = ui->grid.cells[y][x];
            if (!cell) {
                continue;
            }
            // Distinct hues so "already placed" (green) never gets confused
            // with the pending preview ship: valid preview uses blue, an
            // invalid preview (out of bounds / overlap) uses red.
            lv_color_t color = palette.surface;
            view::ShipPattern pattern = view::ShipPattern::None;
            if (preview_cell[y][x]) {
                color = preview_valid ? lv_color_hex(0x2f7dd2) : lv_color_hex(0xd23c3c);
                pattern = preview_pattern;
            } else if (placed_cell[y][x]) {
                color = palette.primary;
                pattern = view::ship_pattern_at(board, battleship::core::Coord{x, y});
            }
            lv_obj_set_style_bg_color(cell, color, 0);
            view::apply_ship_pattern(cell, pattern, color, kCellSize - kCellGap);

            const bool is_cursor = (x == cursor.x && y == cursor.y);
            lv_obj_set_style_border_color(cell, is_cursor ? palette.text : palette.border, 0);
            lv_obj_set_style_border_width(cell, is_cursor ? 2 : 1, 0);
        }
    }

    for (int i = 0; i < battleship::core::kShipTypeCount; ++i) {
        const auto type = battleship::core::kAllShipTypes[i];
        lv_obj_t* label = ui->ship_labels[i];
        if (!label) {
            continue;
        }
        const bool placed = ship_is_placed(board, type);
        const bool is_next = has_next && type == next_type;

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%s(%d)", placed ? "[x] " : (is_next ? "> " : "  "),
                      i18n::ship_name(type).c_str(), battleship::core::ship_length(type));
        lv_label_set_text(label, buf);

        lv_color_t text_color = palette.text;
        if (placed) {
            text_color = palette.text_disabled;
        } else if (is_next) {
            text_color = palette.primary;
        }
        lv_obj_set_style_text_color(label, text_color, 0);

        // Legend swatch: same hull color + dot texture the board itself uses
        // for this ship type, so the list doubles as a pattern key ("what
        // does a Carrier cell look like") without a separate image asset.
        lv_obj_t* swatch = ui->ship_swatches[i];
        if (swatch) {
            const lv_color_t swatch_color = placed ? palette.text_disabled : palette.primary;
            lv_obj_set_style_bg_color(swatch, swatch_color, 0);
            lv_obj_set_style_border_color(swatch, palette.border, 0);
            view::apply_ship_pattern(swatch, view::ship_pattern(type), swatch_color, kSwatchSize);
        }
    }

    if (ui->status_label) {
        const i18n::StrId orientation_id = orientation == battleship::core::Orientation::Horizontal
                                                ? i18n::StrId::OrientationHorizontal
                                                : i18n::StrId::OrientationVertical;
        std::string text;
        if (has_next) {
            text = i18n::trf(i18n::StrId::PlacementShipStatus, i18n::ship_name(next_type).c_str(),
                             i18n::tr(orientation_id));
        } else {
            text = i18n::tr(i18n::StrId::PlacementFleetReady);
        }
        lv_label_set_text(ui->status_label, text.c_str());
    }

    if (ui->confirm_button) {
        if (board.is_fully_placed()) {
            lv_obj_clear_state(ui->confirm_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(ui->confirm_button, LV_STATE_DISABLED);
        }
    }
}

void board_dirty_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* ui = static_cast<PlacementUi*>(lv_observer_get_user_data(observer));
    refresh_ui(ui);
}

void confirm_cb(lv_event_t* event) {
    auto* ui = ui_for(event);
    if (ui && ui->view_model) {
        ui->view_model->advance_from_placement();
    }
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
            view_model.move_placement_cursor(-1, 0);
            break;
        case LV_KEY_RIGHT:
            view_model.move_placement_cursor(1, 0);
            break;
        case LV_KEY_UP:
            view_model.move_placement_cursor(0, -1);
            break;
        case LV_KEY_DOWN:
            view_model.move_placement_cursor(0, 1);
            break;
        case LV_KEY_ENTER:
            // Once the fleet is fully placed there's nothing left to commit;
            // treat Enter on the board the same as pressing "Confirm -> Next"
            // so players don't need a separate way to move focus off the
            // board (arrow keys drive the placement cursor, not group focus
            // navigation, while this screen is up).
            if (!view_model.has_ship_to_place()) {
                view_model.advance_from_placement();
            } else {
                view_model.commit_placement();
            }
            break;
        case LV_KEY_BACKSPACE:
            view_model.undo_last_placement();
            break;
        case 'r':
        case 'R':
            view_model.toggle_placement_orientation();
            break;
        case 'x':
        case 'X':
            view_model.randomize_placement();
            break;
        default:
            break;
    }
    // move_placement_cursor / toggle / commit / undo / randomize all bump
    // board_dirty_subject() internally, so the observer above will redraw.
}

void board_delete_cb(lv_event_t* event) {
    auto* ui = ui_for(event);
    if (ui && ui->board_dirty_observer) {
        // Without this, board_dirty_cb keeps a dangling pointer to this
        // soon-to-be-freed PlacementUi registered on BaseViewModel's
        // long-lived board_dirty_subject -- the next screen to bump the
        // subject would invoke a use-after-free callback and crash.
        lv_observer_remove(ui->board_dirty_observer);
    }
    delete ui;
}

std::string player_label(battleship::core::PlayerId player) {
    return i18n::trf(i18n::StrId::PlacementPlayerN, player == battleship::core::PlayerId::One ? 1 : 2);
}

} // namespace

PlacementScreen::PlacementScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void PlacementScreen::build_content(lv_obj_t* content) {
    auto* ui = new PlacementUi();
    ui->view_model = &view_model();

    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Title / player indicator (top strip) ----
    auto* title = lv_label_create(content);
    std::string title_text;
    if (view_model().match_mode() != viewmodel::MatchMode::Hotseat) {
        // Single-player / network: only the local human ever places here,
        // so a player number would be noise (the CPU's fleet is placed
        // automatically; the network opponent places on their own device).
        title_text = i18n::tr(i18n::StrId::PlacementPlaceYourFleetSolo);
    } else {
        title_text = player_label(view_model().placement_player());
        title_text += i18n::tr(i18n::StrId::PlacementPlaceYourFleetSuffix);
    }
    lv_label_set_text(title, title_text.c_str());
    lv_obj_set_style_text_font(title, i18n::font_for_size(12), 0);
    // y=0, not 2: the CJK FreeType fonts have a ~17px line box at nominal
    // 12px, so a title at y=2 could reach y=19 and clip into the board that
    // starts at y=18 (the same overlap bug the Battle screen's board labels
    // actually shipped with -- caught in JA screenshot review).
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 0);
    reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // ---- Board: focusable container + kBoardSize^2 cell children ----
    auto* board = lv_obj_create(content);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, kBoardPixels, kBoardPixels);
    lv_obj_align(board, LV_ALIGN_TOP_LEFT, 4, 18);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(board, LV_OPA_TRANSP, 0);
    ui->board = board;

    for (int y = 0; y < battleship::core::kBoardSize; ++y) {
        for (int x = 0; x < battleship::core::kBoardSize; ++x) {
            auto* cell = lv_obj_create(board);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, kCellSize - kCellGap, kCellSize - kCellGap);
            lv_obj_set_pos(cell, x * kCellSize, y * kCellSize);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_radius(cell, 0, 0);
            ui->grid.cells[y][x] = cell;
        }
    }

    lv_obj_add_event_cb(board, board_key_cb, LV_EVENT_KEY, ui);
    lv_obj_add_event_cb(board, board_delete_cb, LV_EVENT_DELETE, ui);
    lv_group_add_obj(BaseScreen::shared_group(), board);

    // ---- Side panel: ship list + status line ----
    auto* panel = lv_obj_create(content);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, view::kScreenWidth - kBoardPixels - 16, kBoardPixels);
    lv_obj_align(panel, LV_ALIGN_TOP_RIGHT, -4, 18);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 1, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < battleship::core::kShipTypeCount; ++i) {
        // Row = [pattern swatch][ship name label], laid out with flex-row so
        // the swatch stays a fixed kSwatchSize square and the label takes
        // the rest of the panel's width regardless of translated text length.
        auto* row = lv_obj_create(panel);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), kSwatchSize);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 3, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* swatch = lv_obj_create(row);
        lv_obj_remove_style_all(swatch);
        lv_obj_set_size(swatch, kSwatchSize, kSwatchSize);
        lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(swatch, 1, 0);
        lv_obj_set_style_radius(swatch, 0, 0);
        ui->ship_swatches[i] = swatch;

        auto* label = lv_label_create(row);
        lv_obj_set_style_text_font(label, i18n::font_for_size(12), 0);
        ui->ship_labels[i] = label;
    }

    // ---- Key guide: always-visible key help under the ship list (user
    // feedback: the R/X/Backspace shortcuts were undiscoverable without
    // reading the docs). Three short lines instead of one long one because
    // the EN text at Montserrat 12 doesn't fit the ~184px panel width on a
    // single line; rendered dimmed (text_opa) so it reads as help, not as
    // part of the game state. The arrow glyphs come from the Montserrat
    // FontAwesome range (LV_SYMBOL_*), which the CJK FreeType fonts fall
    // back to per-glyph, so no CJK font regen is needed for the arrows
    // themselves. ----
    const std::string guide_lines[] = {
        i18n::trf(i18n::StrId::KeyGuidePlacementMove,
                  LV_SYMBOL_LEFT LV_SYMBOL_UP LV_SYMBOL_DOWN LV_SYMBOL_RIGHT),
        i18n::tr(i18n::StrId::KeyGuidePlacementModify),
        i18n::tr(i18n::StrId::KeyGuidePlacementUndo),
    };
    for (const auto& line : guide_lines) {
        auto* guide = lv_label_create(panel);
        lv_label_set_text(guide, line.c_str());
        lv_obj_set_style_text_font(guide, i18n::font_for_size(12), 0);
        lv_obj_set_style_text_opa(guide, LV_OPA_70, 0);
        reactive::bind_theme(guide, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    }

    // ---- Bottom strip: current-ship/orientation status + confirm button ----
    auto* status = lv_label_create(content);
    lv_obj_set_style_text_font(status, i18n::font_for_size(12), 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    reactive::bind_theme(status, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    ui->status_label = status;

    auto* confirm_button = lv_button_create(content);
    lv_obj_set_size(confirm_button, 120, 22);
    lv_obj_align(confirm_button, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    reactive::bind_theme(confirm_button, view_model().dark_mode_subject(), reactive::ThemeRole::Button);
    auto* confirm_label = lv_label_create(confirm_button);
    lv_label_set_text(confirm_label, i18n::tr(i18n::StrId::PlacementConfirmNext));
    lv_obj_set_style_text_font(confirm_label, i18n::font_for_size(12), 0);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm_button, confirm_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_state(confirm_button, LV_STATE_DISABLED);
    lv_group_add_obj(BaseScreen::shared_group(), confirm_button);
    ui->confirm_button = confirm_button;

    ui->board_dirty_observer = reactive::observe(view_model().board_dirty_subject(), board_dirty_cb, ui);

    refresh_ui(ui);
    lv_group_focus_obj(board);
}

} // namespace screen
