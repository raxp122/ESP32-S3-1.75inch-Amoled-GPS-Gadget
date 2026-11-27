#!/usr/bin/env python3
"""Generates the LVGL fonts in main/fonts/ from Poppins (tools/fonts, OFL) and the
Font Awesome 5 file shipped with LVGL (components/lvgl/scripts/built_in_font).

Usage:  python3 tools/gen_fonts.py [/path/to/lv_font_conv]
(lv_font_conv: `npm install lv_font_conv@1.5.3`).

Every icon the firmware uses must be listed in ICONS (and defined in main/ui.h).
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONV = sys.argv[1] if len(sys.argv) > 1 else "lv_font_conv"
FA = os.path.join(ROOT, "components", "lvgl", "scripts", "built_in_font", "FontAwesome5-Solid+Brands+Regular.woff")
TTF = lambda n: os.path.join(ROOT, "tools", "fonts", "Poppins-%s.ttf" % n)

# Latin-1 (Italian and the other western languages) plus a few typographic signs
TEXT = "0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2019,0x201C,0x201D,0x2022,0x2026,0x203A"

ICONS = [
    0xF00C, 0xF00D, 0xF011, 0xF013, 0xF015, 0xF017, 0xF019, 0xF01E, 0xF023, 0xF029,
    0xF03E, 0xF008, 0xF04B, 0xF04C, 0xF053, 0xF054, 0xF06E, 0xF074, 0xF093, 0xF0AC,
    0xF0D0, 0xF0E7, 0xF129, 0xF11C, 0xF185, 0xF186, 0xF1AB, 0xF1DE, 0xF1EB, 0xF240,
    0xF241, 0xF242, 0xF243, 0xF244, 0xF254, 0xF2ED, 0xF2F1, 0xF3A5, 0xF3C5, 0xF3CD,
    0xF53F, 0xF554, 0xF7C0, 0xF004, 0xF005, 0xF071, 0xF1C8, 0xF7A0, 0xF2DB, 0xF021,
    0xF6CF, 0xF522, 0xF1DA, 0xF0C7, 0xF067, 0xF068, 0xF073,
]
ICON_RANGE = ",".join(str(c) for c in ICONS)

FONTS = [
    # name,      size, text weight,   with icons
    ("font_xs",   17, "Medium",     False),   # small labels (calendar rings)
    ("font_s",    22, "Medium",     True),
    ("font_m",    30, "Medium",     True),
    ("font_l",    44, "SemiBold",   True),
    ("font_xl",  112, "ExtraLight", False),   # clock digits
    ("font_icon", 64, None,         True),
]

for name, size, weight, icons in FONTS:
    out = os.path.join(ROOT, "main", "fonts", name + ".c")
    args = [CONV, "--bpp", "4", "--size", str(size), "--format", "lvgl", "--no-compress",
            "--lv-include", "lvgl.h", "--force-fast-kern-format", "-o", out]
    if weight:
        rng = "0x20-0x3A" if name == "font_xl" else TEXT
        args += ["--font", TTF(weight), "-r", rng]
    if icons:
        args += ["--font", FA, "-r", ICON_RANGE]
    print(name, size)
    subprocess.check_call(args)
