#!/usr/bin/env python3
"""Build the small, biome-blended terrain atlas from CC0 Poly Haven photos."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets/textures/terrain-atlas.jpg"
BASE = "https://dl.polyhaven.org/file/ph-assets/Textures/jpg"
SOURCES = {
    "rock": f"{BASE}/2k/rocky_terrain/rocky_terrain_diff_2k.jpg",
    "forest": f"{BASE}/1k/rocky_terrain_02/rocky_terrain_02_diff_1k.jpg",
    "coast": f"{BASE}/1k/coast_sand_01/coast_sand_01_diff_1k.jpg",
    "sand": f"{BASE}/1k/sand_03/sand_03_diff_1k.jpg",
}


def tile(path):
    src = np.asarray(Image.open(path).convert("RGB").resize((256, 256), Image.Resampling.LANCZOS))
    rows = []
    for y in range(8):
        cells = []
        for x in range(4):
            cell = src
            if x & 1:
                cell = cell[:, ::-1]
            if y & 1:
                cell = cell[::-1]
            cells.append(cell)
        rows.append(np.concatenate(cells, axis=1))
    return np.concatenate(rows, axis=0).astype(np.float32)


def smooth(x, lo, hi):
    t = np.clip((x - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


with TemporaryDirectory() as folder:
    photos = {}
    for name, url in SOURCES.items():
        path = Path(folder) / f"{name}.jpg"
        run(["curl", "-L", "--fail", "-sS", url, "-o", str(path)], check=True)
        photos[name] = tile(path)

    z = (-385 + 440 * np.arange(2048, dtype=np.float32) / 2048)[:, None, None]
    x = (-110 + 220 * np.arange(1024, dtype=np.float32) / 1024)[None, :, None]
    img = photos["rock"]
    forest = smooth(z, -190, -165) * (1 - smooth(z, -105, -75))
    img = img * (1 - forest) + photos["forest"] * forest
    desert = 1 - smooth(z, -280, -250)
    img = img * (1 - desert) + photos["sand"] * desert
    coast = smooth(z, -115, -70)
    img = img * (1 - coast) + photos["coast"] * coast
    beach = smooth(x, 24, 43)
    img = img * (1 - beach) + photos["sand"] * beach
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").save(OUT, quality=88, optimize=True)
    print(OUT)
