/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Debug-only screenshot capture. Renders the currently active LVGL screen
 * (lv_screen_active()) to a PNG file using lv_snapshot_take() + libpng.
 * Intended for UI verification during development (Phase 2+): press F12 (see
 * platform/linux_input.cpp key routing) or call capture_screenshot()
 * directly, e.g. from an automated smoke-test driver.
 *
 * Not compiled into device (CM0) builds' hot path in any meaningful way --
 * it only touches lv_snapshot/libpng, both already linked for desktop, and
 * is cheap to leave in place, but is expected to be used from desktop
 * builds during development.
 */
#pragma once

#include <string>

namespace app {

/// Directory (relative to the current working directory, or absolute) where
/// screenshots are written. Created if missing.
constexpr const char* kScreenshotDir = "docs/screenshots";

/// Captures the currently active screen and writes it as a PNG file named
/// `<kScreenshotDir>/<name>.png`. Returns true on success. Safe to call
/// repeatedly (overwrites any existing file of the same name).
bool capture_screenshot(const std::string& name);

} // namespace app
