#!/usr/bin/env python3
"""
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT

Generates assets/images/title_logo.png: a 3D isometric-block ASCII-art
rendering of "BATTLESHIP" for the menu screen title
(src/view/screens/menu_screen.cpp), replacing the plain lv_label text.

Design
------
Each letter is authored as a 5x7 dot-matrix glyph (FONT_5X7 below, classic
LED-sign style bitmap font, hand-authored for the 9 distinct letters in
"BATTLESHIP": B A T L E S H I P). Every "on" dot is drawn as a small cube
with 3 faces (front, top, right-side) using isometric-style parallelogram
bevels; adjacent "on" dots fuse into solid blocks because the top/side
bevels are only drawn on the *silhouette edge* of the glyph (a cell with no
"on" neighbor immediately above/right), not on every individual cell. That
fusion plus a heavy dark outline around the whole glyph is what makes this
read as blocky, extruded, 3D ASCII art rather than a checkerboard of
separate cubes -- see render() for the exact algorithm.

Why pre-rendered PNG instead of an LVGL label: LVGL's built-in fonts
(Montserrat) are proportional and have no built-in 3D/bevel rendering, so
drawing this directly with lv_label would require a custom bitmap font at
minimum, and still couldn't do the per-glyph beveled shading. Pre-rendering
with Pillow and displaying via lv_image (LV_USE_LIBPNG=1, LV_USE_FS_STDIO
drive letter 'A' -- both already enabled in lv_conf_desktop.h /
lv_conf_cm0.h) is the simplest correct approach and matches how the app
icon (assets/images/battleship.png) is already produced (see NOTES.md
Sec 14.2 for the icon's own Pillow-based generation).

Color choice: the face color (99, 226, 183) matches
view::color::dark::kPrimary (0x63e2b7, src/view/ui_const.h) so the logo
reads as an intentional accent on the dark theme; a heavy near-black
outline (6, 20, 16) keeps it legible on the light theme's 0xffffff body
too. See NOTES.md for the 3-variant comparison (green / steel-blue / amber)
that led to picking the green variant.

Requirements (installed on demand into a local venv, not vendored):
    pip install pillow

Usage:
    python3 tools/generate_title_logo.py [--variant {A,B,C}] [--all-previews]
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw

REPO_ROOT = Path(__file__).resolve().parent.parent
OUTPUT_PATH = REPO_ROOT / "assets" / "images" / "title_logo.png"

TEXT = "BATTLESHIP"
GLYPH_COLS, GLYPH_ROWS = 5, 7

# Hand-authored 5x7 dot-matrix bitmap font (rows top->bottom, '1' = lit
# cell), classic LED-sign style. Only the 9 distinct letters needed by
# "BATTLESHIP" are defined.
FONT_5X7 = {
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
}

# Max PNG dimensions the menu screen layout budgets for the logo (see
# src/view/screens/menu_screen.cpp: title sits at the top of a 320x170
# screen, above a 96px-tall button stack).
MAX_W, MAX_H = 300, 48

VARIANTS = {
    # "A" (green) is the final pick -- see module docstring / NOTES.md.
    "A": dict(
        face=(99, 226, 183, 255),
        top=(200, 250, 235, 255),
        side=(20, 80, 65, 255),
        outline=(6, 20, 16, 255),
    ),
    "B": dict(
        face=(120, 170, 220, 255),
        top=(210, 230, 250, 255),
        side=(25, 55, 95, 255),
        outline=(8, 16, 28, 255),
    ),
    "C": dict(
        face=(235, 170, 60, 255),
        top=(255, 220, 140, 255),
        side=(110, 65, 10, 255),
        outline=(30, 18, 4, 255),
    ),
}


def _on(grid: list[str], r: int, c: int) -> bool:
    if 0 <= r < GLYPH_ROWS and 0 <= c < GLYPH_COLS:
        return grid[r][c] == "1"
    return False


def render(
    face: tuple[int, int, int, int],
    top: tuple[int, int, int, int],
    side: tuple[int, int, int, int],
    outline: tuple[int, int, int, int],
    cell: int = 6,
    depth: int = 3,
    letter_gap: int = 1,
    supersample: int = 4,
) -> Image.Image:
    ss = supersample
    cell_px = cell * ss
    depth_px = depth * ss

    letter_col_offsets = []
    total_cols = 0
    for _ in TEXT:
        letter_col_offsets.append(total_cols)
        total_cols += GLYPH_COLS + letter_gap

    width = total_cols * cell_px + depth_px + 4 * ss
    height = GLYPH_ROWS * cell_px + depth_px + 4 * ss
    img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    origin_x, origin_y = depth_px + 2 * ss, depth_px + 2 * ss

    def cell_origin(base_col: int, r: int, c: int) -> tuple[int, int]:
        return origin_x + (base_col + c) * cell_px, origin_y + r * cell_px

    # Pass 1: heavy outline -- redraw every face/bevel offset in a ring of
    # directions so a crisp dark border surrounds the whole glyph silhouette.
    for li, ch in enumerate(TEXT):
        grid = FONT_5X7[ch]
        base_col = letter_col_offsets[li]
        for r in range(GLYPH_ROWS):
            for c in range(GLYPH_COLS):
                if grid[r][c] != "1":
                    continue
                x0, y0 = cell_origin(base_col, r, c)
                for dx in (-ss, 0, ss):
                    for dy in (-ss, 0, ss):
                        draw.rectangle(
                            [x0 + dx, y0 + dy, x0 + cell_px - 1 + dx, y0 + cell_px - 1 + dy],
                            fill=outline,
                        )
                if not _on(grid, r - 1, c):
                    for dx in (-ss, 0, ss):
                        for dy in (-ss, 0, ss):
                            draw.polygon(
                                [
                                    (x0 + dx, y0 + dy),
                                    (x0 + depth_px + dx, y0 - depth_px + dy),
                                    (x0 + cell_px - 1 + depth_px + dx, y0 - depth_px + dy),
                                    (x0 + cell_px - 1 + dx, y0 + dy),
                                ],
                                fill=outline,
                            )
                if not _on(grid, r, c + 1):
                    for dx in (-ss, 0, ss):
                        for dy in (-ss, 0, ss):
                            draw.polygon(
                                [
                                    (x0 + cell_px - 1 + dx, y0 + dy),
                                    (x0 + cell_px - 1 + depth_px + dx, y0 - depth_px + dy),
                                    (x0 + cell_px - 1 + depth_px + dx, y0 + cell_px - 1 - depth_px + dy),
                                    (x0 + cell_px - 1 + dx, y0 + cell_px - 1 + dy),
                                ],
                                fill=outline,
                            )

    # Pass 2: front faces (solid fill).
    for li, ch in enumerate(TEXT):
        grid = FONT_5X7[ch]
        base_col = letter_col_offsets[li]
        for r in range(GLYPH_ROWS):
            for c in range(GLYPH_COLS):
                if grid[r][c] == "1":
                    x0, y0 = cell_origin(base_col, r, c)
                    draw.rectangle([x0, y0, x0 + cell_px - 1, y0 + cell_px - 1], fill=face)

    # Pass 3: top bevels, only on the silhouette's top edge (no 'on'
    # neighbor directly above) so adjoining cells fuse into solid blocks.
    for li, ch in enumerate(TEXT):
        grid = FONT_5X7[ch]
        base_col = letter_col_offsets[li]
        for r in range(GLYPH_ROWS):
            for c in range(GLYPH_COLS):
                if grid[r][c] == "1" and not _on(grid, r - 1, c):
                    x0, y0 = cell_origin(base_col, r, c)
                    draw.polygon(
                        [
                            (x0, y0),
                            (x0 + depth_px, y0 - depth_px),
                            (x0 + cell_px - 1 + depth_px, y0 - depth_px),
                            (x0 + cell_px - 1, y0),
                        ],
                        fill=top,
                    )

    # Pass 4: right-side bevels, only on the silhouette's right edge.
    for li, ch in enumerate(TEXT):
        grid = FONT_5X7[ch]
        base_col = letter_col_offsets[li]
        for r in range(GLYPH_ROWS):
            for c in range(GLYPH_COLS):
                if grid[r][c] == "1" and not _on(grid, r, c + 1):
                    x0, y0 = cell_origin(base_col, r, c)
                    draw.polygon(
                        [
                            (x0 + cell_px - 1, y0),
                            (x0 + cell_px - 1 + depth_px, y0 - depth_px),
                            (x0 + cell_px - 1 + depth_px, y0 + cell_px - 1 - depth_px),
                            (x0 + cell_px - 1, y0 + cell_px - 1),
                        ],
                        fill=side,
                    )

    # Downsample (supersampling anti-alias) then fit within the MAX_W x
    # MAX_H budget the menu screen layout allows.
    img = img.resize((width // ss, height // ss), Image.LANCZOS)
    scale = min(MAX_W / img.width, MAX_H / img.height, 1.0)
    if scale < 1.0:
        img = img.resize((max(1, int(img.width * scale)), max(1, int(img.height * scale))), Image.LANCZOS)
    return img


def save_preview(img: Image.Image, path: Path) -> None:
    """Composite over both light (0xffffff) and dark (0x101014) app body
    colors stacked vertically, matching view::color::light::kBody /
    view::color::dark::kBody (src/view/ui_const.h), for a side-by-side
    legibility check against both themes."""
    gap = 10
    preview = Image.new("RGBA", (img.width, img.height * 2 + gap), (128, 128, 128, 255))
    light_bg = Image.new("RGBA", img.size, (0xFF, 0xFF, 0xFF, 255))
    dark_bg = Image.new("RGBA", img.size, (0x10, 0x10, 0x14, 255))
    light_bg.alpha_composite(img)
    dark_bg.alpha_composite(img)
    preview.paste(light_bg, (0, 0))
    preview.paste(dark_bg, (0, img.height + gap))
    preview.save(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--variant", choices=sorted(VARIANTS), default="A")
    parser.add_argument("--preview", action="store_true", help="also write a light/dark preview PNG")
    parser.add_argument("--all-previews", action="store_true", help="render all variants as previews, skip final output")
    args = parser.parse_args()

    if args.all_previews:
        preview_dir = REPO_ROOT / "tools" / "_logo_previews"
        preview_dir.mkdir(exist_ok=True)
        for name, palette in VARIANTS.items():
            img = render(**palette)
            save_preview(img, preview_dir / f"variant_{name}.png")
            print(f"variant {name}: {img.width}x{img.height} -> {preview_dir / f'variant_{name}.png'}")
        return

    img = render(**VARIANTS[args.variant])
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    img.save(OUTPUT_PATH)
    print(f"variant {args.variant}: {OUTPUT_PATH} ({img.width}x{img.height})")

    if args.preview:
        preview_path = OUTPUT_PATH.with_name("title_logo_preview.png")
        save_preview(img, preview_path)
        print(f"preview -> {preview_path}")


if __name__ == "__main__":
    main()
