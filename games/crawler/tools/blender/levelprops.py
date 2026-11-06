# The descent's set dressing, hatches, gun pickups and the loot cache (LEVELS.md), headless:
#   flatpak run org.blender.Blender --background --factory-startup --python \
#     <abs>/tools/blender/levelprops.py -- <abs out dir> [kind ...] [--sheet <abs png dir>]
# kinds: see PROPS at the end (default all).
#
# Rigid props are one-frame .cwchar files (see hordechars.py) with bevelled edges, wear in the vertex
# colours and baked ambient occlusion; group 1 is a prop's glow (unlit, its vertex colours as HDR).
# A hatch is clip 0, six frames from shut to open, each moving part on its own bone. A loot cache is
# clip 0, four frames from shut to open. Every shape here is generated (no outside art).
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy, bmesh
from mathutils import Vector, Matrix
from cwchar_lib import *

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if not argv:
    sys.exit("usage: -- <out dir> [kinds] [--sheet <dir>]")
OUT = os.path.abspath(argv[0])
SHEET = None
kinds = []
i = 1
while i < len(argv):
    if argv[i] == "--sheet":
        SHEET = os.path.abspath(argv[i + 1]); i += 2
    else:
        kinds.append(argv[i]); i += 1
os.makedirs(OUT, exist_ok=True)
TAU = 2 * math.pi

def base():
    M = Model()
    M.bone("root", None, (0, 0, 0), (0, 0, 1))
    return M

def ring(M, bone, z0, z1, r_in0, r_in1, r_out0, r_out1, color, seg=24, group=0, noise_amt=0.1, w=None, a0=0.0, a1=TAU):
    """A thick ring (a tube wall): outer radius r_out0 at z0 to r_out1 at z1, inner r_in0 to r_in1, with a
    flat top and bottom. a0..a1 makes an arc."""
    bm = bmesh.new()
    full = abs(a1 - a0 - TAU) < 1e-6
    n = seg if full else seg + 1
    rows = []
    for (r, z) in ((r_out0, z0), (r_out1, z1), (r_in1, z1), (r_in0, z0)):
        rows.append([bm.verts.new((r * math.cos(a0 + (a1 - a0) * k / seg), r * math.sin(a0 + (a1 - a0) * k / seg), z)) for k in range(n)])
    for j in range(4):
        A, B = rows[j], rows[(j + 1) % 4]
        for k in range(seg):
            k2 = (k + 1) % n
            bm.faces.new((A[k], A[k2], B[k2], B[k]))
    if not full:
        for k in (0, n - 1):
            f = [rows[j][k] for j in range(4)]
            bm.faces.new(f if k == 0 else f[::-1])
    bm.normal_update()
    bm.verts.index_update()
    uv = bm.loops.layers.uv.new()
    for f in bm.faces:
        for l in f.loops:
            co = l.vert.co
            l[uv].uv = (math.atan2(co.y, co.x) / TAU * 4, co.z + math.hypot(co.x, co.y) * 0.5)
    M._add(bm, Matrix.Identity(4), w if w is not None else {bone: 1.0}, color, group, False, (1, 1), noise_amt)
    bm.free()

def slab(M, bone, pts, z0, z1, color, group=0, noise_amt=0.1, w=None):
    """A prism from a 2D outline (counter-clockwise), from z0 to z1."""
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in pts]
    hi = [bm.verts.new((x, y, z1)) for x, y in pts]
    bm.faces.new(hi)
    bm.faces.new(lo[::-1])
    n = len(pts)
    for k in range(n):
        bm.faces.new((lo[k], lo[(k + 1) % n], hi[(k + 1) % n], hi[k]))
    bm.normal_update()
    bm.verts.index_update()
    uv = bm.loops.layers.uv.new()
    for f in bm.faces:
        for l in f.loops:
            l[uv].uv = (l.vert.co.x + l.vert.co.z * 0.3, l.vert.co.y + l.vert.co.z * 0.3)
    M._add(bm, Matrix.Identity(4), w if w is not None else {bone: 1.0}, color, group, False, (1, 1), noise_amt)
    bm.free()

def hatch_rig(M, parts):
    M.bone("root", None, (0, 0, 0), (0, 0, 1))
    for p in parts:
        M.bone(p, "root", (0, 0, 0), (0, 0, 1))

def slide(dx, dy, down=0.0, rx=0.0, ry=0.0, rz=0.0):
    """A pose for an upright bone at the origin: move by (dx, dy) in the floor plane and down, and rotate
    (radians, about the bone's local axes: x = world x, y = world up, z = world -y)."""
    return (rx, ry, rz, dx, -down, -dy)

def plinth(M, color, edge, r_open=1.3, r_top=2.35, r_foot=2.85, h=0.38, seg=28):
    """The hatch's base: a low ring with a sloped outer edge (walkable), a dark well inside going down to
    the floor and a few steps descending on its north side."""
    ring(M, "root", 0.0, h, r_open, r_open, r_foot, r_top, color, seg=seg, noise_amt=0.15)
    ring(M, "root", h - 0.02, h + 0.03, r_open, r_open, r_open + 0.22, r_open + 0.22, edge, seg=seg, noise_amt=0.1)   # the lip
    for k in range(4):                                                                              # steps going down
        z = h - 0.09 * (k + 1)
        M.box("root", (0, 0.95 - 0.22 * k, z / 2), (1.4 - 0.1 * k, 0.24, z), color, bevel=0.01, noise_amt=0.15)

def well_glow(M, color, r=1.28, group=1):
    """The light down the well: a glowing floor disc just above the ground and a band low on the well
    wall (unlit, group 1)."""
    M.ell("root", (0, 0, 0.012), (r - 0.05, r - 0.05, 0.008), color, seg=(20, 3), group=group)
    ring(M, "root", 0.012, 0.13, r - 0.01, r - 0.01, r, r, tuple(c * 0.6 for c in color), seg=24, group=group, noise_amt=0.0)

# ───────────────────────── hatches ───────────────────────────────────

def make_yard_door():  # a freight lift: hazard-striped steel doors that split east and west and sink
    M = Model()
    hatch_rig(M, ["doorE", "doorW"])
    conc = (0.42, 0.41, 0.39); steel = (0.24, 0.25, 0.27); yel = (0.75, 0.55, 0.08); dark = (0.07, 0.07, 0.08)
    plinth(M, conc, steel)
    for k in range(8):                                                                              # bollards round the edge
        a = TAU * k / 8 + 0.39
        M.limb("root", (2.55 * math.cos(a), 2.55 * math.sin(a), 0.2), (2.55 * math.cos(a), 2.55 * math.sin(a), 0.75), 0.08, 0.08, yel, seg=8)
    for s, part in ((1, "doorE"), (-1, "doorW")):
        x0 = s * 0.66
        M.box(part, (x0, 0, 0.36), (1.3, 2.6, 0.06), steel, bevel=0.015, noise_amt=0.2)
        for k in range(5):                                                                          # hazard stripes
            M.box(part, (x0, -1.0 + 0.5 * k, 0.395), (1.15, 0.16, 0.012), yel if k % 2 == 0 else dark, rot=(0, 0, 0.6 * s), bevel=0.0)
    well_glow(M, (3.0, 1.9, 0.6))
    frames = [{"doorE": slide(1.2 * t, 0, 0.3 * t), "doorW": slide(-1.2 * t, 0, 0.3 * t)} for t in [k / 5 for k in range(6)]]
    return M, frames

def make_sluice_door():  # an iris: six iron leaves turn and slide into the brick collar
    M = Model()
    leaves = ["leaf%d" % k for k in range(6)]
    hatch_rig(M, leaves)
    brick = (0.33, 0.2, 0.15); iron = (0.2, 0.17, 0.15); rust = (0.42, 0.22, 0.1); moss = (0.18, 0.24, 0.14)
    plinth(M, brick, iron)
    ring(M, "root", 0.3, 0.42, 2.05, 2.05, 2.4, 2.35, moss, seg=28, noise_amt=0.35)                  # slime on the collar
    for k in range(12):                                                                             # bolts
        a = TAU * k / 12
        M.ell("root", (1.75 * math.cos(a), 1.75 * math.sin(a), 0.4), (0.06, 0.06, 0.04), iron, seg=(6, 3))
    for k, part in enumerate(leaves):
        a0 = TAU * k / 6
        pts = [(0, 0)] + [(1.45 * math.cos(a0 + TAU / 6 * j / 4 - 0.12), 1.45 * math.sin(a0 + TAU / 6 * j / 4 - 0.12)) for j in range(5)]
        slab(M, part, pts, 0.32, 0.38, iron if k % 2 else rust, noise_amt=0.25)
        M.box(part, (0.75 * math.cos(a0 + 0.4), 0.75 * math.sin(a0 + 0.4), 0.4), (0.5, 0.06, 0.03), rust, rot=(0, 0, a0 + 0.4), bevel=0.005)
    well_glow(M, (0.4, 2.6, 1.6))
    frames = []
    for t in [k / 5 for k in range(6)]:
        p = {}
        for k, part in enumerate(leaves):
            a = TAU * k / 6 + 0.52
            p[part] = slide(1.25 * t * math.cos(a), 1.25 * t * math.sin(a), 0.3 * t, ry=0.5 * t)
        frames.append(p)
    return M, frames

