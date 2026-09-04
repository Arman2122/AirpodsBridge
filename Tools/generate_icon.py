#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: MIT
"""Generate the AirpodsBridge application icon.

Emits `Icon.svg` -- rendered live as the tray icon by `Gui/TrayIcon.cpp` -- and the
multi-resolution `Icon.ico` embedded in the executable and the NSIS installer. The raster
is rasterised from the SVG this script just wrote, so the two cannot drift apart.

The artwork is a pair of AirPods Pro earbuds seen from the front. One earbud is drawn from
the path data below and the other is the same drawing mirrored, so the two ear tips face
outward the way a left and a right bud do.

The source drawing is outline-only, which would vanish against a light taskbar, so the
body is filled white behind it: `BODY` is the first subpath of `OUTLINE[0]`, which traces
the earbud's silhouette, and filling it gives a solid backing for the strokes drawn on
top. The result reads as a white earbud with a dark outline on any background.

`renderPM` composites onto an opaque background, so alpha is recovered exactly by
rendering twice -- once on white, once on black -- and solving the compositing equation
per pixel.

Placement and framing are measured, not hand-tuned: one earbud is rendered to find its
bounding box, which fixes the mirror axis and the gap, and the finished pair is rendered
again to centre it in the canvas.

Usage:
    pip install pillow svglib rlPyCairo
    python Tools/generate_icon.py

`cairocffi` must NOT be installed: it takes precedence over `pycairo` inside `renderPM`
and then fails to locate `libcairo-2.dll` on Windows.
"""

from __future__ import annotations

import os

from PIL import Image
from reportlab.graphics import renderPM
from svglib.svglib import svg2rlg

# --- Artwork -----------------------------------------------------------------------------
#
# Path data in the source drawing's own 100x100 user space. BODY is the first subpath of
# OUTLINE[0] and must stay in sync with it.
#
# The drawing is AirPods Pro icon 3456664 from the Noun Project
# (https://thenounproject.com/icon/3456664/). The path data below is not covered by this
# file's SPDX header -- it carries the licence it was obtained under, and the credit for it
# lives in README.md. Keep both if you redistribute this.
#
BODY = "M89.248,43.359c-0.621-3.755-2.888-7.25-6.219-9.588c-3.147-2.21-6.8-3.074-10.275-2.432 c-3.118,0.576-7.831,1.834-12.74,6.097c-3.89,3.379-5.203,6.139-5.53,6.953c-0.948,0.214-1.873,0.559-2.701,0.915 c-1.9,0.816-4.512,2.935-5.758,5.656c-1.226,2.675-1.358,4.125-1.117,6.869c0.232,2.643,2.182,5.252,3.229,6.653l0.182,0.243 c1.09,1.469,3.169,3.331,5.737,4.154c1.167,0.374,2.487,0.572,3.818,0.572c0,0,0,0,0.001,0c1.35,0,2.689-0.205,3.771-0.576 c1.975-0.677,3.81-2.019,4.781-2.962c0.051-0.049,0.1-0.101,0.149-0.15c0.007,0.001,0.015,0.002,0.021,0.003 c0.014,3.063,0.094,21.306,0.094,22.854c0,3.464,3.116,4.379,4.764,4.379h3.191c3.291,0,4.485-2.75,4.485-4.599V61.994 c1.539-0.966,3.027-2.079,4.624-3.589c1.958-1.853,4.476-4.781,5.349-8.213C89.617,48.179,89.666,45.88,89.248,43.359z"

