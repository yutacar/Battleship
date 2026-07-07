/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "core/board.h"
#include "core/types.h"
#include "lvgl.h"

namespace view {

// Per-ship-type "texture" so adjacent hulls on the same fleet board stay
// visually distinct even when placed touching each other (no gap between
// hulls). Each ship type gets a fixed fine-grained texture built from 1px
// primitives (stripes / dots) rendered programmatically inside the cell --
// no font glyphs (no CJK font regen / glyph coverage risk) and no image
// asset pipeline (see NOTES.md).
//
// This replaced the earlier 3x3 dot-mask approach (1 dot ~= 3-4px), which
// user feedback called too coarse ("荒い"): the texture is now generated
// procedurally from the cell's inner pixel size, so the same pattern family
// scales from the 11px placement/battle cells down to the 6px half-size
// own-fleet board cells introduced alongside the Battle screen's live score
// panel.
//
// The five types keep a dense -> sparse progression plus a unique shape per
// type so they read apart even at 1px granularity:
//   Carrier    -> Solid    (flat darkened fill; the densest possible "tint")
//   Battleship -> HStripes (1px horizontal lines, 2px pitch; ~50% fill)
//   Cruiser    -> VStripes (1px vertical lines, 2px pitch; ~50% fill,
//                           orthogonal to Battleship so orientation, not
//                           density, separates the two)
//   Destroyer  -> Checker  (1px dots on a staggered 2px grid; ~15% fill,
//                           fine diagonal-checker look)
//   Submarine  -> CenterDot (single centered dot; the sparsest)
enum class ShipPattern : uint8_t {
    None,       ///< no ship in this cell -- hide all texture primitives
    Solid,      ///< Carrier
    HStripes,   ///< Battleship
    VStripes,   ///< Cruiser
    Checker,    ///< Destroyer
    CenterDot,  ///< Submarine
};

/// Returns the fixed texture pattern for `type`.
ShipPattern ship_pattern(battleship::core::ShipType type);

/// Looks up which ship (if any) of `board` occupies `c` and returns its
/// pattern, or ShipPattern::None if `c` is unoccupied. Shared by
/// placement_screen.cpp (placed ships + the pending preview ship) and
/// battle_screen.cpp (own fleet board) so both screens render identical
/// per-ship textures.
ShipPattern ship_pattern_at(const battleship::core::Board& board, battleship::core::Coord c);

/// Renders `pattern` into `cell` by lazily creating/reusing small
/// rectangle child objects (tagged via user_data) and shows/hides + sizes
/// them per pattern. Passing ShipPattern::None hides all primitives (plain
/// cell, no ship).
///
/// `base_color` should be the color the caller already applied to the cell's
/// own bg_color (hull color, or the hit/sunk overlay color); the texture is
/// drawn darkened relative to `base_color` so it stays visible however the
/// underlying cell is tinted (e.g. still readable once a hit recolors the
/// cell red) without callers having to compute a second color.
/// `cell_content_px` is the inner size (cell side length minus any
/// border/gap) the texture should exactly fill.
void apply_ship_pattern(lv_obj_t* cell, ShipPattern pattern, lv_color_t base_color,
                        int cell_content_px);

} // namespace view
