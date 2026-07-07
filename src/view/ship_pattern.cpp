/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ship_pattern.h"

namespace view {
namespace {

constexpr const char* kRectUserDataTag = "ship_pattern_rect";

/// One texture primitive, in cell-local pixel coordinates.
struct PatternRect {
    int16_t x, y, w, h;
};

/// Upper bound on primitives per cell: Checker on an 11px cell emits 18 1px
/// dots (6 staggered rows x 3 dots); stripes emit at most 5; everything else
/// fewer. 24 leaves headroom for slightly larger future cell sizes without
/// risking silent truncation at today's sizes.
constexpr int kMaxPatternRects = 24;

/// Computes the primitive list for `pattern` inside an n x n cell interior.
/// Returns the number of rects written to `out` (<= kMaxPatternRects).
///
/// All shapes are derived from `n` at 1px granularity so the same recipe
/// scales from the 11px board cells down to the 6px half-size fleet-board
/// cells:
///   HStripes: 1px rows at y = 1,3,5,...  (odd rows -> never touches the
///             cell's top/bottom edge, keeps a clean 2px pitch)
///   VStripes: same, on columns.
///   Checker:  1px dots, rows every 2px, columns every 4px, odd rows offset
///             by 2px -> a staggered fine diamond/checker weave (~15% fill).
///   CenterDot: one centered square, 3px on >=10px cells / 2px on >=6px /
///             1px below that -- scales with the cell but stays "a single
///             sparse dot", clearly lighter than every other pattern.
int build_pattern_rects(ShipPattern pattern, int n, PatternRect* out) {
    int count = 0;
    auto push = [&](int x, int y, int w, int h) {
        if (count < kMaxPatternRects) {
            out[count++] = {static_cast<int16_t>(x), static_cast<int16_t>(y),
                            static_cast<int16_t>(w), static_cast<int16_t>(h)};
        }
    };

    switch (pattern) {
        case ShipPattern::HStripes:
            for (int y = 1; y < n; y += 2) {
                push(0, y, n, 1);
            }
            break;
        case ShipPattern::VStripes:
            for (int x = 1; x < n; x += 2) {
                push(x, 0, 1, n);
            }
            break;
        case ShipPattern::Checker:
            for (int y = 0; y < n; y += 2) {
                const int offset = (y % 4 == 0) ? 0 : 2;
                for (int x = offset; x < n; x += 4) {
                    push(x, y, 1, 1);
                }
            }
            break;
        case ShipPattern::CenterDot: {
            const int d = n >= 10 ? 3 : (n >= 6 ? 2 : 1);
            push((n - d) / 2, (n - d) / 2, d, d);
            break;
        }
        case ShipPattern::None:
        case ShipPattern::Solid:
            break;  // no primitives -- handled by the caller
    }
    return count;
}

} // namespace

ShipPattern ship_pattern(battleship::core::ShipType type) {
    switch (type) {
        case battleship::core::ShipType::Carrier:
            return ShipPattern::Solid;
        case battleship::core::ShipType::Battleship:
            return ShipPattern::HStripes;
        case battleship::core::ShipType::Cruiser:
            return ShipPattern::VStripes;
        case battleship::core::ShipType::Destroyer:
            return ShipPattern::Checker;
        case battleship::core::ShipType::Submarine:
            return ShipPattern::CenterDot;
    }
    return ShipPattern::None;
}

ShipPattern ship_pattern_at(const battleship::core::Board& board, battleship::core::Coord c) {
    for (const auto& ship : board.ships()) {
        if (ship.occupies(c)) {
            return ship_pattern(ship.type());
        }
    }
    return ShipPattern::None;
}

void apply_ship_pattern(lv_obj_t* cell, ShipPattern pattern, lv_color_t base_color,
                        int cell_content_px) {
    if (!cell) {
        return;
    }

    if (pattern == ShipPattern::Solid) {
        // Carrier: the "densest" texture is a flat, slightly-darkened fill
        // (a full dot/stripe grid would still show 1px gaps and read as yet
        // another line texture, not solid). Darken the cell's own bg_color a
        // little so it's visibly a different, denser shade than an undamaged
        // non-Carrier hull cell, then fall through with an empty rect list.
        lv_obj_set_style_bg_color(cell, lv_color_darken(base_color, LV_OPA_20), 0);
    }

    PatternRect rects[kMaxPatternRects];
    const int rect_count = build_pattern_rects(pattern, cell_content_px, rects);

    // Darken relative to whatever the cell's own bg_color already is, so the
    // texture stays visible on the normal hull color and on the hit/sunk
    // overlay colors alike (a fixed color would wash out or vanish against
    // at least one of those). LV_OPA_60 was picked by screenshot review of
    // the 1px primitives: the old 3px dots used LV_OPA_50, but 1px lines
    // carry less ink and needed slightly more contrast to read at cell size,
    // while still staying distinguishable from the near-black cursor border.
    const lv_color_t texture_color = lv_color_darken(base_color, LV_OPA_60);

    // Reuse existing primitive children (tagged via user_data) if this cell
    // was already set up once; only create them lazily on first use so empty
    // board cells (the vast majority: only ~17 of 100 cells hold a ship)
    // never pay for extra lv_obj children.
    lv_obj_t* prims[kMaxPatternRects] = {};
    int existing = 0;
    const uint32_t child_count = lv_obj_get_child_count(cell);
    for (uint32_t i = 0; i < child_count && existing < kMaxPatternRects; ++i) {
        lv_obj_t* child = lv_obj_get_child(cell, static_cast<int32_t>(i));
        if (lv_obj_get_user_data(child) == kRectUserDataTag) {
            prims[existing++] = child;
        }
    }

    const int pool_size = rect_count > existing ? rect_count : existing;
    for (int i = existing; i < rect_count; ++i) {
        lv_obj_t* prim = lv_obj_create(cell);
        lv_obj_remove_style_all(prim);
        lv_obj_clear_flag(prim, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(prim, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(prim, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(prim, 0, 0);
        lv_obj_set_user_data(prim, const_cast<char*>(kRectUserDataTag));
        prims[i] = prim;
    }

    // Show/size the first rect_count primitives, hide any leftovers from a
    // previous (denser) pattern this cell may have displayed.
    for (int i = 0; i < pool_size; ++i) {
        lv_obj_t* prim = prims[i];
        if (!prim) {
            continue;
        }
        if (i >= rect_count) {
            lv_obj_add_flag(prim, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(prim, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(prim, rects[i].w, rects[i].h);
        lv_obj_set_pos(prim, rects[i].x, rects[i].y);
        lv_obj_set_style_bg_color(prim, texture_color, 0);
    }
}

} // namespace view
