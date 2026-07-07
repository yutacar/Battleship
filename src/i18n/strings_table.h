/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Internal detail header: declares the fixed-size per-language string table
 * type and the three language table instances. Not included by UI code --
 * only by strings.cpp (the tr()/trf() implementation) and the
 * strings_en.cpp/strings_ja.cpp/strings_zh.cpp table definitions themselves.
 */
#pragma once

#include <array>

#include "i18n/strings.h"

namespace i18n::detail {

using Table = std::array<const char*, kStrIdCount>;

/// One row per StrId, in enum declaration order. Defined in
/// strings_en.cpp/strings_ja.cpp/strings_zh.cpp respectively.
extern const Table kEnglishTable;
extern const Table kJapaneseTable;
extern const Table kChineseTable;

}  // namespace i18n::detail