def make_ossuary_door():  # a crypt stair: a carved stone lid grinds aside off a stepped plinth
    M = Model()
    hatch_rig(M, ["lid"])
    stone = (0.4, 0.37, 0.32); dark = (0.18, 0.16, 0.14); bone_c = (0.78, 0.72, 0.58)
    plinth(M, stone, dark)
    ring(M, "root", 0.0, 0.18, 2.3, 2.3, 3.1, 3.0, dark, seg=8, noise_amt=0.2)                       # an octagonal lower step
    for k in range(8):                                                                              # skulls set in the step
        a = TAU * k / 8 + 0.39
        M.ell("root", (2.7 * math.cos(a), 2.7 * math.sin(a), 0.2), (0.11, 0.12, 0.1), bone_c, seg=(7, 5))
    pts = [(1.5 * math.cos(TAU * k / 8 + 0.39), 1.5 * math.sin(TAU * k / 8 + 0.39)) for k in range(8)]
    slab(M, "lid", pts, 0.33, 0.48, stone, noise_amt=0.2)
    M.box("lid", (0, 0, 0.5), (0.2, 1.8, 0.05), dark, bevel=0.01)                                     # a carved cross of grooves
    M.box("lid", (0, 0.25, 0.5), (1.3, 0.2, 0.05), dark, bevel=0.01)
    M.ell("lid", (0, -0.5, 0.52), (0.18, 0.2, 0.08), bone_c, seg=(8, 4))                               # a skull boss
    well_glow(M, (3.2, 1.8, 0.5))
    frames = [{"lid": slide(0.15 * t, -2.0 * t, 0.06 * math.sin(t * math.pi) - 0.1 * t, ry=0.35 * t)} for t in [k / 5 for k in range(6)]]
    return M, frames

# ───────────────────────── level 2: the sluice ──────────────────────

BRICK = (0.34, 0.2, 0.15); BRICK2 = (0.27, 0.16, 0.13); MORTAR = (0.4, 0.38, 0.34); MOSS = (0.17, 0.23, 0.13)
CONC = (0.36, 0.36, 0.34); IRON = (0.2, 0.18, 0.16); RUST = (0.4, 0.21, 0.1)

def brick_courses(M, x0, x1, z0, z1, y, rng_seed, depth=0.5):
    """Bricks in courses on a wall face at y (facing -Y), with jitter in shade and depth."""
    import random
    r = random.Random(rng_seed)
    h = 0.24; z = z0
    row = 0
    while z + h <= z1 + 1e-6:
        L = 0.55
        x = x0 + (L / 2 if row % 2 else 0)
        while x < x1 - 0.05:
            w = min(L, x1 - x) - 0.03
            if w > 0.1:
                c = BRICK if r.random() < 0.6 else BRICK2
                k = 0.85 + 0.3 * r.random()
                M.box("root", (x + w / 2, y - r.random() * 0.02, z + h / 2), (w, 0.06, h - 0.03), (c[0] * k, c[1] * k, c[2] * k), bevel=0.012, noise_amt=0.15)
            x += L
        z += h; row += 1

def make_sluice_wall():  # a 4 m brick vault bay, 3.6 m tall: an arched recess between piers, slime at the foot
    M = base()
    M.box("root", (0, 0.25, 1.8), (4.0, 0.5, 3.6), MORTAR, bevel=0.03, noise_amt=0.2)
    for x in (-1.85, 1.85):
        M.box("root", (x, -0.08, 1.8), (0.42, 0.62, 3.6), BRICK2, bevel=0.03, noise_amt=0.25)
    brick_courses(M, -1.6, 1.6, 0.0, 1.9, -0.02, 3)
    # the arch: a half ring of voussoirs over the recess
    for k in range(9):
        a = math.pi * (k + 0.5) / 9
        M.box("root", (1.6 * math.cos(a), -0.05, 1.9 + 1.4 * math.sin(a)), (0.3, 0.14, 0.42), BRICK2 if k % 2 else BRICK, rot=(0, -(a - math.pi / 2), 0), bevel=0.01)
    M.box("root", (0, 0.02, 2.6), (3.0, 0.1, 1.3), (0.1, 0.09, 0.08), bevel=0.01)                     # the dark of the recess
    M.box("root", (0, -0.22, 0.12), (4.0, 0.12, 0.24), MOSS, bevel=0.03, noise_amt=0.4)
    M.box("root", (0, -0.12, 3.5), (4.1, 0.75, 0.2), CONC, bevel=0.03)
    return M

def make_sluice_pier():  # a square brick pier with a stone cap and a band of slime, 1.6 m square, 2.6 m tall
    M = base()
    M.box("root", (0, 0, 1.25), (1.5, 1.5, 2.5), BRICK, bevel=0.04, noise_amt=0.3)
    for z in (0.5, 1.0, 1.5, 2.0):
        M.box("root", (0, 0, z), (1.54, 1.54, 0.04), MORTAR, bevel=0.0)
    M.box("root", (0, 0, 2.6), (1.7, 1.7, 0.2), CONC, bevel=0.04, noise_amt=0.15)
    M.box("root", (0, 0, 0.15), (1.6, 1.6, 0.3), MOSS, bevel=0.05, noise_amt=0.4)
    return M

def make_sluice_curb():  # a 4 m length of concrete channel edge, 0.25 m high, with iron rungs
    M = base()
    M.box("root", (0, 0, 0.12), (4.0, 0.4, 0.24), CONC, bevel=0.03, noise_amt=0.3)
    M.box("root", (0, 0.18, 0.02), (4.0, 0.08, 0.04), MOSS, bevel=0.01, noise_amt=0.4)
    for x in (-1.2, 1.2):
        M.limb("root", (x - 0.15, 0.2, 0.22), (x - 0.15, 0.3, 0.04), 0.02, 0.02, RUST, seg=5)
        M.limb("root", (x + 0.15, 0.2, 0.22), (x + 0.15, 0.3, 0.04), 0.02, 0.02, RUST, seg=5)
    return M

def make_sluice_gate():  # a rusted sluice gate in a concrete frame (the culvert mouths), 4 m wide, 3.4 m tall
    M = base()
    for x in (-1.85, 1.85):
        M.box("root", (x, 0, 1.7), (0.35, 0.6, 3.4), CONC, bevel=0.03, noise_amt=0.25)
    M.box("root", (0, 0, 3.55), (4.1, 0.7, 0.3), CONC, bevel=0.03)
    M.box("root", (0, 0.1, 2.45), (3.3, 0.12, 1.5), RUST, bevel=0.02, noise_amt=0.35)               # the gate, raised
    for k in range(4):
        M.box("root", (-1.2 + 0.8 * k, 0.02, 2.45), (0.12, 0.08, 1.45), IRON, bevel=0.01)
    M.limb("root", (0, 0.1, 3.7), (0, 0.1, 4.6), 0.06, 0.06, IRON, seg=6)                          # the screw
    M.ell("root", (0, 0.1, 4.6), (0.35, 0.35, 0.05), RUST, seg=(12, 3))                              # its wheel
    return M

def make_sluice_lamp():  # a failing sodium lamp on a bracket, the glow in group 1
    M = base()
    M.box("root", (0, 0.1, 0.3), (0.3, 0.3, 0.6), CONC, bevel=0.03)
    M.limb("root", (0, 0.1, 0.5), (0, 0.1, 3.4), 0.07, 0.06, IRON, seg=6)
    M.limb("root", (0, 0.1, 3.3), (0, -0.7, 3.5), 0.04, 0.04, IRON, seg=5)
    M.box("root", (0, -0.75, 3.42), (0.32, 0.5, 0.14), RUST, bevel=0.02)
    M.box("root", (0, -0.75, 3.33), (0.24, 0.4, 0.04), (5.0, 2.6, 0.7), bevel=0.0, group=1)
    return M

def make_sluice_crate():  # a waterlogged wooden crate, 1.3 m (the sim's crate)
    M = base()
    wood = (0.3, 0.22, 0.15); wood2 = (0.24, 0.18, 0.12)
    M.box("root", (0, 0, 0.65), (1.25, 1.25, 1.25), wood, bevel=0.03, noise_amt=0.3)
    for z in (0.1, 1.2):
        M.box("root", (0, 0, z), (1.32, 1.32, 0.12), wood2, bevel=0.015)
    for s in (1, -1):
        M.box("root", (0, s * 0.64, 0.65), (0.12, 0.04, 1.2), wood2, rot=(0, 0.78, 0), bevel=0.01)
    M.box("root", (0, 0, 0.08), (1.3, 1.3, 0.16), MOSS, bevel=0.02, noise_amt=0.4)
    return M

def make_sluice_drum():  # a rusted chemical drum, 0.6 m across (the sim's barrel: it bursts)
    M = base()
    M.limb("root", (0, 0, 0.0), (0, 0, 1.0), 0.32, 0.32, RUST, seg=14, noise_amt=0.35)
    for z in (0.3, 0.7):
        ring(M, "root", z - 0.02, z + 0.02, 0.32, 0.32, 0.34, 0.34, IRON, seg=14)
    M.ell("root", (0, 0, 1.0), (0.28, 0.28, 0.02), (0.55, 0.42, 0.08), seg=(12, 3))
    M.box("root", (0, -0.33, 0.55), (0.3, 0.02, 0.22), (0.7, 0.55, 0.1), bevel=0.0)                 # a hazard label
    return M

def make_sluice_pipe():  # a big culvert pipe mouth, scenery past the walls
    M = base()
    M.limb("root", (0, 2.0, 1.0), (0, -0.4, 1.0), 1.0, 1.0, CONC, seg=16, noise_amt=0.25)
    M.limb("root", (0, -0.39, 1.0), (0, -0.42, 1.0), 0.85, 0.85, (0.03, 0.04, 0.04), seg=16)
    M.box("root", (0, 0.6, 0.15), (2.4, 2.6, 0.3), CONC, bevel=0.04)
    return M

# ───────────────────────── level 3: the ossuary ─────────────────────

STONE = (0.4, 0.37, 0.32); STONE2 = (0.32, 0.29, 0.25); BONE = (0.78, 0.72, 0.58); BONE2 = (0.62, 0.56, 0.44)
SOOT = (0.12, 0.11, 0.1); IRON3 = (0.16, 0.15, 0.14)

def skull(M, c, s=1.0, yaw=0.0, color=BONE):
    x, y, z = c
    M.ell("root", (x, y, z), (0.1 * s, 0.11 * s, 0.1 * s), color, seg=(6, 4), noise_amt=0.15)
    dx, dy = math.sin(yaw), -math.cos(yaw)
    for e in (-1, 1):
        M.ell("root", (x + dx * 0.085 * s + e * 0.035 * s * math.cos(yaw), y + dy * 0.085 * s + e * 0.035 * s * math.sin(yaw), z + 0.01 * s), (0.026 * s, 0.026 * s, 0.03 * s), SOOT, seg=(4, 3))

