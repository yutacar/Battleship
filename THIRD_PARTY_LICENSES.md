# Third-Party Licenses

This project's own code is MIT-licensed (see [`LICENSE`](LICENSE)). It builds on and links against
the third-party components listed below, each under its own license. "In `.deb`?" indicates whether
the component (or its output) is actually copied into the Debian package built from
[`cmake/cm0-package.cmake`](cmake/cm0-package.cmake), as opposed to being a build-time-only
dependency (fetched/compiled from source but not redistributed) or a system library the package
merely depends on (`Depends:`, resolved from the target's own `apt` repositories at install time).

| Component | License | Used for | In `.deb`? |
| --- | --- | --- | --- |
| [LVGL](https://github.com/lvgl/lvgl) v9.5 | MIT | Core GUI framework — all screens, widgets, rendering | Statically linked into the app binary |
| Noto Sans JP / Noto Sans SC (subset) | SIL OFL 1.1 | CJK glyph rendering for the Japanese and Simplified Chinese UI, subsetted at build time by `tools/generate_cjk_fonts.py` to only the glyphs actually used | Yes — `assets/fonts/NotoSans{JP,SC}-Subset.otf` are installed to `/usr/share/battleship/fonts/` |
| [fmt](https://github.com/fmtlib/fmt) | MIT | Logging and formatted strings | No — dynamically linked against the system `libfmt10` package (`Depends:`), not bundled |
| [GoogleTest](https://github.com/google/googletest) | BSD-3-Clause | Unit/headless test suite (`tests/`) only; not part of the shipped application | No — test-only, not linked into or shipped with the app |
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | MIT | Backend for the LLM-based battle commentator (`LlmCommentator`). This feature is **disabled by default** (`BATTLESHIP_LLM=OFF`) and unused in the shipped product, which always uses the template-phrase `TemplateCommentator` instead (see `NOTES.md` §15, "LLM 廃止") | No — not built or shipped by default; only compiled in if a developer explicitly configures `-DBATTLESHIP_LLM=ON` |
| [SDL2](https://www.libsdl.org/) | zlib | Desktop simulator display/input backend (macOS/Linux/Windows development builds only) | No — desktop-only; the CardputerZero device build does not use SDL at all |
| [FreeType](https://freetype.org/) | FTL or GPLv2 (dual-licensed; this project uses the FTL option, compatible with MIT distribution) | Runtime TTF/OTF font rasterization (Latin + CJK) | No — dynamically linked against the system `libfreetype6` package (`Depends:`), not bundled |
| libpng, libjpeg-turbo, zlib | libpng license, IJG/BSD-style, zlib | Image decoding used by LVGL (PNG/JPEG assets) and general compression | No — dynamically linked against system packages (`Depends: libpng16-16, libjpeg62-turbo, zlib1g`) |
| [BlueZ](http://www.bluez.org/) (via `libsystemd`'s `sd-bus`, D-Bus) | Accessed only via the system D-Bus API; not vendored or linked as source | `BleTransport` (`src/net/ble/`) talks to the system `bluetoothd` over D-Bus for the default BLE 2-player mode | No — system service + system `libsystemd0` package (`Depends:`), not bundled |

## Notes

- **Template origin**: this repository began as a fork of the CardputerZero official template
  (https://github.com/CardputerZero/template), which is itself MIT-licensed
  (`SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD`, `SPDX-License-Identifier: MIT`
  headers preserved unmodified throughout the original files). See [`LICENSE`](LICENSE).
- **`assets/fonts/LICENSE.txt`**: this file (installed into the `.deb` as
  `/usr/share/doc/battleship/third-party-assets-license.txt`) documents the Inter font family
  (OFL-1.1), Kenney input-prompt icons (CC0), and Phosphor Icons (MIT) — all inherited from the
  upstream template. **It does not currently mention Noto Sans JP/SC**, even though those are the
  *only* fonts this project's own `.deb` actually installs (`cmake/cm0-package.cmake` only copies
  `NotoSans*.otf` from `assets/fonts/`; Inter and Phosphor are explicitly excluded as dead weight
  — see that file's header comment). This is a real gap: Noto Sans is distributed under the SIL
  Open Font License 1.1, same as Inter, so the obligations are similar (retain the license text,
  don't sell the font by itself), but the shipped `LICENSE.txt` should be updated to name Noto Sans
  JP/SC explicitly and drop or clearly mark the Inter/Kenney/Phosphor entries as "not shipped in
  this app's `.deb`" so downstream users aren't misled about what's actually in the package they
  installed. This was **not** changed as part of this pass (out of scope — no `.deb`/build changes),
  but should be fixed before the next release.