OUTLINE = [
    "M89.248,43.359c-0.621-3.755-2.888-7.25-6.219-9.588c-3.147-2.21-6.8-3.074-10.275-2.432 c-3.118,0.576-7.831,1.834-12.74,6.097c-3.89,3.379-5.203,6.139-5.53,6.953c-0.948,0.214-1.873,0.559-2.701,0.915 c-1.9,0.816-4.512,2.935-5.758,5.656c-1.226,2.675-1.358,4.125-1.117,6.869c0.232,2.643,2.182,5.252,3.229,6.653l0.182,0.243 c1.09,1.469,3.169,3.331,5.737,4.154c1.167,0.374,2.487,0.572,3.818,0.572c0,0,0,0,0.001,0c1.35,0,2.689-0.205,3.771-0.576 c1.975-0.677,3.81-2.019,4.781-2.962c0.051-0.049,0.1-0.101,0.149-0.15c0.007,0.001,0.015,0.002,0.021,0.003 c0.014,3.063,0.094,21.306,0.094,22.854c0,3.464,3.116,4.379,4.764,4.379h3.191c3.291,0,4.485-2.75,4.485-4.599V61.994 c1.539-0.966,3.027-2.079,4.624-3.589c1.958-1.853,4.476-4.781,5.349-8.213C89.617,48.179,89.666,45.88,89.248,43.359z M61.321,67.931c-0.98,0.336-2.204,0.521-3.446,0.521h-0.001c-1.229,0-2.443-0.182-3.514-0.525 c-2.342-0.75-4.242-2.453-5.239-3.797l-0.183-0.246c-0.988-1.322-2.827-3.783-3.035-6.143c-0.228-2.592-0.111-3.874,1.03-6.364 c1.085-2.371,3.459-4.388,5.244-5.154c1.135-0.487,2.606-1.047,3.933-1.047c0.438,0,0.848,0.062,1.216,0.182 c1.696,0.557,3.713,1.482,5.802,4.126c1.893,2.396,3.204,4.909,4.011,7.683c0.625,2.151,0.478,3.679,0.38,4.688l-0.026,0.287 c-0.105,1.217-0.78,2.101-1.762,3.054C64.832,66.068,63.14,67.307,61.321,67.931z M75.939,89.188 c-0.046,0.296-0.253,0.984-1.182,0.984h-6.732c-0.207-0.415-0.334-0.924-0.334-1.549h7.754c0.209,0,0.353,0.045,0.428,0.132 C75.967,88.864,75.961,89.051,75.939,89.188z M74.646,92h-3.191c-0.111,0-2.053-0.026-3.102-1.328h6.404 c0.813,0,1.293-0.415,1.525-0.912h1.585C77.51,90.779,76.664,92,74.646,92z M78.132,88.401c0,0.012-0.004,0.371-0.116,0.858h-1.582 c0.053-0.343-0.01-0.63-0.181-0.83c-0.175-0.203-0.446-0.307-0.808-0.307H67.69c-0.009-3.295-0.079-19.22-0.093-22.291 c0.723,0,1.487-0.082,2.389-0.178c1.404-0.149,3.666-0.64,6.472-2.128c0.573-0.305,1.128-0.612,1.674-0.93V88.401z M88.136,49.946 c-0.814,3.2-3.205,5.971-5.067,7.733c-2.413,2.282-4.575,3.636-7.079,4.965c-2.805,1.487-5.031,1.901-6.171,2.023 c-0.906,0.097-1.678,0.173-2.4,0.157c0.555-0.718,0.979-1.55,1.069-2.594l0.026-0.277c0.098-1.021,0.263-2.729-0.415-5.064 c-0.844-2.9-2.213-5.525-4.187-8.024c-2.268-2.869-4.544-3.889-6.274-4.457c-0.47-0.154-0.983-0.232-1.528-0.232 c-0.147,0-0.296,0.028-0.443,0.039c0.555-1.105,1.944-3.366,5.004-6.023c4.725-4.103,9.263-5.314,12.266-5.869 c3.214-0.594,6.594,0.212,9.52,2.267c3.112,2.185,5.229,5.44,5.807,8.932C88.656,45.906,88.614,48.067,88.136,49.946z",
    "M53.31,58.486c-0.849-1.042-2.077-2.483-3.573-2.394c-0.814,0.049-1.474,0.593-1.387,1.616 c0.143,1.672,1.288,3.587,2.302,4.882c0.769,0.981,2.222,2.426,3.616,2.245c1.204-0.156,1.077-1.741,0.918-2.576 C54.917,60.853,54.206,59.588,53.31,58.486z",
    "M81.628,37.352c-0.936,0.069-1.664,0.564-2.421,1.018c-0.757,0.454-4.965,3.026-6.134,3.824 c-1.17,0.798-2.022,1.279-2.174,2.476c-0.151,1.197,0.44,2.187,1.32,2.724c0.881,0.536,2.098,0.5,2.793,0.082 c0.949-0.572,5.198-3.273,6.106-3.837c0.907-0.564,1.967-1.101,2.53-1.486c0.564-0.385,1.073-0.921,1.046-1.623 c-0.028-0.702-0.221-1.444-0.716-2.008S82.563,37.283,81.628,37.352z",
]

# --- Output ------------------------------------------------------------------------------

CANVAS = 64.0          # user space of the emitted SVG
PROBE_VIEW = 256.0     # viewport used when measuring, in source units
MARGIN = 2.5           # empty space kept around the art
GAP = 3.0              # space between the two earbuds, in source units
INK = "#36404E"        # outline colour
PAPER = "#FFFFFF"      # body fill

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "Source", "Resource", "Image")
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

NL = chr(10)