def make_ossuary_wall():  # a 4 m, 2.2 m tall wall of stacked skulls and long bones between stone courses
    M = base()
    import random
    r = random.Random(5)
    M.box("root", (0, 0.12, 1.1), (4.0, 0.5, 2.2), STONE2, bevel=0.03, noise_amt=0.2)
    for zc, kind in ((0.15, "stone"), (0.45, "skull"), (0.75, "bones"), (1.05, "skull"), (1.35, "bones"), (1.65, "skull"), (2.0, "stone")):
        if kind == "stone":
            M.box("root", (0, -0.05, zc), (4.1, 0.62, 0.3 if zc < 1 else 0.36), STONE, bevel=0.03, noise_amt=0.2)
        elif kind == "skull":
            for k in range(14):
                x = -1.86 + k * 0.286 + r.uniform(-0.02, 0.02)
                skull(M, (x, -0.16, zc), s=1.15 + r.uniform(-0.1, 0.1), yaw=r.uniform(-0.25, 0.25), color=BONE if r.random() < 0.7 else BONE2)
        else:
            for k in range(7):
                M.limb("root", (-1.95, -0.13, zc - 0.1 + 0.04 * k), (1.95, -0.13, zc - 0.1 + 0.04 * k + r.uniform(-0.02, 0.02)), 0.035, 0.035, BONE2 if k % 2 else BONE, seg=5, noise_amt=0.2)
    return M

def make_ossuary_column():  # a squat clustered column with a capital, 1.3 m square, 3.2 m
    M = base()
    M.box("root", (0, 0, 0.2), (1.4, 1.4, 0.4), STONE, bevel=0.05, noise_amt=0.2)
    for a in range(4):
        x, y = 0.28 * math.cos(a * math.pi / 2 + 0.78), 0.28 * math.sin(a * math.pi / 2 + 0.78)
        M.limb("root", (x, y, 0.4), (x, y, 2.7), 0.3, 0.28, STONE2, seg=10, noise_amt=0.2)
    M.limb("root", (0, 0, 0.4), (0, 0, 2.7), 0.38, 0.36, STONE, seg=10, noise_amt=0.2)
    M.box("root", (0, 0, 2.85), (1.4, 1.4, 0.3), STONE, bevel=0.05, noise_amt=0.2)
    M.box("root", (0, 0, 3.08), (1.6, 1.6, 0.16), STONE2, bevel=0.04)
    for a in range(4):                                                                               # skulls round the capital
        yaw = a * math.pi / 2
        skull(M, (0.72 * math.sin(yaw), -0.72 * math.cos(yaw), 2.85), s=1.0, yaw=yaw)
    return M

def make_ossuary_bonepile():  # a heap of bones and skulls, 1.4 m across (the sim's crate: it bursts into gems)
    M = base()
    import random
    r = random.Random(9)
    M.limb("root", (0, 0, 0.0), (0, 0, 0.32), 0.72, 0.5, BONE2, seg=9, noise_amt=0.45)               # the heap, on the floor
    M.ell("root", (0, 0, 0.3), (0.5, 0.46, 0.2), BONE2, seg=(8, 4), noise_amt=0.45)
    for k in range(10):
        a = r.uniform(0, TAU); d = r.uniform(0.1, 0.55)
        z = 0.35 - d * 0.4
        M.limb("root", (d * math.cos(a), d * math.sin(a), z), (d * math.cos(a) + 0.5 * math.cos(a + 1.5), d * math.sin(a) + 0.5 * math.sin(a + 1.5), z + r.uniform(-0.1, 0.15)), 0.035, 0.035, BONE, seg=5, noise_amt=0.2)
    for k in range(5):
        a = r.uniform(0, TAU); d = r.uniform(0.0, 0.45)
        skull(M, (d * math.cos(a), d * math.sin(a), 0.42 - d * 0.35), s=1.1, yaw=r.uniform(0, TAU))
    return M

def make_ossuary_urn():  # a squat iron-bound oil urn (the sim's barrel: it bursts into fire)
    M = base()
    clay = (0.4, 0.25, 0.16)
    M.ell("root", (0, 0, 0.45), (0.36, 0.36, 0.45), clay, seg=(12, 7), noise_amt=0.25)
    M.limb("root", (0, 0, 0.82), (0, 0, 1.0), 0.18, 0.22, clay, seg=12)
    for z in (0.3, 0.6):
        ring(M, "root", z - 0.03, z + 0.03, 0.33, 0.34, 0.37, 0.37, IRON3, seg=14)
    M.ell("root", (0, 0, 1.0), (0.17, 0.17, 0.02), SOOT, seg=(10, 3))
    return M

def make_ossuary_brazier():  # an iron brazier on three legs, coals glowing in group 1
    M = base()
    for k in range(3):
        a = TAU * k / 3
        M.limb("root", (0.45 * math.cos(a), 0.45 * math.sin(a), 0.0), (0.2 * math.cos(a), 0.2 * math.sin(a), 0.85), 0.04, 0.035, IRON3, seg=5)
    ring(M, "root", 0.85, 1.15, 0.3, 0.42, 0.34, 0.46, IRON3, seg=16)
    M.ell("root", (0, 0, 0.88), (0.34, 0.34, 0.05), SOOT, seg=(12, 3))
    M.ell("root", (0, 0, 1.0), (0.32, 0.32, 0.1), (5.0, 1.8, 0.4), seg=(10, 4), group=1)
    for k in range(5):
        a = TAU * k / 5 + 0.3
        M.spike("root", (0.15 * math.cos(a), 0.15 * math.sin(a), 1.02), (0.08 * math.cos(a), 0.08 * math.sin(a), 1.4 + 0.1 * (k % 2)), 0.09, (6.0, 2.6, 0.5), seg=5, group=1)
    return M

def make_ossuary_candles():  # a cluster of melted candles on a stone block
    M = base()
    import random
    r = random.Random(4)
    wax = (0.85, 0.8, 0.66)
    M.box("root", (0, 0, 0.35), (0.9, 0.7, 0.7), STONE2, bevel=0.04, noise_amt=0.25)
    M.ell("root", (0, 0, 0.7), (0.4, 0.3, 0.06), wax, seg=(10, 3), noise_amt=0.3)                     # melted wax over the top
    for k in range(9):
        x, y = r.uniform(-0.32, 0.32), r.uniform(-0.22, 0.22)
        h = r.uniform(0.12, 0.45)
        M.limb("root", (x, y, 0.7), (x, y, 0.7 + h), 0.035, 0.03, wax, seg=6)
        M.ell("root", (x, y, 0.72 + h), (0.016, 0.016, 0.04), (6.0, 3.6, 1.0), seg=(4, 3), group=1)
    return M

def make_ossuary_sarcophagus():  # a stone coffin with a carved lid, 2.4 x 1.1 m
    M = base()
    M.box("root", (0, 0, 0.45), (1.1, 2.4, 0.9), STONE2, bevel=0.04, noise_amt=0.2)
    M.box("root", (0, 0, 0.98), (1.2, 2.5, 0.16), STONE, bevel=0.04, noise_amt=0.15)
    M.ell("root", (0, 0.3, 1.1), (0.3, 0.75, 0.1), STONE, seg=(8, 4), noise_amt=0.2)                 # the effigy
    M.ell("root", (0, -0.75, 1.12), (0.18, 0.2, 0.12), STONE, seg=(7, 4))
    for s in (1, -1):
        for k in range(3):
            skull(M, (s * 0.56, -0.7 + 0.7 * k, 0.55), s=0.8, yaw=-s * math.pi / 2)
    return M

def make_ossuary_outer():  # the outer vault wall, 4 m: deep alcoves of stacked dead, 3.6 m tall
    M = base()
    M.box("root", (0, 0.35, 1.8), (4.0, 0.7, 3.6), STONE2, bevel=0.03, noise_amt=0.2)
    for x in (-1.8, 1.8):
        M.box("root", (x, -0.1, 1.8), (0.4, 0.5, 3.6), STONE, bevel=0.03, noise_amt=0.2)
    for z in (0.9, 2.2):                                                                              # two rows of alcoves
        M.box("root", (0, 0.0, z), (3.0, 0.3, 0.95), SOOT, bevel=0.02)
        M.box("root", (0, -0.2, z - 0.5), (3.2, 0.3, 0.1), STONE, bevel=0.02)
        for k in range(8):
            skull(M, (-1.3 + 0.37 * k, -0.06, z - 0.33), s=1.05, yaw=0.2 * math.sin(k * 3.1))
        for k in range(4):
            M.limb("root", (-1.4, -0.05, z - 0.18 + 0.05 * k), (1.4, -0.05, z - 0.18 + 0.05 * k), 0.035, 0.035, BONE2, seg=5)
    M.box("root", (0, -0.1, 3.5), (4.1, 0.9, 0.2), STONE, bevel=0.03)
    return M

# ───────────────────────── level 4: the foundry ─────────────────────

SLAG = (0.09, 0.08, 0.08); IRON4 = (0.19, 0.18, 0.18); RUST4 = (0.33, 0.17, 0.09); SOOT4 = (0.06, 0.055, 0.05)
MOLTEN = (3.2, 1.0, 0.14); EMBER4 = (2.6, 0.65, 0.1); BRICK4 = (0.3, 0.16, 0.11)

