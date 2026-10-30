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