def _earbud(transform, indent):
    """One earbud: the silhouette filled, then the line art over it."""
    pad = " " * indent
    lines = ['%s<g transform="%s">' % (pad, transform)]
    lines.append('%s  <path d="%s" fill="%s"/>' % (pad, BODY, PAPER))
    lines += ['%s  <path d="%s" fill="%s"/>' % (pad, d, INK) for d in OUTLINE]
    lines.append("%s</g>" % pad)
    return NL.join(lines)


def _svg(body, outer="scale(1)", view=None):
    view = CANVAS if view is None else view
    return NL.join([
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" width="%g"'
        ' height="%g" role="img" aria-label="AirpodsBridge">' % (view, view, view, view),
        "  <title>AirpodsBridge</title>",
        '  <g transform="%s">' % outer,
        body,
        "  </g>",
        "</svg>",
        "",
    ])


def _render(svg_path, px, bg):
    drawing = svg2rlg(svg_path)
    drawing.scale(px / drawing.width, px / drawing.height)
    drawing.width = drawing.height = px
    return renderPM.drawToPIL(drawing, bg=bg).convert("RGB")


def _render_rgba(svg_path, px, supersample=4):
    """Render with real transparency by solving the compositing equation."""
    n = px * supersample
    on_white = _render(svg_path, n, 0xFFFFFF)
    on_black = _render(svg_path, n, 0x000000)

    out = Image.new("RGBA", (n, n))
    white, black, dst = on_white.load(), on_black.load(), out.load()
    for y in range(n):
        for x in range(n):
            rw, gw, bw = white[x, y]
            rb, gb, bb = black[x, y]
            # Compositing C over a background gives C*a + bg*(1 - a), so the difference
            # between the two renders is 255*(1 - a), independent of C.
            alpha = 255 - max(rw - rb, gw - gb, bw - bb)
            dst[x, y] = (rb, gb, bb, alpha) if alpha > 0 else (0, 0, 0, 0)
    return out.resize((px, px), Image.LANCZOS)


def _bbox(body, work_dir, probe=1024):
    """Bounding box of `body`, in the source drawing's user units.

    The probe viewport has to span the source coordinate space, not the 64-unit output
    canvas: the artwork sits around x=45..90, so measuring it through a 64-unit viewBox
    would measure a clipped image and yield a bounding box that is silently too small.
    """
    probe_path = os.path.join(work_dir, "_probe.svg")
    with open(probe_path, "w", encoding="utf-8", newline=NL) as f:
        f.write(_svg(body, view=PROBE_VIEW))
    try:
        box = _render_rgba(probe_path, probe, supersample=1).getbbox()
    finally:
        os.remove(probe_path)
    unit = PROBE_VIEW / probe
    return tuple(v * unit for v in box)


def _pair(work_dir):
    """Both earbuds, the second mirrored, separated by GAP."""
    single = _earbud("translate(0 0)", 4)
    x0, _, x1, _ = _bbox(single, work_dir)

    # Mirroring about x sends a point p to (axis - p), so the reflected bounding box is
    # [axis - x1, axis - x0]. Placing its left edge one gap right of the original fixes it.
    axis = 2 * x1 + GAP
    return NL.join([single, _earbud("translate(%.4f 0) scale(-1 1)" % axis, 4)])


def _fit(body, work_dir):
    """Scale and translation that centre the art in the canvas with an even margin."""
    x0, y0, x1, y1 = _bbox(body, work_dir)
    width, height = x1 - x0, y1 - y0
    span = CANVAS - 2 * MARGIN
    scale = span / max(width, height)
    return "translate(%.4f %.4f) scale(%.5f)" % (
        MARGIN + (span - width * scale) / 2 - x0 * scale,
        MARGIN + (span - height * scale) / 2 - y0 * scale,
        scale,
    )


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    body = _pair(OUT_DIR)
    svg_path = os.path.join(OUT_DIR, "Icon.svg")
    with open(svg_path, "w", encoding="utf-8", newline=NL) as f:
        f.write(_svg(body, _fit(body, OUT_DIR)))
    print("wrote %s" % svg_path)

    # Each entry is rendered at its own size rather than downscaled from one bitmap, so the
    # small sizes stay crisp.
    frames = [_render_rgba(svg_path, size) for size in ICO_SIZES]
    ico_path = os.path.join(OUT_DIR, "Icon.ico")
    frames[-1].save(ico_path, format="ICO", sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1])
    print("wrote %s (%s)" % (ico_path, ", ".join("%dx%d" % (s, s) for s in ICO_SIZES)))


if __name__ == "__main__":
    main()