def make_foundry_door():  # a crucible gate: four iron blast doors slide out under a riveted collar
    M = Model()
    petals = ["petal%d" % k for k in range(4)]
    hatch_rig(M, petals)
    plinth(M, SLAG, IRON4)
    ring(M, "root", 0.3, 0.44, 2.0, 2.0, 2.45, 2.4, RUST4, seg=24, noise_amt=0.3)                      # a riveted iron collar
    for k in range(16):
        a = TAU * k / 16
        M.ell("root", (2.2 * math.cos(a), 2.2 * math.sin(a), 0.45), (0.05, 0.05, 0.035), IRON4, seg=(5, 3))
    for k, part in enumerate(petals):
        a0 = TAU * k / 4 + math.pi / 4
        pts = [(0, 0)] + [(1.45 * math.cos(a0 - math.pi / 4 + math.pi / 2 * j / 4), 1.45 * math.sin(a0 - math.pi / 4 + math.pi / 2 * j / 4)) for j in range(5)]
        slab(M, part, pts, 0.32, 0.4, IRON4, noise_amt=0.25)
        M.box(part, (0.7 * math.cos(a0), 0.7 * math.sin(a0), 0.42), (0.9, 0.12, 0.04), RUST4, rot=(0, 0, a0), bevel=0.01)
    well_glow(M, (5.0, 1.6, 0.3))
    frames = []
    for t in [k / 5 for k in range(6)]:
        p = {}
        for k, part in enumerate(petals):
            a = TAU * k / 4 + math.pi / 4
            p[part] = slide(1.25 * t * math.cos(a), 1.25 * t * math.sin(a), 0.3 * t)   # each blast door slides out under the collar
        frames.append(p)
    return M, frames

def make_foundry_wall():  # a 4 m bay of soot-black foundry wall: brick below, an iron-plated upper course, 4 m tall
    M = base()
    M.box("root", (0, 0.3, 2.0), (4.0, 0.6, 4.0), SOOT4, bevel=0.03, noise_amt=0.25)
    import random
    r = random.Random(41)
    for row in range(7):
        z = 0.12 + row * 0.26
        x = -2.0 + (0.3 if row % 2 else 0)
        while x < 1.9:
            w = min(0.6, 2.0 - x) - 0.03
            k = 0.8 + 0.4 * r.random()
            M.box("root", (x + w / 2, -0.02, z), (w, 0.06, 0.23), (BRICK4[0] * k, BRICK4[1] * k, BRICK4[2] * k), bevel=0.01, noise_amt=0.2)
            x += 0.6
    M.box("root", (0, -0.05, 2.9), (4.0, 0.1, 1.6), IRON4, bevel=0.02, noise_amt=0.3)
    for x in (-1.5, -0.5, 0.5, 1.5):
        M.box("root", (x, -0.11, 2.9), (0.08, 0.04, 1.6), RUST4, bevel=0.0)
    M.box("root", (0, -0.15, 3.9), (4.1, 0.6, 0.2), IRON4, bevel=0.03)
    return M

def make_foundry_crucible():  # a crucible block: a 2.6 m iron pot on a brick base, glowing at the lip (heavy cover)
    M = base()
    M.box("root", (0, 0, 0.45), (2.6, 2.6, 0.9), BRICK4, bevel=0.05, noise_amt=0.3)
    M.limb("root", (0, 0, 0.9), (0, 0, 2.4), 1.0, 1.15, IRON4, seg=16, noise_amt=0.25)
    ring(M, "root", 2.35, 2.5, 1.0, 1.0, 1.25, 1.25, RUST4, seg=16)
    M.ell("root", (0, 0, 2.38), (0.98, 0.98, 0.05), MOLTEN, seg=(14, 3), group=1)
    for k in range(4):
        a = TAU * k / 4 + math.pi / 4
        M.box("root", (1.1 * math.cos(a), 1.1 * math.sin(a), 1.6), (0.18, 0.18, 1.4), IRON4, rot=(0, 0, a), bevel=0.02)
    return M

def make_foundry_cart():  # a slag cart (the sim's barrel: it bursts into a molten splash)
    M = base()
    M.box("root", (0, 0, 0.3), (1.0, 0.7, 0.2), IRON4, bevel=0.02)
    for x in (-0.35, 0.35):
        for y in (-0.38, 0.38):
            M.limb("root", (x, y - 0.04, 0.15), (x, y + 0.04, 0.15), 0.15, 0.15, SOOT4, seg=10)
    M.limb("root", (0, 0, 0.4), (0, 0, 0.95), 0.42, 0.5, RUST4, seg=12, noise_amt=0.3)
    M.ell("root", (0, 0, 0.92), (0.44, 0.44, 0.05), EMBER4, seg=(10, 3), group=1)
    return M

def make_foundry_ingots():  # a stack of iron ingots on a pallet (the sim's crate)
    M = base()
    M.box("root", (0, 0, 0.07), (1.3, 1.3, 0.14), (0.24, 0.17, 0.11), bevel=0.01)
    for layer in range(4):
        for k in range(4):
            w = (0.3, 1.2, 0.22) if layer % 2 == 0 else (1.2, 0.3, 0.22)
            x, y = ((-0.45 + 0.3 * k, 0) if layer % 2 == 0 else (0, -0.45 + 0.3 * k))
            M.box("root", (x, y, 0.25 + 0.23 * layer), w, IRON4 if (k + layer) % 3 else RUST4, bevel=0.02, noise_amt=0.2)
    return M

def make_foundry_vent():  # a floor vent (scenery near the molten channels): a grated box breathing smoke
    M = base()
    M.box("root", (0, 0, 0.12), (1.2, 1.2, 0.24), IRON4, bevel=0.03)
    for k in range(5):
        M.box("root", (-0.4 + 0.2 * k, 0, 0.25), (0.06, 1.0, 0.04), RUST4, bevel=0.0)
    M.box("root", (0, 0, 0.2), (1.0, 1.0, 0.02), EMBER4, bevel=0.0, group=1)
    return M

def make_foundry_lamp():  # a caged work lamp on a chain from a gantry post
    M = base()
    M.box("root", (0, 0, 0.2), (0.5, 0.5, 0.4), IRON4, bevel=0.03)
    M.box("root", (0, 0, 2.6), (0.22, 0.22, 4.8), IRON4, bevel=0.02)
    M.box("root", (0, -0.7, 4.9), (0.16, 1.6, 0.16), IRON4, bevel=0.02)
    M.limb("root", (0, -1.35, 4.85), (0, -1.35, 4.1), 0.02, 0.02, IRON4, seg=4)
    M.ell("root", (0, -1.35, 3.95), (0.2, 0.2, 0.22), (5.0, 3.0, 1.2), seg=(8, 5), group=1)
    for k in range(4):
        a = TAU * k / 4
        M.limb("root", (0.22 * math.cos(a), -1.35 + 0.22 * math.sin(a), 4.15), (0.22 * math.cos(a), -1.35 + 0.22 * math.sin(a), 3.75), 0.015, 0.015, IRON4, seg=4)
    return M

# ───────────────────────── level 5: the grotto ──────────────────────

ROCK5 = (0.16, 0.13, 0.17); ROCK5B = (0.22, 0.18, 0.2); MOSS5 = (0.12, 0.2, 0.16); STEM5 = (0.6, 0.57, 0.52)
CAP5 = (0.34, 0.18, 0.36); LUM5 = (0.4, 3.6, 3.0); LUM5G = (1.2, 4.0, 0.9); ROOT5 = (0.3, 0.25, 0.22)

def make_grotto_door():  # a root pit: a knot of grey roots on a mossy mound unclenches outward like a flower
    M = Model()
    roots = ["root%d" % k for k in range(6)]
    hatch_rig(M, roots)
    plinth(M, MOSS5, ROOT5)
    for k in range(10):                                                                              # glowing moss lumps on the mound
        a = TAU * k / 10 + 0.2
        M.ell("root", (2.0 * math.cos(a), 2.0 * math.sin(a), 0.38), (0.12, 0.12, 0.05), LUM5 if k % 2 else LUM5G, seg=(6, 3), group=1)
    for k, part in enumerate(roots):
        a = TAU * k / 6
        b = (1.55 * math.cos(a), 1.55 * math.sin(a), 0.35)
        m = (0.7 * math.cos(a + 0.5), 0.7 * math.sin(a + 0.5), 0.55)
        t = (0.08 * math.cos(a + 1.2), 0.08 * math.sin(a + 1.2), 0.45)
        M.limb(part, b, m, 0.22, 0.16, ROOT5, seg=7, noise_amt=0.3)
        M.limb(part, m, t, 0.16, 0.05, ROOT5, seg=7, noise_amt=0.3)
    well_glow(M, (0.4, 3.2, 2.6))
    frames = []
    for tt in [k / 5 for k in range(6)]:
        p = {}
        for k, part in enumerate(roots):
            a = TAU * k / 6
            p[part] = slide(1.1 * tt * math.cos(a), 1.1 * tt * math.sin(a), 0.2 * tt, ry=0.6 * tt)
        frames.append(p)
    return M, frames

def make_grotto_rock():  # a 4 m bulge of cave rock (the irregular arena edge), 4.5 m tall
    M = base()
    import random
    r = random.Random(57)
    M.ell("root", (0, 0.4, 1.9), (2.3, 1.0, 1.9), ROCK5, seg=(10, 7), noise_amt=0.4)
    for k in range(5):
        x = r.uniform(-1.6, 1.6)
        M.ell("root", (x, 0.1 + r.uniform(-0.2, 0.3), 1.0 + r.uniform(0, 2.4)), (r.uniform(0.6, 1.0), r.uniform(0.5, 0.8), r.uniform(0.6, 1.0)), ROCK5B if k % 2 else ROCK5, seg=(8, 5), noise_amt=0.4)
    M.box("root", (0, 0.4, 0.15), (4.2, 1.6, 0.3), ROCK5, bevel=0.1, noise_amt=0.4)                   # its foot on the floor
    for k in range(4):
        x = -1.5 + k
        M.ell("root", (x, -0.55, 0.35 + 0.3 * (k % 2)), (0.1, 0.06, 0.08), LUM5 if k % 2 else LUM5G, seg=(5, 3), group=1)
    return M

