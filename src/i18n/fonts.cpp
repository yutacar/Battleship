/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "i18n/fonts.h"

#include "i18n/strings.h"

#include "asset_manager.h"
#include "logger.h"

namespace i18n {

namespace {

struct CjkFontSet {
    lv_font_t* size_12 = nullptr;
    lv_font_t* size_18 = nullptr;
};

CjkFontSet g_japanese;
CjkFontSet g_chinese;
bool g_initialized = false;

/// Wires `cjk_font`'s fallback to `latin_font` (Montserrat bitmap) so ASCII
/// glyphs embedded in JA/ZH strings render crisply via the bitmap font
/// instead of being rasterized by FreeType (see fonts.h class doc). No-op if
/// `cjk_font` failed to load (nullptr) -- callers already treat that as "use
/// Montserrat directly" via font_for_size()'s fallback logic.
void wire_fallback(lv_font_t* cjk_font, const lv_font_t* latin_font) {
    if (cjk_font) {
        cjk_font->fallback = latin_font;
    }
}

CjkFontSet load_cjk_font_set(app::AssetManager& assets, const char* file_name) {
    CjkFontSet set;
    set.size_12 = assets.load_font(file_name, 12);
    set.size_18 = assets.load_font(file_name, 18);
    if (!set.size_12 || !set.size_18) {
        LOG_WARN("i18n: failed to load CJK font asset '{}' (falling back to Montserrat)", file_name);
    }
    wire_fallback(set.size_12, &lv_font_montserrat_12);
    wire_fallback(set.size_18, &lv_font_montserrat_18);
    return set;
}

const lv_font_t* montserrat_for_size(int32_t size) {
    switch (size) {
        case 12:
            return &lv_font_montserrat_12;
        case 18:
            return &lv_font_montserrat_18;
        case 24:
            return &lv_font_montserrat_24;
        default:
            return &lv_font_montserrat_12;
    }
}

}  // namespace

void init_fonts(app::AssetManager& assets) {
    if (g_initialized) {
        return;
    }
    g_japanese = load_cjk_font_set(assets, "NotoSansJP-Subset.otf");
    g_chinese = load_cjk_font_set(assets, "NotoSansSC-Subset.otf");
    g_initialized = true;
}

const lv_font_t* font_for_language_and_size(Lang lang, int32_t size) {
    const CjkFontSet* set = nullptr;
    if (lang == Lang::JA) {
        set = &g_japanese;
    } else if (lang == Lang::ZH) {
        set = &g_chinese;
    }

    if (set) {
        if (size == 12 && set->size_12) {
            return set->size_12;
        }
        if (size == 18 && set->size_18) {
            return set->size_18;
        }
    }

    // English, or any size/language without a loaded CJK font (e.g. size 24
    // -- only the English-only "BATTLESHIP" logo uses it): Montserrat.
    return montserrat_for_size(size);
}

const lv_font_t* font_for_size(int32_t size) {
    return font_for_language_and_size(current_language(), size);
}

}  // namespace i18n
