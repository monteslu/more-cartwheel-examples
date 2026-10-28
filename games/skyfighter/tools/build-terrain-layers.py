#!/usr/bin/env python3
"""Pack CC0 Poly Haven material maps into Terrain3D's RGBA layer format."""
from pathlib import Path
import subprocess
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/source-terrain"
OUT = ROOT / "assets/terrain"
SOURCE.mkdir(exist_ok=True)
OUT.mkdir(exist_ok=True)
for name, asset in (("sand", "sand_03"), ("rock", "aerial_rocks_02"), ("coast", "coast_sand_01"), ("grass", "aerial_grass_rock"), ("snow", "snow_03")):
    maps = {}
    for kind in ("diff", "nor_gl", "rough"):
        path = SOURCE / f"{asset}_{kind}_4k.jpg"
        if not path.exists():
            url = f"https://dl.polyhaven.org/file/ph-assets/Textures/jpg/4k/{asset}/{asset}_{kind}_4k.jpg"
            subprocess.run(["curl", "-fsSL", url, "-o", str(path)], check=True)
        maps[kind] = Image.open(path).convert("RGB").resize((1024, 1024), Image.Resampling.LANCZOS)
    albedo = maps["diff"].convert("RGBA")
    albedo.putalpha(ImageOps.grayscale(maps["diff"]))
    # These scan normals are too strong at aircraft scale; retain grain
    # without making the small repeated tile read as a field of furrows.
    strength = {"sand": 0.22, "rock": 0.30, "coast": 0.30, "grass": 0.18, "snow": 0.12}[name]
    flat = Image.new("RGB", (1024, 1024), (128, 128, 255))
    normal = Image.blend(flat, maps["nor_gl"], strength).convert("RGBA")
    normal.putalpha(ImageOps.grayscale(maps["rough"]))
    (OUT / f"{name}-albedo.rgba").write_bytes(albedo.tobytes())
    (OUT / f"{name}-normal.rgba").write_bytes(normal.tobytes())