def make_grotto_trunk():  # a giant pale fungus trunk with shelf caps, 1.6 m thick, 6 m tall
    M = base()
    M.limb("root", (0, 0, 0.0), (0, 0, 5.2), 0.85, 0.65, STEM5, seg=12, noise_amt=0.25)
    M.limb("root", (0, 0, 0.0), (0, 0, 0.4), 1.15, 0.85, STEM5, seg=12, noise_amt=0.3)
    M.ell("root", (0, 0, 5.4), (2.4, 2.4, 0.7), CAP5, seg=(14, 6), noise_amt=0.25)
    M.ell("root", (0, 0, 5.1), (2.2, 2.2, 0.12), LUM5, seg=(14, 3), group=1)
    for k in range(3):
        a = TAU * k / 3 + 0.4
        M.ell("root", (0.85 * math.cos(a), 0.85 * math.sin(a), 2.0 + 0.8 * k), (0.5, 0.5, 0.1), CAP5, seg=(9, 3), noise_amt=0.2)
    return M

def make_grotto_cluster():  # a cluster of mushrooms (the sim's crate: soft cover that bursts when shot)
    M = base()
    import random
    r = random.Random(61)
    for k in range(5):
        a = r.uniform(0, TAU); d = r.uniform(0.0, 0.45)
        h = r.uniform(0.4, 1.0); rc = r.uniform(0.25, 0.42)
        x, y = d * math.cos(a), d * math.sin(a)
        M.limb("root", (x, y, 0.0), (x, y, h), 0.08, 0.06, STEM5, seg=6)
        M.ell("root", (x, y, h), (rc, rc, rc * 0.45), CAP5, seg=(9, 4), noise_amt=0.2)
        M.ell("root", (x, y, h - 0.04), (rc * 0.92, rc * 0.92, 0.03), LUM5 if k % 2 else LUM5G, seg=(8, 3), group=1)
    return M

def make_grotto_pod():  # a spore pod on a stalk (the sim's barrel: it bursts into a spore cloud)
    M = base()
    M.limb("root", (0, 0, 0.0), (0, 0, 0.5), 0.12, 0.08, ROOT5, seg=7)
    M.ell("root", (0, 0, 0.75), (0.36, 0.36, 0.42), (0.28, 0.3, 0.16), seg=(10, 7), noise_amt=0.3)
    for k in range(6):
        a = TAU * k / 6
        M.ell("root", (0.3 * math.cos(a), 0.3 * math.sin(a), 0.8), (0.07, 0.07, 0.1), LUM5G, seg=(5, 3), group=1)
    return M

def make_grotto_roots():  # a curtain of hanging roots and glow worms (scenery along the cave wall)
    M = base()
    import random
    r = random.Random(71)
    M.box("root", (0, 0.2, 3.6), (3.6, 0.4, 0.4), ROCK5, bevel=0.1, noise_amt=0.4)
    for k in range(9):
        x = -1.6 + 0.4 * k + r.uniform(-0.1, 0.1)
        z1 = r.uniform(0.6, 2.2)
        M.limb("root", (x, 0.1, 3.5), (x + r.uniform(-0.2, 0.2), 0.0, z1), 0.06, 0.02, ROOT5, seg=5, noise_amt=0.3)
        M.ell("root", (x, 0.0, z1 + r.uniform(0.2, 0.8)), (0.03, 0.03, 0.05), LUM5, seg=(4, 3), group=1)
    M.box("root", (0, 0.2, 1.8), (0.4, 0.3, 3.6), ROCK5B, bevel=0.1, noise_amt=0.4)                    # a pillar that holds it up
    return M

def make_weapon_mortar():  # the slag mortar: a squat iron tube on a stock with a glowing breech
    M = base()
    M.limb("root", (0, 0.15, 0.2), (0, -0.35, 0.28), 0.12, 0.1, IRON4, seg=10)
    M.limb("root", (0, -0.35, 0.28), (0, -0.4, 0.29), 0.13, 0.13, RUST4, seg=10)
    M.box("root", (0, 0.35, 0.12), (0.12, 0.35, 0.12), (0.25, 0.16, 0.1), bevel=0.02)
    M.ell("root", (0, 0.2, 0.22), (0.1, 0.1, 0.1), MOLTEN, seg=(6, 4), group=1)
    return M

def make_weapon_seeker():  # the seeker swarm: a drum of eight small rockets with green tips
    M = base()
    M.limb("root", (0, 0.25, 0.2), (0, -0.25, 0.2), 0.16, 0.16, (0.22, 0.24, 0.22), seg=12)
    for k in range(8):
        a = TAU * k / 8
        x, z = 0.1 * math.cos(a), 0.2 + 0.1 * math.sin(a)
        M.limb("root", (x, -0.25, z), (x, -0.33, z), 0.03, 0.025, (0.6, 0.6, 0.58), seg=5)
        M.ell("root", (x, -0.35, z), (0.025, 0.03, 0.025), LUM5G, seg=(4, 3), group=1)
    M.box("root", (0, 0.3, 0.08), (0.08, 0.14, 0.2), (0.08, 0.08, 0.09), rot=(0.25, 0, 0), bevel=0.02)
    return M

# ───────────────────────── level 6: the cathedral ───────────────────

NAVE = (0.2, 0.19, 0.2); NAVE2 = (0.13, 0.12, 0.13); OAK = (0.16, 0.1, 0.07); GILT = (0.55, 0.42, 0.18)
RED6 = (5.0, 0.6, 0.35); VIO6 = (2.2, 0.6, 4.5); WAX6 = (0.85, 0.8, 0.66)

def make_cathedral_door():  # an altar seal: a round stone seal carved like a rose window splits into four tilting petals
    M = Model()
    petals = ["petal%d" % k for k in range(4)]
    hatch_rig(M, petals)
    plinth(M, NAVE, NAVE2)
    ring(M, "root", 0.3, 0.44, 2.05, 2.05, 2.45, 2.4, NAVE2, seg=24, noise_amt=0.2)
    for k in range(12):                                                                              # carved tracery round the collar
        a = TAU * k / 12
        M.box("root", (2.25 * math.cos(a), 2.25 * math.sin(a), 0.46), (0.06, 0.32, 0.03), GILT, rot=(0, 0, a), bevel=0.0)
    for k, part in enumerate(petals):
        a0 = TAU * k / 4 + math.pi / 4
        pts = [(0, 0)] + [(1.45 * math.cos(a0 - math.pi / 4 + math.pi / 2 * j / 4), 1.45 * math.sin(a0 - math.pi / 4 + math.pi / 2 * j / 4)) for j in range(5)]
        slab(M, part, pts, 0.32, 0.42, NAVE, noise_amt=0.15)
        for j in range(3):                                                                           # the rose-window tracery
            r = 0.4 + 0.35 * j
            M.box(part, (r * math.cos(a0), r * math.sin(a0), 0.43), (0.05, 0.36 - 0.06 * j, 0.025), GILT, rot=(0, 0, a0), bevel=0.0)
        M.box(part, (0.7 * math.cos(a0), 0.7 * math.sin(a0), 0.43), (1.0, 0.04, 0.025), (0.3, 0.05, 0.05), rot=(0, 0, a0), bevel=0.0)
    well_glow(M, (5.0, 0.7, 0.4))
    frames = [{part: slide(1.2 * t * math.cos(TAU * k / 4 + math.pi / 4), 1.2 * t * math.sin(TAU * k / 4 + math.pi / 4), 0.3 * t) for k, part in enumerate(petals)} for t in [j / 5 for j in range(6)]]
    return M, frames

def make_cathedral_wall():  # a 4 m nave bay: a buttressed wall with a tall stained-glass window glowing red and violet
    M = base()
    M.box("root", (0, 0.35, 3.0), (4.0, 0.7, 6.0), NAVE2, bevel=0.03, noise_amt=0.2)
    for x in (-1.85, 1.85):
        M.box("root", (x, -0.15, 3.0), (0.4, 0.6, 6.0), NAVE, bevel=0.04, noise_amt=0.2)
    M.box("root", (0, -0.02, 3.2), (2.2, 0.06, 4.2), (0.02, 0.02, 0.02), bevel=0.0)                    # the window opening
    for i in range(4):                                                                               # glass panes in lead
        for j in range(8):
            x = -0.78 + 0.52 * i; z = 1.35 + 0.5 * j
            if j == 7 and i in (0, 3): continue
            col = RED6 if (i + j) % 3 else VIO6
            M.box("root", (x, -0.06, z), (0.46, 0.02, 0.44), col, bevel=0.0, group=1)
    M.limb("root", (0, -0.05, 5.15), (0, -0.05, 5.45), 1.0, 0.6, NAVE, seg=10)                         # the pointed head of the arch
    M.box("root", (0, -0.1, 0.25), (4.0, 0.7, 0.5), NAVE, bevel=0.03)
    return M

def make_cathedral_column():  # a clustered gothic column, 1.4 m, 7 m tall
    M = base()
    M.box("root", (0, 0, 0.3), (1.5, 1.5, 0.6), NAVE, bevel=0.06, noise_amt=0.2)
    for k in range(8):
        a = TAU * k / 8
        M.limb("root", (0.42 * math.cos(a), 0.42 * math.sin(a), 0.6), (0.42 * math.cos(a), 0.42 * math.sin(a), 6.4), 0.16, 0.15, NAVE2 if k % 2 else NAVE, seg=6)
    M.limb("root", (0, 0, 0.6), (0, 0, 6.4), 0.45, 0.42, NAVE, seg=10)
    M.box("root", (0, 0, 6.6), (1.6, 1.6, 0.4), NAVE, bevel=0.05)
    return M

def make_cathedral_pews():  # a barricade of heaped oak pews, 3.6 m long (the sim's wall: it falls to blasts)
    M = base()
    for k, (y, z, rx) in enumerate(((0.0, 0.0, 0.0), (0.12, 0.45, 0.35), (-0.15, 0.55, -0.6))):
        M.box("root", (0, y, z + 0.25), (3.4, 0.45, 0.08), OAK, rot=(rx, 0, 0.05 * k), bevel=0.01)          # the seat
        M.box("root", (0, y + 0.22, z + 0.55), (3.4, 0.06, 0.55), OAK, rot=(rx, 0, 0.05 * k), bevel=0.01)   # the back
        for x in (-1.6, 0, 1.6):
            M.box("root", (x, y, z + 0.12), (0.08, 0.5, 0.25 + (0 if k else 0.0)), OAK, rot=(rx, 0, 0.05 * k), bevel=0.01)
    return M

