#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Generate the bitmaps the installer shows.

Emits, under `Source/Resource/Installer/`:

  Header.bmp   150 x 57   the strip at the top of every installer page
  Sidebar.bmp  164 x 314  the panel beside the welcome and finish pages

Both are rendered from `Source/Resource/Image/Icon.svg` through the same rasteriser
`generate_icon.py` uses, so the installer and the application never show two different
earbuds. Colours come from the icon's own ink and the application's sidebar tint, and the
name is set in Inter SemiBold, the face the application ships.

NSIS wants 24-bit BMPs at exactly these sizes: MUI2 stretches nothing and clips anything
larger.

Usage:
    pip install pillow svglib rlPyCairo
    python Tools/generate_installer_art.py

Run `generate_icon.py` first when the icon itself has changed.
"""

from __future__ import annotations

import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import generate_icon  # noqa: E402  (sibling script, imported for its renderer)

ROOT = generate_icon.ROOT
ICON_SVG = os.path.join(generate_icon.OUT_DIR, "Icon.svg")
FONT = os.path.join(ROOT, "Source", "Resource", "Font", "Inter-SemiBold.ttf")
OUT_DIR = os.path.join(ROOT, "Source", "Resource", "Installer")

HEADER_SIZE = (150, 57)
SIDEBAR_SIZE = (164, 314)

PAPER = (255, 255, 255)
SIDEBAR = (246, 246, 247)   # Gui::Theme::Colour::Sidebar
INK = (0x36, 0x40, 0x4E)    # the icon's outline colour
SUBTLE = (94, 94, 94)       # Gui::Theme::Colour::TextSecondary

NAME = "AirpodsBridge"
TAGLINE = "for Windows"


def _paste_icon(canvas, size, centre):
    icon = generate_icon._render_rgba(ICON_SVG, size)
    x = int(round(centre[0] - size / 2))
    y = int(round(centre[1] - size / 2))
    canvas.alpha_composite(icon, (x, y))


def _text(draw, canvas_width, y, text, font, fill):
    """Draw `text` centred horizontally, its top at `y`; return the bottom edge."""
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    x = (canvas_width - (right - left)) / 2 - left
    draw.text((x, y - top), text, font=font, fill=fill)
    return y + (bottom - top)


def _header():
    """The icon alone, centred: the page title is drawn beside it by the installer."""
    width, height = HEADER_SIZE
    canvas = Image.new("RGBA", HEADER_SIZE, PAPER + (255,))
    _paste_icon(canvas, 40, (width / 2, height / 2))
    return canvas


def _sidebar():
    """Icon over the name, on the same tint the application's own sidebar uses."""
    width, height = SIDEBAR_SIZE
    canvas = Image.new("RGBA", SIDEBAR_SIZE, SIDEBAR + (255,))

    # A hairline on the right edge, where the panel meets the white page.
    draw = ImageDraw.Draw(canvas)
    draw.line([(width - 1, 0), (width - 1, height)], fill=(229, 229, 234, 255))

    _paste_icon(canvas, 96, (width / 2, 112))

    name_font = ImageFont.truetype(FONT, 17)
    tagline_font = ImageFont.truetype(FONT, 11)
    bottom = _text(draw, width, 178, NAME, name_font, INK + (255,))
    _text(draw, width, bottom + 6, TAGLINE, tagline_font, SUBTLE + (255,))
    return canvas


def _save(canvas, name):
    path = os.path.join(OUT_DIR, name)
    canvas.convert("RGB").save(path, format="BMP")
    print("wrote %s (%dx%d)" % (path, canvas.width, canvas.height))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    _save(_header(), "Header.bmp")
    _save(_sidebar(), "Sidebar.bmp")


if __name__ == "__main__":
    main()
