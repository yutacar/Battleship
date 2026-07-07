/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Font selection for Phase 5 (i18n). English continues to use the existing
 * bitmap Montserrat fonts (LV_FONT_MONTSERRAT_12/18/24, already builtin and
 * crisp at these small sizes -- see PLAN.md "英語は既存フォントのまま").
 * Japanese/Chinese need CJK glyphs, which Montserrat does not contain; those
 * are rendered at runtime via FreeType from a glyph-subsetted TTF/OTF
 * (assets/fonts/NotoSansJP-Subset.otf / NotoSansSC-Subset.otf -- see
 * tools/generate_cjk_fonts.py and NOTES.md Phase 5 for why FreeType+subset
 * was chosen over lv_font_conv pre-baked bitmaps).
 *
 * Every CJK font returned here has its `fallback` pointer set to the
 * same-size Montserrat bitmap font (LVGL v9's lv_font_t::fallback,
 * resolved recursively per-glyph -- see lv_font.h), so ASCII/Latin
 * characters embedded in JA/ZH strings (e.g. digits, "WiFi", "CPU") still
 * render via the crisp bitmap font instead of paying FreeType rasterization
 * for glyphs Montserrat already has.
 */
#pragma once

#include "lvgl.h"

#include "commentary/commentator.h"

namespace app {
class AssetManager;
}

namespace i18n {

using Lang = battleship::commentary::Lang;

/// Must be called once at startup (after AssetManager is constructed, before
/// building any UI) so the CJK FreeType fonts are loaded and ready by the
/// time the first screen calls font_for_language(). Safe to call multiple
/// times (subsequent calls are no-ops) -- mirrors AssetManager::load_font's
/// own load-once-then-cache behavior.
void init_fonts(app::AssetManager& assets);

/// Returns the font to use for `size` (12, 18, or 24 -- the only sizes any
/// screen currently requests) in the current i18n::current_language().
/// English always returns the corresponding builtin Montserrat bitmap font.
/// Japanese/Chinese return the FreeType CJK font for that size (falling back
/// to Montserrat automatically for any request size without a loaded CJK
/// font, e.g. 24 -- only used by the English-only "BATTLESHIP" logo title,
/// see menu_screen.cpp) with fallback wired per the class doc above. Never
/// returns nullptr: on any load failure this falls back to the requested
/// size's Montserrat font instead (readable-but-tofu-free ASCII rendering
/// beats a null font pointer crashing lv_label_set_text).
const lv_font_t* font_for_size(int32_t size);

/// Same as font_for_size(), but for an explicitly given `lang` rather than
/// i18n::current_language(). Used by the Settings screen so each language
/// option in the list (e.g. "日本語", "简体中文") always renders in its own
/// script regardless of which language is currently active -- a user who
/// doesn't yet read the active UI language still needs to recognize their
/// own language's name to select it.
const lv_font_t* font_for_language_and_size(Lang lang, int32_t size);

}  // namespace i18n