def make_cathedral_shrine():  # a shrine: an altar stone under a gilt frame, its flame glowing (the blessing)
    M = base()
    M.box("root", (0, 0, 0.5), (1.4, 0.9, 1.0), NAVE, bevel=0.05, noise_amt=0.15)
    M.box("root", (0, 0, 1.04), (1.5, 1.0, 0.08), GILT, bevel=0.02)
    for x in (-0.6, 0.6):
        M.limb("root", (x, 0.3, 1.08), (x, 0.3, 2.3), 0.05, 0.05, GILT, seg=6)
    M.box("root", (0, 0.3, 2.32), (1.4, 0.12, 0.1), GILT, bevel=0.02)                               # the frame's lintel
    M.spike("root", (0, 0.3, 2.35), (0, 0.3, 3.0), 0.12, GILT, seg=6)
    M.ell("root", (0, 0, 1.25), (0.14, 0.14, 0.22), (5.0, 3.4, 1.4), seg=(6, 5), group=1)
    return M

def make_cathedral_candelabra():  # a tall iron candelabra with five candles
    M = base()
    iron = (0.12, 0.11, 0.11)
    for k in range(3):
        a = TAU * k / 3
        M.limb("root", (0.35 * math.cos(a), 0.35 * math.sin(a), 0.0), (0, 0, 0.5), 0.03, 0.03, iron, seg=4)
    M.limb("root", (0, 0, 0.4), (0, 0, 1.9), 0.04, 0.03, iron, seg=6)
    for k in range(5):
        x = -0.5 + 0.25 * k
        M.limb("root", (0, 0, 1.75), (x, 0, 1.95 - abs(x) * 0.1), 0.02, 0.02, iron, seg=4)
        M.limb("root", (x, 0, 1.95 - abs(x) * 0.1), (x, 0, 2.15 - abs(x) * 0.1), 0.03, 0.03, WAX6, seg=5)
        M.ell("root", (x, 0, 2.2 - abs(x) * 0.1), (0.02, 0.02, 0.05), (6.0, 3.8, 1.2), seg=(4, 3), group=1)
    return M

def make_cathedral_reliquary():  # a small gilt reliquary box (the sim's crate)
    M = base()
    M.box("root", (0, 0, 0.3), (0.9, 0.6, 0.6), (0.2, 0.12, 0.07), bevel=0.03, noise_amt=0.2)
    M.limb("root", (-0.45, 0, 0.65), (0.45, 0, 0.65), 0.3, 0.3, (0.2, 0.12, 0.07), seg=8)
    for x in (-0.35, 0, 0.35):
        M.box("root", (x, 0, 0.5), (0.05, 0.64, 0.82), GILT, bevel=0.0)
    return M

def make_cathedral_censer():  # a standing censer of burning oil (the sim's barrel: it bursts into fire)
    M = base()
    iron = (0.14, 0.13, 0.13)
    M.limb("root", (0, 0, 0.0), (0, 0, 0.7), 0.25, 0.08, iron, seg=10)
    M.ell("root", (0, 0, 0.9), (0.32, 0.32, 0.25), GILT, seg=(10, 6), noise_amt=0.15)
    M.ell("root", (0, 0, 1.08), (0.18, 0.18, 0.05), (5.0, 1.5, 0.3), seg=(8, 3), group=1)
    return M

# ───────────────────────── level 7: the rift ────────────────────────

GLASS7 = (0.04, 0.04, 0.06); GLASS7B = (0.09, 0.08, 0.13); BASALT7 = (0.17, 0.16, 0.17); VOID7 = (2.2, 1.0, 5.0)
COLD7 = (2.0, 3.6, 5.5); SHARD7 = (0.55, 0.5, 0.8); ASH7 = (0.5, 0.5, 0.52)

def make_rift_door():  # a void rift: a ring of obsidian shards round a disc of black glass; the shards draw back and down
    M = Model()
    shards = ["shard%d" % k for k in range(8)]
    hatch_rig(M, shards)
    plinth(M, BASALT7, GLASS7B)
    for k, part in enumerate(shards):
        a = TAU * k / 8 + 0.2
        M.spike(part, (1.75 * math.cos(a), 1.75 * math.sin(a), 0.3), (1.3 * math.cos(a + 0.1), 1.3 * math.sin(a + 0.1), 1.6 + 0.4 * (k % 3)), 0.22, GLASS7 if k % 2 else GLASS7B, seg=4)
    disc = ["disc"]
    M.bone("disc", "root", (0, 0, 0), (0, 0, 1))
    M.ell("disc", (0, 0, 0.36), (1.32, 1.32, 0.04), GLASS7, seg=(18, 3))
    ring(M, "root", 0.38, 0.42, 1.0, 1.0, 1.08, 1.08, VOID7, seg=24, group=1, noise_amt=0.0)
    well_glow(M, (2.2, 0.9, 5.0))
    frames = []
    for t in [j / 5 for j in range(6)]:
        p = {"disc": slide(0, 0, 0.36 * t)}
        for k, part in enumerate(shards):
            a = TAU * k / 8 + 0.2
            p[part] = slide(0.6 * t * math.cos(a), 0.6 * t * math.sin(a), 1.4 * t)
        frames.append(p)
    return M, frames

def make_rift_rim():  # 4 m of the plateau's broken basalt edge, falling away into the void behind it
    M = base()
    import random
    r = random.Random(77)
    M.box("root", (0, 0.5, 0.35), (4.1, 1.2, 0.7), BASALT7, bevel=0.08, noise_amt=0.35)
    for k in range(4):
        x = -1.5 + k + r.uniform(-0.2, 0.2)
        h = r.uniform(1.0, 2.6)
        M.box("root", (x, 0.4 + r.uniform(-0.2, 0.2), h / 2), (0.7, 0.7, h), BASALT7 if k % 2 else GLASS7B, rot=(0, r.uniform(-0.15, 0.15), r.uniform(0, 0.8)), bevel=0.05, noise_amt=0.3)
    M.box("root", (0, 1.4, 0.02), (4.0, 0.6, 0.04), VOID7, bevel=0.0, group=1)                         # the void's glow at the edge
    return M

def make_rift_pillar():  # a broken hexagonal basalt pillar, 1.4 m, up to 4 m tall
    M = base()
    M.limb("root", (0, 0, 0.0), (0, 0, 3.6), 0.7, 0.62, BASALT7, seg=6, noise_amt=0.3)
    M.limb("root", (0.3, 0.2, 3.5), (0.35, 0.25, 4.1), 0.25, 0.05, BASALT7, seg=6, noise_amt=0.3)
    M.ell("root", (0, -0.6, 1.6), (0.08, 0.04, 0.6), VOID7, seg=(5, 4), group=1)                         # a glowing seam
    return M

def make_rift_shards():  # a cluster of black crystal shards (the sim's crate: it shatters)
    M = base()
    import random
    r = random.Random(81)
    for k in range(6):
        a = r.uniform(0, TAU); d = r.uniform(0.0, 0.35)
        x, y = d * math.cos(a), d * math.sin(a)
        M.spike("root", (x, y, 0.12), (x + r.uniform(-0.3, 0.3), y + r.uniform(-0.3, 0.3), r.uniform(0.8, 1.6)), r.uniform(0.15, 0.25), GLASS7 if k % 2 else SHARD7, seg=4)
    M.ell("root", (0, 0, 0.08), (0.45, 0.45, 0.08), BASALT7, seg=(8, 3))
    return M

def make_rift_brazier():  # a brazier of bone holding cold blue fire
    M = base()
    bone_c = (0.62, 0.58, 0.5)
    for k in range(4):
        a = TAU * k / 4
        M.limb("root", (0.4 * math.cos(a), 0.4 * math.sin(a), 0.0), (0.25 * math.cos(a), 0.25 * math.sin(a), 0.9), 0.05, 0.04, bone_c, seg=5)
    ring(M, "root", 0.9, 1.15, 0.3, 0.4, 0.34, 0.45, bone_c, seg=12)
    for k in range(5):
        a = TAU * k / 5
        M.spike("root", (0.12 * math.cos(a), 0.12 * math.sin(a), 0.95), (0.05 * math.cos(a), 0.05 * math.sin(a), 1.55 + 0.15 * (k % 2)), 0.1, COLD7, seg=5, group=1)
    return M

def make_rift_urn():  # a cracked obsidian urn of void-light (the sim's barrel: it bursts)
    M = base()
    M.ell("root", (0, 0, 0.5), (0.35, 0.35, 0.5), GLASS7B, seg=(10, 6), noise_amt=0.2)
    M.ell("root", (0.2, -0.25, 0.55), (0.04, 0.03, 0.3), VOID7, seg=(4, 3), group=1)
    M.ell("root", (0, 0, 0.98), (0.15, 0.15, 0.04), VOID7, seg=(6, 3), group=1)
    return M

# ───────────────────────── level 8: the core ────────────────────────

ROCK8 = (0.09, 0.06, 0.05); ROCK8B = (0.16, 0.08, 0.05); IRON8 = (0.13, 0.12, 0.12); BONE8 = (0.55, 0.47, 0.38)
LAVA8 = (6.0, 1.6, 0.2); FIRE8 = (6.0, 2.6, 0.5)

