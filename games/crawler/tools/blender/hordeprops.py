# The yard's set dressing, headless:
#   flatpak run org.blender.Blender --background --factory-startup --python \
#     <abs>/tools/blender/hordeprops.py -- <abs out dir> [kind ...]
# kinds: wall lamp container barrier rubble tyres pipes
# Rigid props written as one-frame .cwchar files (see hordechars.py), with
# bevelled edges, wear colours and baked ambient occlusion. All shapes are
# generated here.
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector
from cwchar_lib import *

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if not argv:
    sys.exit("usage: -- <out dir> [kinds]")
OUT = os.path.abspath(argv[0]); kinds = argv[1:]
os.makedirs(OUT, exist_ok=True)

def base():
    M = Model()
    M.bone("root", None, (0, 0, 0), (0, 0, 1))
    return M

def make_wall():  # 4 m panel, faces -Y (the yard side); 3 m tall with a coping, pilasters and a drainage slot
    M = base()
    c = (0.42, 0.41, 0.39); c2 = (0.34, 0.33, 0.32); dark = (0.12, 0.12, 0.12)
    M.box("root", (0, 0, 1.4), (4.0, 0.5, 2.8), c, bevel=0.04, noise_amt=0.2)
    M.box("root", (0, 0, 2.95), (4.1, 0.7, 0.2), c2, bevel=0.04)
    for x in (-1.95, 1.95):
        M.box("root", (x, -0.05, 1.5), (0.25, 0.66, 3.0), c2, bevel=0.03)
    M.box("root", (0, -0.26, 0.28), (3.0, 0.04, 0.3), dark, bevel=0.01)
    M.box("root", (0, -0.26, 1.7), (3.2, 0.03, 0.06), c2, bevel=0.01)
    M.box("root", (0, -0.26, 2.3), (3.2, 0.03, 0.06), c2, bevel=0.01)
    return M

def make_lamp():  # a 7 m floodlight mast with an arm and a lamp bank
    M = base()
    steel = (0.22, 0.23, 0.25); glow = (6.0, 5.2, 3.8); dark = (0.08, 0.08, 0.09)
    M.box("root", (0, 0, 0.2), (0.7, 0.7, 0.4), (0.4, 0.4, 0.38), bevel=0.04)
    M.limb("root", (0, 0, 0.3), (0, 0, 6.8), 0.17, 0.09, steel, seg=8, noise_amt=0.05)
    M.limb("root", (0, 0, 6.6), (0, -1.3, 7.1), 0.06, 0.05, steel, seg=6)
    M.box("root", (0, -1.4, 7.1), (1.1, 0.5, 0.25), dark, bevel=0.03)
    for k in range(3):
        M.box("root", (-0.36 + 0.36 * k, -1.66, 7.09), (0.28, 0.06, 0.18), glow, bevel=0.0, group=1)
    return M

def make_container():  # a shipping container, 6 x 2.4 x 2.6, ribbed sides
    M = base()
    c = (0.5, 0.17, 0.12); dark = (0.1, 0.1, 0.1); c2 = (0.42, 0.14, 0.1)
    M.box("root", (0, 0, 1.3), (6.0, 2.4, 2.4), c, bevel=0.05, noise_amt=0.25)
    for k in range(-14, 15):
        x = k * 0.2
        M.box("root", (x, -1.22, 1.3), (0.09, 0.06, 2.3), c2, bevel=0.0, noise_amt=0.2)
        M.box("root", (x, 1.22, 1.3), (0.09, 0.06, 2.3), c2, bevel=0.0, noise_amt=0.2)
    for sx in (-3.0, 3.0):
        M.box("root", (sx, 0, 1.3), (0.12, 2.5, 2.5), dark, bevel=0.02)
    M.box("root", (0, 0, 2.55), (6.1, 2.5, 0.12), dark, bevel=0.02)
    M.box("root", (0, 0, 0.08), (6.1, 2.5, 0.16), dark, bevel=0.02)
    return M

