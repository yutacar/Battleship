/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "i18n/strings.h"

#include <cstdio>

#include "i18n/strings_table.h"

namespace i18n {

namespace {

Lang g_current_language = Lang::EN;

const detail::Table& table_for(Lang lang) {
    switch (lang) {
        case Lang::EN:
            return detail::kEnglishTable;
        case Lang::JA:
            return detail::kJapaneseTable;
        case Lang::ZH:
            return detail::kChineseTable;
    }
    return detail::kEnglishTable;
}

}  // namespace

Lang current_language() {
    return g_current_language;
}

void set_language(Lang lang) {
    g_current_language = lang;
}

const char* tr(StrId id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= kStrIdCount) {
        return "";
    }
    const char* text = table_for(g_current_language)[index];
    return text ? text : "";
}

std::string trf(StrId id, ...) {
    const char* format = tr(id);

    va_list args;
    va_start(args, id);
    // Two-pass vsnprintf: first with a stack buffer big enough for every
    // template in the catalog (all short UI strings; see strings_en.cpp),
    // falling back to a heap buffer only if a future template exceeds it.
    char stack_buf[256];
    va_list args_copy;
    va_copy(args_copy, args);
    const int needed = std::vsnprintf(stack_buf, sizeof(stack_buf), format, args_copy);
    va_end(args_copy);

    std::string result;
    if (needed < 0) {
        result.clear();
    } else if (static_cast<std::size_t>(needed) < sizeof(stack_buf)) {
        result.assign(stack_buf, static_cast<std::size_t>(needed));
    } else {
        std::string heap_buf(static_cast<std::size_t>(needed) + 1, '\0');
        std::vsnprintf(heap_buf.data(), heap_buf.size(), format, args);
        heap_buf.resize(static_cast<std::size_t>(needed));
        result = std::move(heap_buf);
    }
    va_end(args);
    return result;
}

std::string ship_name(battleship::core::ShipType type) {
    switch (type) {
        case battleship::core::ShipType::Carrier:
            return tr(StrId::ShipCarrier);
        case battleship::core::ShipType::Battleship:
            return tr(StrId::ShipBattleship);
        case battleship::core::ShipType::Cruiser:
            return tr(StrId::ShipCruiser);
        case battleship::core::ShipType::Submarine:
            return tr(StrId::ShipSubmarine);
        case battleship::core::ShipType::Destroyer:
            return tr(StrId::ShipDestroyer);
    }
    return tr(StrId::ShipDestroyer);
}

}  // namespace i18n