def make_core_door():  # the heart seal: a black stone heart bound in iron bands on a dais inside the lava ring; the bands burst
    M = Model()
    bands = ["band%d" % k for k in range(4)]
    hatch_rig(M, bands + ["heart"])
    plinth(M, ROCK8, IRON8)
    M.ell("heart", (0, 0, 0.55), (1.2, 1.0, 0.35), ROCK8B, seg=(12, 6), noise_amt=0.3)                  # the clenched heart
    for k in range(6):
        a = TAU * k / 6
        M.ell("heart", (0.75 * math.cos(a), 0.6 * math.sin(a), 0.7), (0.06, 0.04, 0.05), LAVA8, seg=(4, 3), emissive=True)
    for k, part in enumerate(bands):
        a = TAU * k / 4
        M.box(part, (0.0, 0.0, 0.88), (0.18, 2.5, 0.08), IRON8, rot=(0, 0, a), bevel=0.02)
    well_glow(M, (6.0, 5.0, 3.6))                                                                     # daylight from below the core
    frames = []
    for t in [j / 5 for j in range(6)]:
        p = {"heart": slide(0, 0, 0.75 * t)}
        for k, part in enumerate(bands):
            a = TAU * k / 4 + math.pi / 2
            p[part] = slide(1.6 * t * math.cos(a), 1.6 * t * math.sin(a), 0.75 * t, ry=0.8 * t)
        frames.append(p)
    return M, frames

def make_core_wall():  # 4 m of the burning cavern's wall: black rock with veins of lava, 5 m tall
    M = base()
    import random
    r = random.Random(91)
    M.box("root", (0, 0.6, 2.5), (4.1, 1.2, 5.0), ROCK8, bevel=0.1, noise_amt=0.4)
    for k in range(5):
        M.ell("root", (r.uniform(-1.6, 1.6), 0.0, r.uniform(0.6, 4.2)), (r.uniform(0.5, 0.9), 0.4, r.uniform(0.5, 1.0)), ROCK8B if k % 2 else ROCK8, seg=(7, 5), noise_amt=0.4)
    for k in range(3):
        x0 = r.uniform(-1.6, 1.6)
        M.limb("root", (x0, -0.2, 0.0), (x0 + r.uniform(-0.6, 0.6), -0.2, r.uniform(2.5, 4.5)), 0.05, 0.02, LAVA8, seg=4, group=1)
    return M

def make_core_cairn():  # a caged cairn of skulls (heavy cover)
    M = base()
    M.ell("root", (0, 0, 0.6), (0.85, 0.85, 0.6), ROCK8B, seg=(10, 6), noise_amt=0.35)
    M.box("root", (0, 0, 0.1), (1.6, 1.6, 0.2), ROCK8, bevel=0.05)
    for k in range(6):
        a = TAU * k / 6
        M.limb("root", (0.8 * math.cos(a), 0.8 * math.sin(a), 0.0), (0.55 * math.cos(a), 0.55 * math.sin(a), 1.9), 0.04, 0.04, IRON8, seg=4)
        M.ell("root", (0.82 * math.cos(a), 0.82 * math.sin(a), 0.75), (0.13, 0.13, 0.12), BONE8, seg=(6, 4))
    M.ell("root", (0, 0, 1.25), (0.25, 0.25, 0.2), FIRE8, seg=(6, 4), group=1)
    return M

def make_core_vent():  # a brimstone vent: a cracked cone of yellowed rock with fire at its throat
    M = base()
    M.limb("root", (0, 0, 0.0), (0, 0, 0.9), 1.2, 0.45, (0.3, 0.26, 0.1), seg=10, noise_amt=0.4)
    M.ell("root", (0, 0, 0.88), (0.4, 0.4, 0.06), LAVA8, seg=(8, 3), group=1)
    return M

def make_core_throne():  # the throne of fused bone and iron filling the north wall, 12 m wide, 9 m tall
    M = base()
    M.box("root", (0, 1.5, 0.4), (12.0, 4.0, 0.8), ROCK8, bevel=0.1, noise_amt=0.3)
    M.box("root", (0, 1.8, 1.4), (7.0, 3.0, 1.2), IRON8, bevel=0.08, noise_amt=0.3)
    M.box("root", (0, 2.6, 5.0), (6.0, 1.0, 8.0), ROCK8B, bevel=0.1, noise_amt=0.35)                       # the back
    for s in (1, -1):
        M.box("root", (s * 3.2, 1.6, 2.6), (0.8, 2.6, 1.2), IRON8, bevel=0.06)                               # arms
        for k in range(4):
            M.spike("root", (s * (1.5 + 0.9 * k), 2.6, 8.6 - 0.5 * k), (s * (2.0 + 1.3 * k), 2.8, 11.0 - 0.8 * k), 0.3, BONE8, seg=5)
        for k in range(5):
            M.ell("root", (s * (4.2 + 0.35 * k), 1.0, 1.2 + 0.5 * k), (0.28, 0.28, 0.26), BONE8, seg=(7, 5))  # skulls heaped at its foot
    M.box("root", (0, 2.05, 4.0), (5.0, 0.1, 5.0), LAVA8, bevel=0.0, group=1)                                  # fire behind the seat's cracks
    return M

def make_core_cage():  # a chained cage (the sim's crate)
    M = base()
    M.box("root", (0, 0, 0.06), (1.2, 1.2, 0.12), IRON8, bevel=0.02)
    M.box("root", (0, 0, 1.34), (1.2, 1.2, 0.12), IRON8, bevel=0.02)
    for k in range(8):
        a = TAU * k / 8
        M.limb("root", (0.55 * math.cos(a), 0.55 * math.sin(a), 0.1), (0.55 * math.cos(a), 0.55 * math.sin(a), 1.3), 0.025, 0.025, IRON8, seg=4)
    M.ell("root", (0, 0, 0.25), (0.25, 0.25, 0.22), BONE8, seg=(7, 5))
    return M

def make_core_pot():  # a pot of brimstone oil (the sim's barrel)
    M = base()
    M.ell("root", (0, 0, 0.45), (0.38, 0.38, 0.45), (0.22, 0.12, 0.06), seg=(10, 6), noise_amt=0.3)
    M.ell("root", (0, 0, 0.9), (0.2, 0.2, 0.04), LAVA8, seg=(7, 3), group=1)
    return M

def make_weapon_wheel():  # the saint's wheel: a bladed disc with a gilt hub
    M = base()
    steel = (0.6, 0.6, 0.62)
    M.limb("root", (0, 0, 0.18), (0, 0, 0.22), 0.32, 0.32, steel, seg=16)
    for k in range(8):
        a = TAU * k / 8
        M.spike("root", (0.3 * math.cos(a), 0.3 * math.sin(a), 0.2), (0.46 * math.cos(a + 0.3), 0.46 * math.sin(a + 0.3), 0.2), 0.05, steel, seg=4)
    M.limb("root", (0, 0, 0.16), (0, 0, 0.24), 0.1, 0.1, GILT, seg=10)
    M.ell("root", (0, 0, 0.25), (0.05, 0.05, 0.02), (5.0, 4.0, 1.6), seg=(6, 3), group=1)
    return M

def make_weapon_frost():  # the frost lance: a long cryo projector with a glowing coil
    M = base()
    body = (0.35, 0.38, 0.42)
    M.box("root", (0, 0.1, 0.2), (0.12, 0.6, 0.12), body, bevel=0.02)
    M.limb("root", (0, -0.2, 0.2), (0, -0.75, 0.2), 0.035, 0.035, body, seg=8)
    M.limb("root", (0, 0.05, 0.31), (0, 0.4, 0.31), 0.06, 0.06, (0.8, 0.85, 0.9), seg=8)                  # the tank
    M.box("root", (0, 0.35, 0.08), (0.08, 0.14, 0.2), (0.08, 0.08, 0.09), rot=(0.25, 0, 0), bevel=0.02)
    for k in range(4):
        M.limb("root", (0, -0.25 - 0.1 * k, 0.2), (0, -0.28 - 0.1 * k, 0.2), 0.07, 0.07, COLD7, seg=8, group=1)
    return M

def make_weapon_hellfire():  # the hellfire lance: a ribbed barrel over a burning core
    M = base()
    M.box("root", (0, 0.1, 0.2), (0.16, 0.55, 0.16), IRON8, bevel=0.02)
    M.limb("root", (0, -0.15, 0.2), (0, -0.7, 0.2), 0.08, 0.05, IRON8, seg=8)
    for k in range(4):
        M.limb("root", (0, -0.25 - 0.12 * k, 0.2), (0, -0.29 - 0.12 * k, 0.2), 0.09, 0.09, (0.25, 0.1, 0.06), seg=8)
    M.ell("root", (0, 0.1, 0.3), (0.06, 0.2, 0.05), FIRE8, seg=(5, 4), group=1)
    M.ell("root", (0, -0.72, 0.2), (0.04, 0.03, 0.04), FIRE8, seg=(5, 3), group=1)
    M.box("root", (0, 0.38, 0.08), (0.08, 0.14, 0.2), (0.08, 0.08, 0.09), rot=(0.25, 0, 0), bevel=0.02)
    return M

# ───────────────────────── guns and loot ────────────────────────────

def make_weapon_arc():  # the arc caster: a stubby gun body with three copper coils and a fork at the muzzle
    M = base()
    body = (0.24, 0.25, 0.28); copper = (0.72, 0.38, 0.18); dark = (0.07, 0.07, 0.08); glow = (1.2, 3.6, 5.0)
    M.box("root", (0, 0.05, 0.2), (0.16, 0.5, 0.16), body, bevel=0.02)
    M.box("root", (0, 0.32, 0.08), (0.1, 0.14, 0.22), dark, rot=(0.3, 0, 0), bevel=0.02)            # grip
    for k in range(3):
        ring(M, "root", 0.12, 0.28, 0.08, 0.08, 0.12, 0.12, copper, seg=10)
    for k in range(3):
        y = -0.12 - 0.1 * k
        M.limb("root", (0, y, 0.11), (0, y, 0.29), 0.11, 0.11, copper, seg=10)
        M.limb("root", (0, y - 0.035, 0.12), (0, y - 0.035, 0.28), 0.085, 0.085, dark, seg=8)
    for s in (1, -1):
        M.spike("root", (s * 0.04, -0.4, 0.2), (s * 0.08, -0.62, 0.2), 0.025, copper, seg=5)
    M.ell("root", (0, -0.45, 0.2), (0.04, 0.04, 0.04), glow, seg=(6, 4), group=1)
    M.box("root", (0, 0.05, 0.29), (0.04, 0.3, 0.02), glow, bevel=0.0, group=1)
    return M