def make_barrier():  # a concrete traffic barrier, 2.4 m
    M = base()
    c = (0.55, 0.54, 0.5); st = (0.8, 0.55, 0.1)
    M.box("root", (0, 0, 0.12), (2.4, 0.62, 0.24), c, bevel=0.03, noise_amt=0.2)
    M.box("root", (0, 0, 0.5), (2.4, 0.4, 0.55), c, bevel=0.05, noise_amt=0.2)
    M.box("root", (0, 0, 0.88), (2.4, 0.22, 0.3), c, bevel=0.05, noise_amt=0.2)
    for k in range(5):
        M.box("root", (-0.96 + 0.48 * k, -0.215, 0.55), (0.22, 0.02, 0.2), st if k % 2 == 0 else (0.1, 0.1, 0.1), bevel=0.0, rot=(0, 0, 0))
    return M

def make_rubble():
    import random
    r = random.Random(5)
    M = base()
    for k in range(12):
        s = r.uniform(0.25, 0.7)
        g = r.uniform(0.3, 0.55)
        M.box("root", (r.uniform(-0.8, 0.8), r.uniform(-0.6, 0.6), s * 0.3 + r.uniform(0, 0.3)), (s * r.uniform(0.8, 1.6), s * r.uniform(0.8, 1.4), s * 0.7),
              (g, g * 0.97, g * 0.92), rot=(r.uniform(-0.4, 0.4), r.uniform(-0.4, 0.4), r.uniform(0, 3)), bevel=0.05, noise_amt=0.25)
    for k in range(3):  # rebar
        M.limb("root", (r.uniform(-0.5, 0.5), r.uniform(-0.4, 0.4), 0.4), (r.uniform(-0.9, 0.9), r.uniform(-0.7, 0.7), r.uniform(0.9, 1.4)), 0.025, 0.02, (0.35, 0.2, 0.12), seg=5)
    return M

def make_tyres():
    M = base()
    k = (0.06, 0.06, 0.065)
    for i, (x, y, z) in enumerate([(0, 0, 0.17), (0.05, 0.03, 0.5), (0.9, 0.1, 0.17), (-0.85, -0.2, 0.17), (0.4, 0.85, 0.17)]):
        M.limb("root", (x, y, z - 0.15), (x, y, z + 0.15), 0.46, 0.46, k, seg=14, noise_amt=0.15)
        M.ell("root", (x, y, z + 0.15), (0.3, 0.3, 0.05), (0.03, 0.03, 0.03), seg=(10, 4))
    return M

def make_pipes():
    M = base()
    for i, (y, z, r, c) in enumerate([(-0.4, 0.3, 0.25, (0.35, 0.36, 0.38)), (0.1, 0.3, 0.25, (0.5, 0.3, 0.12)), (-0.15, 0.7, 0.25, (0.3, 0.33, 0.36))]):
        M.limb("root", (-1.6, y, z), (1.6, y, z), r, r, c, seg=12, noise_amt=0.12)
        for x in (-1.6, 1.6):
            M.limb("root", (x * 0.97, y, z), (x * 1.0, y, z), r * 1.12, r * 1.12, (0.2, 0.2, 0.22), seg=12)
    return M

PROPS = {"wall": make_wall, "lamp": make_lamp, "container": make_container, "barrier": make_barrier, "rubble": make_rubble, "tyres": make_tyres, "pipes": make_pipes}
for name in (kinds or list(PROPS)):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    M = PROPS[name]()
    ob, ao = build_object(M, name)
    pose_frame(ao, {})
    aoc = bake_ao(ob, M, 48)
    clips = [bake_clip(ob, M, ao, [{}])]
    size = write_cwchar(os.path.join(OUT, name + ".cwchar"), M, aoc, clips, ob)
    print("HORDEPROP %s: %d verts, %d tris, %d bytes" % (name, len(M.v), sum(len(p.vertices) - 2 for p in ob.data.polygons), size))
