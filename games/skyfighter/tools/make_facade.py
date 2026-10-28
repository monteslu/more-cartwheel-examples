#!/usr/bin/env python3
"""Make a compact weathered city facade from the CC0 concrete photo."""
from pathlib import Path
import random

from PIL import Image, ImageDraw, ImageEnhance

ROOT = Path(__file__).resolve().parents[1]
source = Image.open(ROOT / "assets/textures/concrete-wall.jpg").convert("RGB")
base = source.resize((512, 1024), Image.Resampling.LANCZOS)
base = ImageEnhance.Brightness(base).enhance(0.78)
draw = ImageDraw.Draw(base)
rng = random.Random(20261016)
for row in range(9):
    for col in range(4):
        x = 24 + col * 122
        y = 23 + row * 110
        w, h = 86, 75
        draw.rectangle((x-5, y-6, x+w+5, y+h+8), fill=(59, 65, 65))
        draw.rectangle((x-2, y-3, x+w+2, y+h+3), fill=(18, 27, 32))
        shade = rng.randrange(-13, 14)
        for yy in range(y, y+h):
            t = (yy-y)/h
            gleam = int(17*(1-t)+10*(t<0.20))
            color = (max(0, 37+shade+gleam), max(0, 55+shade+gleam),
                     max(0, 67+shade+gleam))
            draw.line((x, yy, x+w, yy), fill=color)
        draw.line((x+w//2, y, x+w//2, y+h), fill=(36, 42, 44), width=4)
        draw.line((x, y+int(h*0.61), x+w, y+int(h*0.61)), fill=(51, 57, 58), width=3)
    draw.rectangle((0, 100+row*110, 512, 104+row*110), fill=(50, 52, 50))
base.save(ROOT / "assets/textures/city-facade.jpg", quality=87, optimize=True)