def make_weapon_stake():  # the stake repeater: a crossbow stock with a drum of iron stakes
    M = base()
    wood = (0.3, 0.2, 0.12); iron = (0.22, 0.21, 0.2); dark = (0.07, 0.07, 0.08); glow = (5.0, 4.4, 3.4)
    M.box("root", (0, 0.1, 0.18), (0.1, 0.7, 0.1), wood, bevel=0.015)
    M.box("root", (0, 0.4, 0.1), (0.08, 0.14, 0.2), wood, rot=(0.25, 0, 0), bevel=0.015)
    M.limb("root", (-0.38, -0.25, 0.2), (0.38, -0.25, 0.2), 0.022, 0.022, iron, seg=6)               # the bow arms
    for s in (1, -1):
        M.limb("root", (s * 0.38, -0.25, 0.2), (s * 0.45, -0.1, 0.2), 0.018, 0.012, iron, seg=5)
    M.limb("root", (0, 0.0, 0.3), (0, 0.15, 0.3), 0.09, 0.09, dark, seg=10)                         # the drum, axis forward
    for k in range(6):
        a = TAU * k / 6
        M.limb("root", (0.06 * math.cos(a), -0.05, 0.3 + 0.06 * math.sin(a)), (0.06 * math.cos(a), -0.4, 0.3 + 0.06 * math.sin(a)), 0.012, 0.004, iron, seg=4)
    M.box("root", (0, -0.2, 0.235), (0.02, 0.3, 0.01), glow, bevel=0.0, group=1)
    return M

def make_loot_cache():  # an iron-bound coffer; the lid opens; group 1 the trim the game tints by rarity
    M = Model()
    M.bone("root", None, (0, 0, 0), (0, 0, 1))
    M.bone("lid", "root", (0, 0.22, 0.42), (0, 0.22, 0.62))
    wood = (0.28, 0.17, 0.1); iron = (0.18, 0.17, 0.17); trim = (3.0, 3.0, 3.0)
    M.box("root", (0, 0, 0.22), (0.8, 0.5, 0.42), wood, bevel=0.025, noise_amt=0.25)
    for x in (-0.3, 0.3):
        M.box("root", (x, 0, 0.22), (0.06, 0.53, 0.45), iron, bevel=0.01)
    M.box("root", (0, -0.26, 0.3), (0.12, 0.03, 0.14), iron, bevel=0.01)
    M.limb("lid", (-0.4, 0.0, 0.42), (0.4, 0.0, 0.42), 0.25, 0.25, wood, seg=12, noise_amt=0.2)
    for x in (-0.3, 0.3):
        M.limb("lid", (x - 0.03, 0.0, 0.42), (x + 0.03, 0.0, 0.42), 0.265, 0.265, iron, seg=12)
    M.box("root", (0, -0.252, 0.4), (0.82, 0.01, 0.03), trim, bevel=0.0, group=1)                    # the glowing seam
    M.box("root", (0, 0.252, 0.4), (0.82, 0.01, 0.03), trim, bevel=0.0, group=1)
    M.ell("root", (0, -0.28, 0.3), (0.035, 0.012, 0.035), trim, seg=(6, 3), group=1)                  # the lock's eye
    frames = [{"lid": (-1.6 * t, 0, 0)} for t in (0, 0.2, 0.6, 1.0)]
    return M, frames

PROPS = {
    "yard_door": make_yard_door, "sluice_door": make_sluice_door, "ossuary_door": make_ossuary_door,
    "sluice_wall": make_sluice_wall, "sluice_pier": make_sluice_pier, "sluice_curb": make_sluice_curb,
    "sluice_gate": make_sluice_gate, "sluice_lamp": make_sluice_lamp, "sluice_crate": make_sluice_crate,
    "sluice_drum": make_sluice_drum, "sluice_pipe": make_sluice_pipe,
    "ossuary_wall": make_ossuary_wall, "ossuary_column": make_ossuary_column, "ossuary_bonepile": make_ossuary_bonepile,
    "ossuary_urn": make_ossuary_urn, "ossuary_brazier": make_ossuary_brazier, "ossuary_candles": make_ossuary_candles,
    "ossuary_sarcophagus": make_ossuary_sarcophagus, "ossuary_outer": make_ossuary_outer,
    "foundry_door": make_foundry_door, "foundry_wall": make_foundry_wall, "foundry_crucible": make_foundry_crucible,
    "foundry_cart": make_foundry_cart, "foundry_ingots": make_foundry_ingots, "foundry_vent": make_foundry_vent, "foundry_lamp": make_foundry_lamp,
    "grotto_door": make_grotto_door, "grotto_rock": make_grotto_rock, "grotto_trunk": make_grotto_trunk, "grotto_cluster": make_grotto_cluster,
    "grotto_pod": make_grotto_pod, "grotto_roots": make_grotto_roots, "weapon_mortar": make_weapon_mortar, "weapon_seeker": make_weapon_seeker,
    "cathedral_door": make_cathedral_door, "cathedral_wall": make_cathedral_wall, "cathedral_column": make_cathedral_column,
    "cathedral_pews": make_cathedral_pews, "cathedral_shrine": make_cathedral_shrine, "cathedral_candelabra": make_cathedral_candelabra,
    "cathedral_reliquary": make_cathedral_reliquary, "cathedral_censer": make_cathedral_censer,
    "rift_door": make_rift_door, "rift_rim": make_rift_rim, "rift_pillar": make_rift_pillar, "rift_shards": make_rift_shards,
    "rift_brazier": make_rift_brazier, "rift_urn": make_rift_urn,
    "core_door": make_core_door, "core_wall": make_core_wall, "core_cairn": make_core_cairn, "core_vent": make_core_vent,
    "core_throne": make_core_throne, "core_cage": make_core_cage, "core_pot": make_core_pot,
    "weapon_wheel": make_weapon_wheel, "weapon_frost": make_weapon_frost, "weapon_hellfire": make_weapon_hellfire,
    "weapon_arc": make_weapon_arc, "weapon_stake": make_weapon_stake, "loot_cache": make_loot_cache,
}

def sheet(name, ob, ao, frames):
    """The game's camera angle and a low three-quarter view, first and last frame, Cycles on the CPU."""
    if not SHEET: return
    os.makedirs(SHEET, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'; sc.cycles.samples = 16
    sc.render.resolution_x = 360; sc.render.resolution_y = 360
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam); sc.camera = cam
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN')); sun.data.energy = 3.5
    sun.rotation_euler = (0.8, 0.25, 0.7); sc.collection.objects.link(sun)
    w = bpy.data.worlds.new("w"); w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.22, 0.24, 0.28, 1); sc.world = w
    me = ob.data
    ca = me.color_attributes.new("col", 'FLOAT_COLOR', 'POINT')
    ea = me.color_attributes.new("emi", 'FLOAT_COLOR', 'POINT')
    for vi, c in enumerate(ob["_cols"]):
        ca.data[vi].color = (min(c[0], 1), min(c[1], 1), min(c[2], 1), 1)
        ea.data[vi].color = (c[0] / 4, c[1] / 4, c[2] / 4, 1) if max(c) > 1.5 else (0, 0, 0, 1)
    mat = bpy.data.materials.new("m"); mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Roughness"].default_value = 0.75
    a1 = nt.nodes.new("ShaderNodeAttribute"); a1.attribute_name = "col"
    nt.links.new(a1.outputs["Color"], bsdf.inputs["Base Color"])
    a2 = nt.nodes.new("ShaderNodeAttribute"); a2.attribute_name = "emi"
    nt.links.new(a2.outputs["Color"], bsdf.inputs["Emission Color"])
    bsdf.inputs["Emission Strength"].default_value = 3.0
    while len(me.materials) < 2: me.materials.append(mat)
    ground = bpy.data.meshes.new("g"); ground.from_pydata([(-30, -30, 0), (30, -30, 0), (30, 30, 0), (-30, 30, 0)], [], [(0, 1, 2, 3)])
    go = bpy.data.objects.new("g", ground); sc.collection.objects.link(go)
    gm = bpy.data.materials.new("gm"); gm.use_nodes = True
    next(n for n in gm.node_tree.nodes if n.type == "BSDF_PRINCIPLED").inputs["Base Color"].default_value = (0.1, 0.1, 0.11, 1)
    ground.materials.append(gm)
    pose_frame(ao, frames[0] if frames else {})
    bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    ctr = sum(bb, Vector()) / 8
    R = max((v - ctr).length for v in bb)
    D = R * 3.2
    picks = [0, len(frames) - 1] if frames and len(frames) > 1 else [0]
    for fi in picks:
        if frames: pose_frame(ao, frames[fi])
        for tag, (el, az) in (("game", (math.radians(62), math.radians(200))), ("low", (math.radians(22), math.radians(215)))):
            cam.location = ctr + Vector((D * math.cos(el) * math.sin(az), D * math.cos(el) * math.cos(az), D * math.sin(el)))
            d = ctr - cam.location
            cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
            cam.data.lens = 50
            sc.render.filepath = os.path.join(SHEET, "%s_%s_%d.png" % (name, tag, fi))
            bpy.ops.render.render(write_still=True)

for name in (kinds or list(PROPS)):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    r = PROPS[name]()
    M, frames = r if isinstance(r, tuple) else (r, [])
    ob, ao = build_object(M, name)
    pose_frame(ao, frames[0] if frames else {})
    ao_col = bake_ao(ob, M)
    clips = [bake_clip(ob, M, ao, frames if frames else [{}])]
    size = write_cwchar(os.path.join(OUT, name + ".cwchar"), M, ao_col, clips, ob)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    print("LEVELPROP %s: %d verts, %d tris, %d frames, %d bytes" % (name, len(M.v), tris, len(clips[0]), size))
    ob["_cols"] = [tuple(c[k] * ao_col[vi] for k in range(3)) for vi, c in enumerate(M.col)]
    sheet(name, ob, ao, frames)
