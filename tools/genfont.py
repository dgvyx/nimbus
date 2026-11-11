#!/usr/bin/env python3
"""Gera a fonte de interface. Requer Pillow; veja assets/OFL.txt."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
font = ImageFont.truetype(str(ROOT / "assets/NotoSans-Regular.ttf"), 15)
pixels, offsets, widths = [], [], []
for code in range(32, 127):
    char = chr(code)
    width = int(font.getlength(char) + .99)
    image = Image.new("L", (width, 22))
    ImageDraw.Draw(image).text((0, 0), char, font=font, fill=255)
    offsets.append(len(pixels))
    widths.append(width)
    pixels.extend(image.getdata())
def array(name, kind, values):
    rows = [", ".join(map(str, values[i:i+24])) for i in range(0, len(values), 24)]
    return f"static const {kind} {name}[] = {{\n" + ",\n".join(rows) + "\n};\n"
(ROOT / "kernel/uifont.h").write_text(
    "/* Gerado por tools/genfont.py; Noto Sans, SIL OFL 1.1. */\n"
    + array("ui_glyph_offset", "unsigned short", offsets)
    + array("ui_glyph_width", "unsigned char", widths)
    + array("ui_glyph_alpha", "unsigned char", pixels), encoding="ascii")
