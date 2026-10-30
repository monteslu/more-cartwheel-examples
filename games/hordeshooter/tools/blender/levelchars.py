# The descent's monsters and bosses (LEVELS.md), headless:
#   flatpak run org.blender.Blender --background --factory-startup --python \
#     <abs>/tools/blender/levelchars.py -- <abs out dir> [kind ...] [--sheet <abs png dir>]
# kinds: see CHARS at the end (default all).
#
# The same pipeline as hordechars.py (cwchar_lib.py): primitives skinned to an armature with hand-set
# weights, posed per frame by procedural cycles, evaluated through Blender's armature modifier and
# written as .cwchar. Clips, in the order the game indexes them (LEVELS.md, section 5):
#   basic monster: 0 walk (6 frames), 1 attack (3: wind-up, strike, recovery)
#   boss:          0 walk (10), 1 close attack (3), 2 special (3: tell, release, recovery), 3 summon (2)
# Glows are vertex colours above 1.5 (the view reads them as emissive), as on the yard's crowd.
# Every shape is generated here (no outside art).
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector
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
WALK, BOSS_WALK = 6, 10

def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)

# ───────────────────────── rigs ──────────────────────────────────────
# Characters face -Y with Z up (the writer turns that into the file's +Z forward, Y up). A bone that
# points straight down has local X = world X (rotating it +x swings its tip back), local Y along the
# bone (a twist about the vertical) and local Z = world +Y (rotating it +z swings its tip toward +X).

def humanoid_rig(M, hip_z, thigh, shin, torso, neck, arm_up, arm_lo, sx, hx):
    M.bone("hips", None, (0, 0, hip_z - 0.05), (0, 0, hip_z + 0.1))
    M.bone("spine", "hips", (0, 0, hip_z + 0.1), (0, 0, hip_z + torso * 0.5))
    M.bone("chest", "spine", (0, 0, hip_z + torso * 0.5), (0, 0, hip_z + torso))
    M.bone("head", "chest", (0, 0, hip_z + torso), (0, 0, hip_z + torso + neck))
    for s, side in ((1, "L"), (-1, "R")):
        sz = hip_z + torso - 0.08
        M.bone("uarm" + side, "chest", (s * sx, 0, sz), (s * sx, 0, sz - arm_up))
        M.bone("farm" + side, "uarm" + side, (s * sx, 0, sz - arm_up), (s * sx, 0, sz - arm_up - arm_lo))
        M.bone("thigh" + side, "hips", (s * hx, 0, hip_z), (s * hx, 0, hip_z - thigh))
        M.bone("shin" + side, "thigh" + side, (s * hx, 0, hip_z - thigh), (s * hx, 0, hip_z - thigh - shin))
        M.bone("foot" + side, "shin" + side, (s * hx, 0, hip_z - thigh - shin), (s * hx, -0.18, hip_z - thigh - shin))

def walk_pose(f, n, A_leg=0.7, K=0.9, arm=0.5, bounce=0.05, spine_lean=0.0, twist=0.15, arm_bend=0.4, arm_fwd=0.0, lean_extra=0.0):
    ph = TAU * f / n
    p = {}
    sL = math.sin(ph); sR = math.sin(ph + math.pi)
    def leg(side, s):
        fold = K * max(0.0, math.sin(ph + (0.9 if side == "L" else 0.9 + math.pi)))
        return {"thigh" + side: (-A_leg * s, 0, 0), "shin" + side: (fold + 0.1, 0, 0), "foot" + side: (-0.3 * fold + 0.25 * s, 0, 0)}
    p.update(leg("L", sL)); p.update(leg("R", sR))
    bob = bounce * abs(math.sin(ph))
    p["hips"] = (spine_lean * 0.4, 0, twist * 0.4 * math.sin(ph), 0, bob, 0)
    p["spine"] = (spine_lean * 0.4, 0, -twist * math.sin(ph) * 0.8)
    p["chest"] = (spine_lean * 0.2 + lean_extra, 0, -twist * 0.5 * math.sin(ph))
    p["head"] = (-spine_lean * 0.6 - lean_extra - 0.05, 0, twist * 1.1 * math.sin(ph))
    p["uarmL"] = (arm * sR - arm_fwd, 0.0, 0); p["farmL"] = (arm_bend + 0.2 * max(0, sR) * arm - arm_fwd * 0.3, 0, 0)
    p["uarmR"] = (arm * sL - arm_fwd, 0.0, 0); p["farmR"] = (arm_bend + 0.2 * max(0, sL) * arm - arm_fwd * 0.3, 0, 0)
    return p

def stance(crouch=0.0, spread=0.0):
    p = {}
    for side, s in (("L", 1), ("R", -1)):
        p["thigh" + side] = (-crouch, 0, s * spread)
        p["shin" + side] = (crouch * 1.8, 0, 0)
        p["foot" + side] = (-crouch * 0.8, 0, 0)
    p["hips"] = (0, 0, 0, 0, 0, 0)
    return p

def torso_weights(hz, a, b, c, d):
    return lambda p: {"hips": 1 - smoothstep(hz + a, hz + b, p.z),
                      "spine": smoothstep(hz + a, hz + b, p.z) * (1 - smoothstep(hz + c, hz + d, p.z)),
                      "chest": smoothstep(hz + c, hz + d, p.z)}

def bug_rig(M, body_z, legs, prefix="leg"):
    """A body bone and, per leg, a vertical bone at its hip (legs: list of (name, x, y))."""
    M.bone("body", None, (0, 0, body_z - 0.1), (0, 0, body_z + 0.1))
    M.bone("head", "body", (0, -0.2, body_z), (0, -0.2, body_z + 0.15))
    for name, x, y in legs:
        M.bone(name, "body", (x, y, body_z), (x, y, body_z - 0.3))

def bug_walk(f, n, legs, swing=0.35, lift=0.35, bob=0.03, sway=0.06):
    """A tripod gait: alternate legs in antiphase, each swinging fore and aft about the vertical and lifted
    on the forward swing."""
    ph = TAU * f / n
    p = {"body": (0, 0, sway * math.sin(ph), 0, bob * abs(math.sin(ph * 2)), 0), "head": (0.05 * math.sin(ph * 2), 0, -sway * math.sin(ph))}
    for k, (name, x, y) in enumerate(legs):
        s = 1 if x > 0 else -1
        a = ph + (0 if k % 2 == 0 else math.pi)
        up = max(0.0, math.cos(a))
        p[name] = (0, swing * math.sin(a), s * lift * up * 0.8)
    return p

# ───────────────────────── level 2: the skitter ───────────────────────

SK_LEGS = [("legL0", 0.26, -0.18), ("legL1", 0.3, 0.06), ("legL2", 0.24, 0.3),
           ("legR0", -0.26, -0.18), ("legR1", -0.3, 0.06), ("legR2", -0.24, 0.3)]

def make_skitter():
    M = Model()
    bz = 0.27
    bug_rig(M, bz, SK_LEGS)
    M.bone("hookL", "body", (0.16, -0.36, bz + 0.04), (0.16, -0.36, bz - 0.2))
    M.bone("hookR", "body", (-0.16, -0.36, bz + 0.04), (-0.16, -0.36, bz - 0.2))
    shell = (0.13, 0.11, 0.07); plate = (0.24, 0.2, 0.11); belly = (0.62, 0.56, 0.4); leg = (0.17, 0.13, 0.09)
    tip = (0.75, 0.68, 0.5); eye = (1.6, 4.0, 0.8); band = (0.42, 0.36, 0.14)
    M.ell("body", (0, 0.05, bz + 0.04), (0.36, 0.46, 0.13), shell, seg=(10, 6), noise_amt=0.18)        # the flat shell
    M.ell("body", (0, 0.05, bz - 0.04), (0.3, 0.4, 0.09), belly, seg=(8, 5))                            # the pale belly
    for k in range(4):                                                                                 # overlapping back plates
        y = -0.22 + 0.15 * k
        M.ell("body", (0, y, bz + 0.12 - 0.012 * k), (0.31 - abs(k - 1.5) * 0.05, 0.11, 0.05), plate if k % 2 else shell, seg=(7, 3), noise_amt=0.25)
        M.box("body", (0, y + 0.08, bz + 0.15 - 0.012 * k), (0.2 - abs(k - 1.5) * 0.04, 0.025, 0.02), band, bevel=0.008)
    M.ell("head", (0, -0.4, bz + 0.03), (0.17, 0.14, 0.09), shell, seg=(8, 5))
    for s in (1, -1):
        M.ell("head", (s * 0.07, -0.5, bz + 0.08), (0.03, 0.03, 0.025), eye, seg=(5, 4), emissive=True)
        M.ell("head", (s * 0.12, -0.46, bz + 0.07), (0.022, 0.022, 0.02), eye, seg=(5, 4), emissive=True)
        M.spike("head", (s * 0.06, -0.5, bz - 0.02), (s * 0.02, -0.62, bz - 0.08), 0.025, tip, seg=4)    # mandibles
    for name, x, y in SK_LEGS:
        s = 1 if x > 0 else -1
        knee = (x + s * 0.3, y + 0.06 * (1 if y > 0 else -1), bz + 0.24)
        foot = (x + s * 0.56, y + 0.14 * (1 if y > 0 else -1), 0.0)
        M.limb(name, (x, y, bz), knee, 0.055, 0.045, leg, seg=5)
        M.limb(name, knee, foot, 0.045, 0.012, leg, seg=5)
    for s, side in ((1, "L"), (-1, "R")):                                                              # the two hooked forelegs
        a = (s * 0.16, -0.36, bz + 0.04); b = (s * 0.3, -0.6, bz + 0.26); c = (s * 0.22, -0.86, bz + 0.04)
        M.limb("hook" + side, a, b, 0.06, 0.05, leg, seg=5)
        M.limb("hook" + side, b, c, 0.05, 0.012, tip, seg=5)
    return M

def skitter_poses():
    walk = []
    for f in range(WALK):
        p = bug_walk(f, WALK, SK_LEGS, swing=0.45, lift=0.4, bob=0.025, sway=0.08)
        ph = TAU * f / WALK
        p["hookL"] = (-0.2 + 0.15 * math.sin(ph), 0, 0); p["hookR"] = (-0.2 - 0.15 * math.sin(ph), 0, 0)
        walk.append(p)
    attack = []
    # the leap: crouched low with the forelegs raised high (the tell), sprung forward with the hooks thrown out
    # and the back legs stretched, then gathered
    for crouch, pitch, hook, legs_back in ((-0.09, -0.25, -1.3, 0.0), (0.05, 0.3, -0.5, 0.5), (-0.02, 0.05, -0.4, 0.2)):
        p = {"body": (pitch, 0, 0, 0, crouch, 0), "head": (-pitch * 0.6, 0, 0)}
        for name, x, y in SK_LEGS:
            s = 1 if x > 0 else -1
            back = legs_back if y > 0 else -legs_back * 0.5
            p[name] = (-pitch, s * 0.0 + back * 0.0, s * (0.25 if crouch < 0 else -0.1))
        p["hookL"] = (hook, 0, 0.2); p["hookR"] = (hook, 0, -0.2)
        attack.append(p)
    return [walk, attack]

# ───────────────────────── level 3: the bonewalker ────────────────────

def make_bonewalker():
    M = Model()
    hz = 0.98
    humanoid_rig(M, hz, 0.48, 0.48, 0.62, 0.2, 0.34, 0.34, 0.22, 0.11)
    bone_c = (0.78, 0.72, 0.58); old = (0.55, 0.49, 0.38); cloth = (0.16, 0.13, 0.11); rust = (0.36, 0.2, 0.12)
    steel = (0.42, 0.4, 0.38); eye = (0.9, 2.2, 4.0)
    tw = torso_weights(hz, 0.05, 0.25, 0.3, 0.5)
    M.limb("spine", (0, 0.02, hz + 0.02), (0, 0.04, hz + 0.4), 0.035, 0.035, old, seg=5, w=tw)          # the spine
    M.ell("hips", (0, 0.0, hz + 0.0), (0.16, 0.1, 0.08), old, seg=(7, 4))                               # pelvis
    for k in range(5):                                                                                  # the ribs
        z = hz + 0.3 + k * 0.055
        r = 0.15 - abs(k - 2) * 0.012
        M.ell("chest", (0, -0.02, z), (r, 0.1, 0.018), bone_c, seg=(8, 3))
    M.box("chest", (0, -0.1, hz + 0.42), (0.04, 0.03, 0.22), bone_c, bevel=0.01)                       # sternum
    M.ell("chest", (0, 0, hz + 0.6), (0.24, 0.1, 0.04), bone_c, seg=(8, 3))                             # collarbones
    M.ell("head", (0, -0.02, hz + 0.72), (0.1, 0.12, 0.12), bone_c, seg=(8, 6))                         # skull
    M.box("head", (0, -0.1, hz + 0.63), (0.12, 0.07, 0.05), bone_c, bevel=0.015)                       # jaw
    for s in (1, -1):
        M.ell("head", (s * 0.045, -0.11, hz + 0.73), (0.03, 0.02, 0.025), eye, seg=(5, 4), emissive=True)
    # the shroud: a hood and a rag over the shoulders and hips, matte wool
    M.ell("head", (0, 0.03, hz + 0.75), (0.15, 0.15, 0.15), cloth, seg=(8, 6), noise_amt=0.25)
    M.ell("chest", (0, 0.06, hz + 0.5), (0.24, 0.13, 0.16), cloth, seg=(8, 5), noise_amt=0.3)
    for k in range(6):                                                                                  # a rag of strips round the hips
        a = TAU * k / 6 + 0.5
        top = (0.17 * math.cos(a), 0.03 + 0.13 * math.sin(a), hz + 0.1)
        bot = (0.22 * math.cos(a), 0.05 + 0.17 * math.sin(a), hz - 0.3 - 0.12 * ((k * 5) % 3))
        M.limb("hips", top, bot, 0.08, 0.03, cloth, seg=4, noise_amt=0.35,
               w={"hips": 0.5, "thighL": 0.5} if top[0] > 0 else {"hips": 0.5, "thighR": 0.5})
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 0.62 - 0.08
        M.ell("chest", (s * 0.22, 0, sz + 0.02), (0.05, 0.05, 0.05), bone_c, seg=(6, 4))
        M.limb("uarm" + side, (s * 0.22, 0, sz), (s * 0.22, 0, sz - 0.34), 0.03, 0.025, bone_c, seg=5)
        M.limb("farm" + side, (s * 0.22, 0, sz - 0.34), (s * 0.22, 0, sz - 0.68), 0.025, 0.02, bone_c, seg=5)
        M.limb("thigh" + side, (s * 0.11, 0, hz), (s * 0.11, 0, hz - 0.48), 0.035, 0.03, bone_c, seg=5)
        M.ell("shin" + side, (s * 0.11, 0, hz - 0.48), (0.04, 0.04, 0.04), old, seg=(5, 4))
        M.limb("shin" + side, (s * 0.11, 0, hz - 0.48), (s * 0.11, 0, hz - 0.92), 0.03, 0.025, bone_c, seg=5)
        M.box("foot" + side, (s * 0.11, -0.07, 0.03), (0.07, 0.2, 0.05), old, bevel=0.015)
    M.limb("farmL", (0.22, -0.02, hz + 0.54 - 0.68), (0.22, -0.04, hz + 0.54 - 0.78), 0.03, 0.01, old, seg=5)  # claw fingers
    # the notched blade fused to the right forearm, pointing down past the hand
    M.box("farmR", (-0.22, -0.02, hz + 0.54 - 0.6), (0.06, 0.04, 0.1), rust, bevel=0.01)
    M.box("farmR", (-0.22, -0.02, hz + 0.54 - 0.98), (0.02, 0.1, 0.62), steel, bevel=0.005)
    M.spike("farmR", (-0.22, -0.02, hz + 0.54 - 1.28), (-0.22, -0.04, hz + 0.54 - 1.4), 0.04, steel, seg=4)
    return M

def bonewalker_poses():
    walk = []
    for f in range(WALK):
        p = walk_pose(f, WALK, A_leg=0.45, K=0.6, arm=0.15, bounce=0.035, spine_lean=0.45, twist=0.25, arm_bend=0.15, arm_fwd=0.15)
        ph = TAU * f / WALK
        p["head"] = (p["head"][0] + 0.25, 0.15 * math.sin(ph), p["head"][2])      # a stiff, lolling head
        p["uarmR"] = (0.25, 0, 0.15); p["farmR"] = (-0.25, 0, 0)               # the blade arm hangs, dragging
        walk.append(p)
    attack = []
    # a backhand slash: the blade arm thrown far back across the body (the tell), swept out across the front,
    # then hanging forward
    for twist, uarm, farm, lean, crouch in (
            (-0.7, (-0.3, 0, 1.2), (-0.6, 0, 0), 0.2, 0.15),
            (0.75, (-1.45, 0, -0.9), (-0.1, 0, 0), 0.5, 0.3),
            (0.35, (-0.9, 0, -0.4), (-0.3, 0, 0), 0.4, 0.2)):
        p = stance(crouch, 0.1)
        p["thighL"] = (-crouch - 0.3, 0, 0.1)
        p["hips"] = (lean * 0.3, 0, twist * 0.3, 0, -0.04, 0)
        p["spine"] = (lean * 0.4, 0, twist * 0.6)
        p["chest"] = (lean * 0.3, 0, twist * 0.4)
        p["head"] = (-lean * 0.5, 0, -twist * 0.4)
        p["uarmR"] = uarm; p["farmR"] = farm
        p["uarmL"] = (-0.5, 0, 0.3); p["farmL"] = (-0.4, 0, 0)
        attack.append(p)
    return [walk, attack]

# ───────────────────────── level 2 boss: Vhessa ───────────────────────

VH_LEGS = [("legL0", 0.75, -0.45), ("legL1", 0.9, 0.1), ("legL2", 0.75, 0.65),
           ("legR0", -0.75, -0.45), ("legR1", -0.9, 0.1), ("legR2", -0.75, 0.65)]

def make_vhessa():
    M = Model()
    bz = 1.15
    bug_rig(M, bz, VH_LEGS)
    M.bone("abdo", "body", (0, 0.5, bz), (0, 1.1, bz - 0.1))
    for s, side in ((1, "L"), (-1, "R")):
        M.bone("arm" + side, "body", (s * 0.6, -0.7, bz + 0.1), (s * 0.6, -0.7, bz - 0.3))
        M.bone("claw" + side, "arm" + side, (s * 1.0, -1.5, bz + 0.5), (s * 1.0, -1.5, bz + 0.1))
    shell = (0.15, 0.1, 0.07); ridge = (0.36, 0.27, 0.15); belly = (0.58, 0.5, 0.36); leg = (0.2, 0.13, 0.09)
    tip = (0.72, 0.64, 0.46); sac = (1.2, 3.2, 0.9); eye = (2.4, 4.0, 0.6); spot = (0.55, 0.5, 0.2)
    M.ell("body", (0, 0, bz + 0.2), (1.0, 0.95, 0.42), shell, seg=(14, 8), noise_amt=0.22)        # the great carapace
    for s in (1, -1):                                                                                # ochre markings
        M.ell("body", (s * 0.42, -0.2, bz + 0.5), (0.22, 0.32, 0.08), spot, seg=(7, 4), noise_amt=0.3)
    M.ell("body", (0, 0, bz - 0.12), (0.85, 0.8, 0.25), belly, seg=(10, 6))
    for k in range(4):                                                                               # shell ridges
        M.box("body", (0, -0.5 + 0.33 * k, bz + 0.58 - abs(k - 1.5) * 0.06), (1.3 - abs(k - 1.5) * 0.3, 0.1, 0.08), ridge, bevel=0.03)
    for s in (1, -1):
        for k in range(3):                                                                           # spikes along the rim
            M.spike("body", (s * 0.92, -0.4 + 0.45 * k, bz + 0.3), (s * 1.3, -0.45 + 0.5 * k, bz + 0.5), 0.07, tip, seg=5)
    M.ell("abdo", (0, 0.95, bz - 0.05), (0.62, 0.62, 0.36), shell, seg=(10, 7), noise_amt=0.2)        # the brood abdomen
    for k in range(9):                                                                               # egg sacs on the abdomen's surface
        a = -1.2 + 0.3 * k
        x, y, z = 0.6 * math.sin(a), 0.95 + 0.62 * math.cos(a) * 0.7, 0.18 + 0.1 * math.cos(a * 2)
        r = 0.13 + 0.03 * ((k * 7) % 3)
        M.ell("abdo", (x * 0.95, y, bz + z), (r, r, r * 0.8), sac, seg=(6, 4), emissive=True)
    M.ell("head", (0, -0.92, bz + 0.12), (0.42, 0.3, 0.24), shell, seg=(10, 6))
    for s in (1, -1):
        for k in range(3):
            M.ell("head", (s * (0.08 + 0.1 * k), -1.18, bz + 0.22 - 0.03 * k), (0.045, 0.04, 0.04), eye, seg=(5, 4), emissive=True)
        M.spike("head", (s * 0.12, -1.15, bz - 0.02), (s * 0.05, -1.45, bz - 0.15), 0.06, tip, seg=5)   # mandibles
        M.limb("head", (s * 0.1, -1.1, bz + 0.25), (s * 0.5, -1.9, bz + 0.65), 0.02, 0.008, tip, seg=4)  # feelers
    for name, x, y in VH_LEGS:
        s = 1 if x > 0 else -1
        sy = 1 if y > 0 else -1
        knee = (x + s * 0.7, y + 0.15 * sy, bz + 0.65)
        foot = (x + s * 1.2, y + 0.3 * sy, 0.0)
        M.limb(name, (x, y, bz), knee, 0.17, 0.13, leg, seg=6)
        M.ell(name, knee, (0.14, 0.14, 0.13), ridge, seg=(6, 4))
        M.limb(name, knee, foot, 0.13, 0.03, leg, seg=6)
    for s, side in ((1, "L"), (-1, "R")):                                                           # the pincers
        a = (s * 0.6, -0.7, bz + 0.1); b = (s * 1.0, -1.5, bz + 0.3)
        M.limb("arm" + side, a, b, 0.2, 0.17, leg, seg=7)
        M.ell("claw" + side, (s * 1.05, -1.9, bz + 0.3), (0.45, 0.62, 0.32), shell, seg=(10, 7), noise_amt=0.15)
        M.ell("claw" + side, (s * 1.05, -1.9, bz + 0.5), (0.3, 0.45, 0.12), ridge, seg=(8, 4), noise_amt=0.2)
        M.spike("claw" + side, (s * 1.05, -2.35, bz + 0.42), (s * 0.75, -3.25, bz + 0.32), 0.2, ridge, seg=6)   # upper jaw
        M.spike("claw" + side, (s * 1.12, -2.35, bz + 0.15), (s * 1.0, -3.0, bz + 0.05), 0.16, tip, seg=6)     # lower jaw
        for k in range(3):
            M.ell("claw" + side, (s * (1.15 + 0.1 * k), -1.7 - 0.15 * k, bz + 0.58), (0.06, 0.06, 0.04), spot, seg=(5, 3))
    return M

def vhessa_poses():
    walk = []
    for f in range(BOSS_WALK):
        p = bug_walk(f, BOSS_WALK, VH_LEGS, swing=0.3, lift=0.3, bob=0.06, sway=0.05)
        ph = TAU * f / BOSS_WALK
        p["armL"] = (-0.1 + 0.08 * math.sin(ph), 0, 0); p["armR"] = (-0.1 - 0.08 * math.sin(ph), 0, 0)
        p["clawL"] = (0, 0, 0); p["clawR"] = (0, 0, 0)
        p["abdo"] = (0.04 * math.sin(ph * 2), 0, 0.05 * math.sin(ph))
        walk.append(p)
    def base(pitch=0.0, drop=0.0):
        p = {"body": (pitch, 0, 0, 0, drop, 0), "head": (-pitch * 0.5, 0, 0), "abdo": (0, 0, 0)}
        for name, x, y in VH_LEGS:
            p[name] = (-pitch * (1 if y < 0 else 0.6), 0, (1 if x > 0 else -1) * (-0.25 if drop < 0 else 0))
        return p
    attack = []
    # the pincer sweep: both pincers raised high and drawn out to the right (the tell), swept across the front
    # to the left, then lowered
    for twist, arm, claw in ((0.45, -1.1, -0.4), (-0.5, -0.4, 0.2), (-0.2, -0.3, 0.0)):
        p = base(-0.08)
        p["body"] = (-0.08, 0, twist * 0.5, 0, 0, 0)
        p["armL"] = (arm, 0, twist); p["armR"] = (arm, 0, twist)
        p["clawL"] = (claw, 0, 0); p["clawR"] = (claw, 0, 0)
        attack.append(p)
    special = []
    # the acid jet: reared up on the back legs with the belly forward (the tell), the head thrust down at the
    # floor in front (release), settling
    for pitch, head, drop in ((-0.55, -0.25, 0.25), (-0.2, 0.55, 0.05), (-0.05, 0.2, 0.0)):
        p = base(pitch, drop)
        p["head"] = (head, 0, 0)
        p["armL"] = (-0.8, 0, 0.5); p["armR"] = (-0.8, 0, -0.5)
        p["abdo"] = (0.3 * -pitch, 0, 0)
        special.append(p)
    summon = []
    # the brood: squatting low with the abdomen pressed to the floor and the pincers spread, then the abdomen
    # heaved up
    for drop, abdo in ((-0.45, 0.45), (-0.15, -0.3)):
        p = base(0.1, drop)
        p["abdo"] = (abdo, 0, 0)
        p["armL"] = (-0.4, 0, 0.6); p["armR"] = (-0.4, 0, -0.6)
        summon.append(p)
    return [walk, attack, special, summon]

# ───────────────────────── level 3 boss: Ossivar ──────────────────────

def make_ossivar():
    M = Model()
    hz = 1.75
    humanoid_rig(M, hz, 0.87, 0.87, 1.35, 0.4, 0.7, 0.7, 0.55, 0.22)
    bone_c = (0.8, 0.74, 0.6); old = (0.5, 0.45, 0.36); robe = (0.06, 0.055, 0.06); robe2 = (0.12, 0.08, 0.07)
    gold = (4.0, 2.6, 0.7); eye = (4.0, 2.8, 0.8); wood = (0.25, 0.18, 0.12); trim = (0.42, 0.34, 0.18)
    tw = torso_weights(hz, 0.1, 0.5, 0.6, 1.0)
    # the robe, open at the chest: a heavy hem to the floor, matte wool
    M.limb("hips", (0, 0.05, hz + 0.25), (0, 0.1, 0.04), 0.45, 0.62, robe, seg=10, noise_amt=0.35,
           w=lambda p: {"hips": 0.4 + 0.6 * smoothstep(0.0, hz, p.z), "thighL": 0.6 * (1 - smoothstep(0.0, hz, p.z)) * (1 if p.x > 0 else 0.2), "thighR": 0.6 * (1 - smoothstep(0.0, hz, p.z)) * (1 if p.x < 0 else 0.2)})
    for k in range(9):                                                                                 # a torn hem: hanging strips
        a = TAU * k / 9 + 0.2
        r = 0.6
        top = (r * math.cos(a), 0.1 + r * math.sin(a), 0.75 + 0.15 * ((k * 5) % 3))
        bot = (r * 1.12 * math.cos(a), 0.1 + r * 1.12 * math.sin(a), 0.02 + 0.06 * ((k * 3) % 2))
        M.limb("hips", top, bot, 0.13, 0.05, robe2 if k % 2 else robe, seg=4, noise_amt=0.3,
               w={"thighL": 0.5, "hips": 0.5} if top[0] > 0 else {"thighR": 0.5, "hips": 0.5})
    M.ell("hips", (0, 0.05, hz + 0.1), (0.5, 0.42, 0.25), trim, seg=(10, 4), noise_amt=0.2)          # the girdle
    M.ell("chest", (0, 0.18, hz + 0.95), (0.78, 0.46, 0.52), robe, seg=(10, 7), noise_amt=0.3)          # the mantle, hunched high
    for k in range(5):                                                                                  # bone spurs through it
        x = -0.5 + 0.25 * k
        M.spike("chest", (x, 0.35, hz + 1.2), (x * 1.5, 0.75, hz + 1.65 - abs(x) * 0.3), 0.07, bone_c, seg=5)
    M.ell("spine", (0, 0.0, hz + 0.6), (0.38, 0.26, 0.4), robe2, seg=(9, 6), w=tw, noise_amt=0.25)
    # the open ribcage, lit gold from inside
    M.ell("chest", (0, -0.2, hz + 0.85), (0.26, 0.14, 0.26), gold, seg=(8, 6), emissive=True)
    for k in range(6):
        z = hz + 0.62 + k * 0.08
        r = 0.32 - abs(k - 2.5) * 0.03
        M.ell("chest", (0, -0.08, z), (r, 0.2, 0.026), bone_c, seg=(10, 3))
    M.box("chest", (0, -0.27, hz + 0.84), (0.06, 0.04, 0.42), bone_c, bevel=0.01)
    M.ell("head", (0, -0.05, hz + 1.42), (0.24, 0.27, 0.26), bone_c, seg=(10, 7))                     # skull
    M.box("head", (0, -0.2, hz + 1.24), (0.2, 0.12, 0.08), bone_c, bevel=0.02)                        # jaw
    for s in (1, -1):
        M.ell("head", (s * 0.075, -0.22, hz + 1.4), (0.05, 0.03, 0.04), eye, seg=(5, 4), emissive=True)
        for k in range(3):                                                                            # the antler crown
            a = 0.35 + 0.35 * k
            M.spike("head", (s * 0.1, 0.0, hz + 1.52), (s * (0.35 + 0.28 * k), 0.12 * k - 0.05, hz + 2.05 + 0.3 * math.sin(a * 2)), 0.08 - 0.012 * k, bone_c, seg=5)
        M.spike("head", (s * 0.2, 0.05, hz + 1.7), (s * 0.55, 0.15, hz + 2.0), 0.04, old, seg=5)
    M.ell("head", (0, 0.06, hz + 1.42), (0.28, 0.26, 0.26), robe, seg=(9, 6), noise_amt=0.25)         # the hood
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 1.2 - 0.08
        M.ell("chest", (s * 0.45, 0.02, sz + 0.06), (0.22, 0.2, 0.18), robe, seg=(8, 5), noise_amt=0.3)
        M.limb("uarm" + side, (s * 0.45, 0, sz), (s * 0.45, 0, sz - 0.62), 0.14, 0.16, robe, seg=7, noise_amt=0.3)   # sleeve
        M.limb("farm" + side, (s * 0.45, 0, sz - 0.62), (s * 0.45, 0, sz - 1.02), 0.17, 0.22, robe2, seg=7, noise_amt=0.3)
        M.limb("farm" + side, (s * 0.45, 0, sz - 1.0), (s * 0.45, -0.02, sz - 1.25), 0.04, 0.03, bone_c, seg=5)        # hand
        for k in (-1, 0, 1):
            M.spike("farm" + side, (s * 0.45 + k * 0.03, -0.02, sz - 1.23), (s * 0.45 + k * 0.05, -0.08, sz - 1.4), 0.016, bone_c, seg=4)
    # the crozier in the right hand: a long staff with a hooked head of bone and a gold lamp
    sz = hz + 1.2 - 0.08
    M.limb("farmR", (-0.45, -0.12, sz - 1.2 - 1.3), (-0.45, -0.12, sz - 1.2 + 1.2), 0.04, 0.035, wood, seg=6)
    M.limb("farmR", (-0.45, -0.12, sz + 0.0), (-0.45, -0.32, sz + 0.25), 0.05, 0.04, bone_c, seg=5)
    M.limb("farmR", (-0.45, -0.32, sz + 0.25), (-0.45, -0.25, sz + 0.5), 0.04, 0.03, bone_c, seg=5)
    M.limb("farmR", (-0.45, -0.25, sz + 0.5), (-0.45, -0.05, sz + 0.42), 0.03, 0.015, bone_c, seg=5)
    M.ell("farmR", (-0.45, -0.3, sz + 0.12), (0.06, 0.06, 0.07), gold, seg=(6, 4), emissive=True)
    M.box("farmR", (-0.45, -0.12, sz - 0.05), (0.1, 0.1, 0.1), trim, bevel=0.02)
    return M

def ossivar_poses():
    walk = []
    for f in range(BOSS_WALK):
        p = walk_pose(f, BOSS_WALK, A_leg=0.28, K=0.35, arm=0.12, bounce=0.03, spine_lean=0.12, twist=0.12, arm_bend=0.3)
        ph = TAU * f / BOSS_WALK
        p["uarmR"] = (-0.35, 0, 0.1); p["farmR"] = (-0.35, 0, 0)          # the crozier carried upright
        p["head"] = (p["head"][0] + 0.05, 0, 0.08 * math.sin(ph))
        walk.append(p)
    attack = []
    # the crozier slam: both hands high on the staff over the head (the tell), driven down into the floor with
    # the body folded over it, then straightening
    for arm, farm, lean, crouch in ((-2.6, -0.4, -0.15, 0.1), (-1.0, -0.6, 0.55, 0.4), (-0.8, -0.5, 0.3, 0.25)):
        p = stance(crouch, 0.15)
        p["hips"] = (lean * 0.3, 0, 0, 0, -0.08 * crouch, 0)
        p["spine"] = (lean * 0.4, 0, 0); p["chest"] = (lean * 0.3, 0, 0); p["head"] = (-lean * 0.5, 0, 0)
        p["uarmR"] = (arm, 0, -0.3); p["farmR"] = (farm, 0, 0)
        p["uarmL"] = (arm + 0.1, 0, 0.5); p["farmL"] = (farm - 0.2, 0, 0)
        attack.append(p)
    special = []
    # the bone lances: arms flung back and the burning chest thrust forward (the tell), then the arms swept
    # forward as the fan flies, then recovering
    for lean, arm, spread in ((-0.35, 0.9, 0.7), (0.35, -1.5, 0.3), (0.15, -0.6, 0.4)):
        p = stance(0.1, 0.12)
        p["spine"] = (lean * 0.4, 0, 0); p["chest"] = (lean * 0.5, 0, 0); p["head"] = (-lean * 0.6, 0, 0)
        p["uarmL"] = (arm, 0, spread); p["farmL"] = (-0.3, 0, 0)
        p["uarmR"] = (arm * 0.6, 0, -spread); p["farmR"] = (-0.2, 0, 0)
        special.append(p)
    summon = []
    # raise the dead: kneeling with the arms spread wide and low (gather), then the arms raised high
    for crouch, arm, spread in ((0.75, -0.3, 1.2), (0.2, -2.3, 0.6)):
        p = stance(crouch, 0.25)
        p["thighL"] = (-crouch - 0.4, 0, 0.2)
        p["hips"] = (0.1, 0, 0, 0, -0.35 * crouch, 0)
        p["spine"] = (0.2 * crouch, 0, 0); p["head"] = (-0.3 if arm < -1 else 0.3, 0, 0)
        p["uarmL"] = (arm, 0, spread); p["uarmR"] = (arm, 0, -spread)
        p["farmL"] = (-0.2, 0, 0); p["farmR"] = (-0.2, 0, 0)
        summon.append(p)
    return [walk, attack, special, summon]

# ───────────────────────── level 4: the slagborn ─────────────────────

def make_slagborn():
    M = Model()
    hz = 0.9
    humanoid_rig(M, hz, 0.42, 0.44, 0.62, 0.16, 0.42, 0.5, 0.36, 0.17)
    slag = (0.08, 0.07, 0.07); crust = (0.16, 0.13, 0.12); ember = (4.5, 1.4, 0.25); hot = (5.0, 2.6, 0.6)
    tw = torso_weights(hz, 0.05, 0.25, 0.3, 0.5)
    M.ell("spine", (0, 0.02, hz + 0.28), (0.3, 0.24, 0.3), slag, w=tw, seg=(9, 6), noise_amt=0.35)
    M.ell("chest", (0, 0.06, hz + 0.52), (0.44, 0.3, 0.24), crust, seg=(10, 6), noise_amt=0.4)        # hunched shoulders
    for k, (x, z, r) in enumerate([(0.12, 0.3, 0.07), (-0.1, 0.42, 0.06), (0.05, 0.5, 0.05), (-0.16, 0.22, 0.05)]):  # cracks glowing through
        M.ell("chest" if z > 0.35 else "spine", (x, -0.24, hz + z), (r, 0.03, r * 1.6), ember, seg=(5, 3), emissive=True)
    M.ell("hips", (0, 0, hz), (0.24, 0.18, 0.14), slag, seg=(8, 5), noise_amt=0.3)
    M.ell("head", (0, -0.12, hz + 0.62), (0.16, 0.17, 0.14), crust, seg=(8, 5), noise_amt=0.35)       # a heavy low head
    M.box("head", (0, -0.27, hz + 0.63), (0.2, 0.03, 0.035), hot, bevel=0.0)                            # the molten slot
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 0.62 - 0.08
        M.ell("chest", (s * 0.38, 0.04, sz + 0.04), (0.17, 0.16, 0.14), crust, seg=(7, 5), noise_amt=0.4)
        M.limb("uarm" + side, (s * 0.36, 0, sz), (s * 0.36, 0, sz - 0.42), 0.11, 0.1, slag, seg=6, noise_amt=0.3)
        M.limb("farm" + side, (s * 0.36, 0, sz - 0.42), (s * 0.36, 0, sz - 0.92), 0.1, 0.14, crust, seg=6, noise_amt=0.35)
        M.ell("farm" + side, (s * 0.36, -0.03, sz - 0.98), (0.14, 0.14, 0.12), slag, seg=(7, 5), noise_amt=0.35)   # hands that hang low
        M.ell("farm" + side, (s * 0.36, -0.12, sz - 0.95), (0.06, 0.03, 0.06), ember, seg=(5, 3), emissive=True)
        M.limb("thigh" + side, (s * 0.17, 0, hz), (s * 0.17, 0, hz - 0.42), 0.12, 0.1, slag, seg=6, noise_amt=0.3)
        M.limb("shin" + side, (s * 0.17, 0, hz - 0.42), (s * 0.17, 0, hz - 0.86), 0.1, 0.12, crust, seg=6, noise_amt=0.3)
        M.box("foot" + side, (s * 0.17, -0.07, 0.05), (0.18, 0.28, 0.1), slag, bevel=0.03)
    return M

def slagborn_poses():
    walk = []
    for f in range(WALK):
        p = walk_pose(f, WALK, A_leg=0.42, K=0.55, arm=0.25, bounce=0.06, spine_lean=0.35, twist=0.3, arm_bend=0.15, arm_fwd=0.1)
        walk.append(p)
    attack = []
    # the lob: hands cupped together low in front, brightening (the tell), flung up and forward (release),
    # then hanging again
    for lean, arm, farm, crouch in ((0.5, -0.5, -1.3, 0.3), (-0.15, -2.4, -0.2, 0.1), (0.3, -1.0, -0.4, 0.2)):
        p = stance(crouch, 0.15)
        p["spine"] = (lean * 0.5, 0, 0); p["chest"] = (lean * 0.3, 0, 0); p["head"] = (-lean * 0.6, 0, 0)
        p["uarmL"] = (arm, 0, -0.35); p["uarmR"] = (arm, 0, 0.35)
        p["farmL"] = (farm, 0, 0); p["farmR"] = (farm, 0, 0)
        attack.append(p)
    return [walk, attack]

# ───────────────────────── level 4 boss: Anvilgrave ───────────────────

def make_anvilgrave():
    M = Model()
    hz = 1.7
    humanoid_rig(M, hz, 0.85, 0.85, 1.4, 0.4, 0.8, 0.85, 0.95, 0.42)
    iron = (0.2, 0.19, 0.19); rivet = (0.35, 0.32, 0.3); hide = (0.18, 0.1, 0.08); chain = (0.3, 0.29, 0.28)
    ember = (5.0, 1.8, 0.3); hot = (6.0, 3.2, 0.8); wood = (0.22, 0.14, 0.09)
    tw = torso_weights(hz, 0.1, 0.6, 0.7, 1.2)
    M.ell("spine", (0, 0, hz + 0.6), (0.62, 0.48, 0.62), hide, w=tw, seg=(12, 8), noise_amt=0.2)
    M.box("chest", (0, -0.1, hz + 1.05), (1.4, 0.95, 0.85), iron, bevel=0.12, noise_amt=0.2)               # the cuirass
    for x in (-0.55, -0.2, 0.2, 0.55):
        for z in (0.75, 1.35):
            M.ell("chest", (x, -0.59, hz + z), (0.05, 0.03, 0.05), rivet, seg=(5, 3))
    M.ell("chest", (0, -0.6, hz + 1.05), (0.2, 0.06, 0.22), ember, seg=(7, 5), emissive=True)            # a furnace grille
    for k in range(3):
        M.box("chest", (0, -0.64, hz + 0.95 + 0.1 * k), (0.34, 0.03, 0.03), iron, bevel=0.0)
    M.ell("hips", (0, 0, hz), (0.6, 0.45, 0.3), iron, seg=(10, 6), noise_amt=0.2)
    for s in (1, -1):                                                                                   # tassets
        M.box("hips", (s * 0.4, -0.3, hz - 0.3), (0.42, 0.1, 0.55), iron, rot=(0.15, 0, 0), bevel=0.04)
    # the helm: a horned furnace mouth
    M.box("head", (0, -0.08, hz + 1.6), (0.6, 0.62, 0.62), iron, bevel=0.08, noise_amt=0.15)
    M.box("head", (0, -0.4, hz + 1.55), (0.42, 0.04, 0.2), hot, bevel=0.0)                              # the mouth slot
    for s in (1, -1):
        M.spike("head", (s * 0.28, 0.0, hz + 1.8), (s * 0.85, 0.2, hz + 2.35), 0.12, rivet, seg=6)
        M.ell("head", (s * 0.14, -0.4, hz + 1.72), (0.05, 0.03, 0.03), ember, seg=(5, 3), emissive=True)
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 1.4 - 0.1
        M.ell("chest", (s * 0.95, 0, sz + 0.1), (0.38, 0.36, 0.3), iron, seg=(9, 6), noise_amt=0.15)      # pauldrons
        M.limb("uarm" + side, (s * 0.95, 0, sz), (s * 0.95, 0, sz - 0.8), 0.24, 0.22, hide, seg=8)
        M.limb("farm" + side, (s * 0.95, 0, sz - 0.8), (s * 0.95, 0, sz - 1.6), 0.25, 0.27, iron, seg=8, noise_amt=0.15)
        for k in range(3):                                                                             # chains wound on the forearms
            ring_z = sz - 1.0 - 0.18 * k
            M.ell("farm" + side, (s * 0.95, 0, ring_z), (0.3, 0.3, 0.05), chain, seg=(10, 3))
        M.ell("farm" + side, (s * 0.95, -0.05, sz - 1.72), (0.24, 0.24, 0.22), hide, seg=(7, 5))
        M.limb("thigh" + side, (s * 0.42, 0, hz), (s * 0.42, 0, hz - 0.85), 0.3, 0.25, hide, seg=8)
        M.limb("shin" + side, (s * 0.42, 0, hz - 0.85), (s * 0.42, 0, hz - 1.6), 0.27, 0.3, iron, seg=8)
        M.box("foot" + side, (s * 0.42, -0.2, 0.1), (0.45, 0.75, 0.2), iron, bevel=0.06)
    # the forge hammer in the right hand: a long haft with a huge glowing-edged head
    sz = hz + 1.4 - 0.1
    hz0 = sz - 1.72                                                                                     # the hand; the hammer hangs head-down
    M.limb("farmR", (-0.95, -0.1, hz0 + 0.3), (-0.95, -0.1, hz0 - 0.75), 0.07, 0.07, wood, seg=6)
    M.box("farmR", (-0.95, -0.1, hz0 - 0.95), (0.55, 1.1, 0.5), iron, bevel=0.06, noise_amt=0.2)
    M.box("farmR", (-0.95, -0.67, hz0 - 0.95), (0.5, 0.04, 0.42), ember, bevel=0.0)
    M.box("farmR", (-0.95, 0.47, hz0 - 0.95), (0.5, 0.04, 0.42), ember, bevel=0.0)
    return M

def anvilgrave_poses():
    walk = []
    for f in range(BOSS_WALK):
        p = walk_pose(f, BOSS_WALK, A_leg=0.35, K=0.45, arm=0.15, bounce=0.09, spine_lean=0.12, twist=0.2, arm_bend=0.3)
        p["uarmR"] = (0.15, 0, 0.12); p["farmR"] = (0.0, 0, 0)    # the hammer dragged head-down at the side
        walk.append(p)
    attack = []
    # the hammer fall: both hands high over the head with the hammer behind (the tell), driven down onto the
    # floor in front with the body folded, then hauled back up
    for arm, farm, lean, crouch in ((-3.0, -0.3, -0.2, 0.1), (-1.3, -0.1, 0.6, 0.45), (-1.6, -0.4, 0.35, 0.3)):
        p = stance(crouch, 0.2)
        p["hips"] = (lean * 0.3, 0, 0, 0, -0.1 * crouch, 0)
        p["spine"] = (lean * 0.4, 0, 0); p["chest"] = (lean * 0.3, 0, 0); p["head"] = (-lean * 0.5, 0, 0)
        p["uarmR"] = (arm, 0, -0.3); p["farmR"] = (farm, 0, 0)
        p["uarmL"] = (arm, 0, 0.35); p["farmL"] = (farm, 0, 0)
        attack.append(p)
    special = []
    # the pour: the left arm swung out and up hooking a crucible on its chain (the tell), tipped over in front
    # (release), lowered
    for armL, twist, lean in (((-1.8, 0, 1.3), 0.4, -0.1), ((-1.3, 0, 0.3), -0.3, 0.35), ((-0.6, 0, 0.2), -0.1, 0.15)):
        p = stance(0.15, 0.2)
        p["spine"] = (lean * 0.4, 0, twist * 0.6); p["chest"] = (lean * 0.3, 0, twist * 0.5)
        p["uarmL"] = armL; p["farmL"] = (-0.4, 0, 0)
        p["uarmR"] = (0.15, 0, 0.12); p["farmR"] = (0.0, 0, 0)
        special.append(p)
    summon = []
    # the bellows roar: head thrown back and arms wide (gather), then fists down (release)
    for head, arm, spread in ((-0.6, -1.2, 1.2), (0.2, -0.3, 0.5)):
        p = stance(0.2, 0.25)
        p["head"] = (head, 0, 0); p["chest"] = (head * 0.3, 0, 0)
        p["uarmL"] = (arm, 0, spread); p["uarmR"] = (arm, 0, -spread)
        p["farmL"] = (-0.3, 0, 0); p["farmR"] = (-0.3, 0, 0)
        summon.append(p)
    return [walk, attack, special, summon]

# ───────────────────────── level 5: the sporeling ─────────────────────

def make_sporeling():
    M = Model()
    hz = 0.42
    humanoid_rig(M, hz, 0.2, 0.2, 0.55, 0.3, 0.28, 0.3, 0.26, 0.13)
    stalk = (0.62, 0.58, 0.5); cap = (0.34, 0.07, 0.05); spot = (0.75, 0.7, 0.6); gill = (0.5, 2.8, 3.0); root = (0.36, 0.3, 0.24)
    tw = torso_weights(hz, 0.05, 0.25, 0.3, 0.5)
    M.limb("spine", (0, 0, hz - 0.05), (0, 0, hz + 0.6), 0.3, 0.2, stalk, w=tw, seg=9, noise_amt=0.25)       # the fat stalk body
    M.ell("hips", (0, 0, hz), (0.3, 0.28, 0.2), stalk, seg=(9, 5), noise_amt=0.25)
    M.ell("head", (0, 0, hz + 0.8), (0.46, 0.46, 0.2), cap, seg=(12, 6), noise_amt=0.3)                   # the broad cap
    M.ell("head", (0, 0, hz + 0.7), (0.47, 0.47, 0.05), gill, seg=(12, 3), emissive=True)                  # glowing gills under it, their rim showing
    for k in range(7):
        a = TAU * k / 7 + 0.3
        rr = 0.3 if k % 2 else 0.18
        M.ell("head", (rr * math.cos(a), rr * math.sin(a), hz + 0.8 + 0.2 * math.sqrt(1 - (rr / 0.46) ** 2) - 0.005), (0.07, 0.07, 0.03), gill if k % 3 == 0 else spot, seg=(6, 3), emissive=k % 3 == 0)
    for s in (1, -1):
        M.ell("chest", (s * 0.08, -0.24, hz + 0.5), (0.035, 0.02, 0.03), gill, seg=(5, 3), emissive=True)   # little eyes
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 0.55 - 0.08
        M.limb("uarm" + side, (s * 0.26, 0, sz), (s * 0.3, 0, sz - 0.28), 0.04, 0.03, root, seg=5)
        M.limb("farm" + side, (s * 0.3, 0, sz - 0.28), (s * 0.3, -0.02, sz - 0.58), 0.03, 0.01, root, seg=5)
        M.limb("thigh" + side, (s * 0.13, 0, hz), (s * 0.14, 0, hz - 0.2), 0.11, 0.09, stalk, seg=6)
        M.limb("shin" + side, (s * 0.14, 0, hz - 0.2), (s * 0.15, 0, hz - 0.4), 0.09, 0.11, root, seg=6)
        for k in (-1, 0, 1):                                                                              # root toes
            M.spike("foot" + side, (s * 0.15, 0, 0.04), (s * 0.15 + k * 0.1, -0.16, 0.0), 0.04, root, seg=4)
    return M

def sporeling_poses():
    walk = []
    for f in range(WALK):
        p = walk_pose(f, WALK, A_leg=0.4, K=0.4, arm=0.6, bounce=0.06, spine_lean=0.0, twist=0.45, arm_bend=0.3)
        ph = TAU * f / WALK
        p["spine"] = (p["spine"][0], 0.0, 0.25 * math.sin(ph))      # a big side-to-side waddle
        p["head"] = (0.05 * math.sin(ph * 2), 0, -0.2 * math.sin(ph))
        walk.append(p)
    attack = []
    # the headbutt: the cap tilted back (the tell), slammed forward and down, then rocked up
    for head, lean in ((-0.6, -0.25), (0.75, 0.5), (0.25, 0.2)):
        p = stance(0.15, 0.1)
        p["spine"] = (lean, 0, 0); p["head"] = (head, 0, 0)
        p["uarmL"] = (-0.4, 0, 0.6); p["uarmR"] = (-0.4, 0, -0.6)
        attack.append(p)
    return [walk, attack]

# ───────────────────────── level 5 boss: Mother Mycel ─────────────────

MM_T = 6

def make_mycel():
    M = Model()
    bz = 1.6
    M.bone("body", None, (0, 0, bz - 0.5), (0, 0, bz + 0.5))
    M.bone("crown", "body", (0, 0, bz + 1.2), (0, 0, bz + 1.8))
    M.bone("maw", "body", (0, -1.0, bz), (0, -1.0, bz + 0.4))
    for k in range(MM_T):
        a = TAU * k / MM_T + 0.25
        M.bone("ten%d" % k, "body", (1.1 * math.cos(a), 1.1 * math.sin(a), bz - 0.6), (2.0 * math.cos(a), 2.0 * math.sin(a), 0.2))
    flesh = (0.52, 0.47, 0.42); bulb = (0.3, 0.17, 0.3); shelf = (0.32, 0.22, 0.14); gill = (0.5, 3.0, 2.6)
    glow = (0.4, 4.0, 1.4); root = (0.32, 0.26, 0.22); dark = (0.08, 0.05, 0.07)
    M.ell("body", (0, 0, bz + 0.3), (1.35, 1.25, 1.25), bulb, seg=(14, 9), noise_amt=0.3)               # the bulb
    M.ell("body", (0, 0, bz - 0.55), (1.2, 1.15, 0.5), flesh, seg=(12, 6), noise_amt=0.3)
    # the maw: a vertical slit lined with pale gills, glowing inside
    M.ell("maw", (0, -1.12, bz + 0.25), (0.22, 0.2, 0.75), dark, seg=(8, 6))
    M.ell("maw", (0, -1.2, bz + 0.25), (0.08, 0.06, 0.6), gill, seg=(6, 5), emissive=True)
    for s in (1, -1):
        for k in range(4):
            M.ell("maw", (s * 0.18, -1.15, bz - 0.15 + 0.27 * k), (0.06, 0.08, 0.12), flesh, seg=(5, 3))
    # the crown: shelves of bracket fungus, glowing on their undersides
    for k in range(7):
        a = TAU * k / 7
        r = 0.75 + 0.15 * (k % 2)
        z = bz + 1.35 + 0.18 * (k % 3)
        M.ell("crown", (r * math.cos(a), r * math.sin(a), z), (0.55, 0.42, 0.1), shelf, rot=(0, 0, a), seg=(10, 4), noise_amt=0.25)
        M.ell("crown", (r * math.cos(a), r * math.sin(a), z - 0.08), (0.45, 0.33, 0.03), glow, rot=(0, 0, a), seg=(8, 3), emissive=True)
        M.ell("crown", ((r + 0.42) * math.cos(a), (r + 0.42) * math.sin(a), z + 0.03), (0.12, 0.12, 0.04), glow, seg=(5, 3), emissive=True)  # the glowing lip
    for k in range(12):                                                                                     # glowing pores on the bulb
        a = TAU * k / 12 + 0.2
        M.ell("body", (1.25 * math.cos(a) * 0.95, 1.15 * math.sin(a) * 0.95, bz + 0.2 + 0.35 * math.sin(k * 2.1)), (0.07, 0.07, 0.07), glow, seg=(5, 3), emissive=True)
    for k in range(MM_T):                                                                                   # the root tentacles
        a = TAU * k / MM_T + 0.25
        p0 = (1.0 * math.cos(a), 1.0 * math.sin(a), bz - 0.6)
        p1 = (1.9 * math.cos(a), 1.9 * math.sin(a), 0.5)
        p2 = (2.6 * math.cos(a + 0.15), 2.6 * math.sin(a + 0.15), 0.05)
        M.limb("ten%d" % k, p0, p1, 0.3, 0.2, root, seg=7, noise_amt=0.3)
        M.limb("ten%d" % k, p1, p2, 0.2, 0.05, root, seg=7, noise_amt=0.3)
    return M

def mycel_poses():
    walk = []
    for f in range(BOSS_WALK):
        ph = TAU * f / BOSS_WALK
        p = {"body": (0.04 * math.sin(ph), 0, 0.05 * math.sin(ph), 0, 0.06 * abs(math.sin(ph)), 0), "crown": (0, 0, 0.06 * math.sin(ph))}
        for k in range(MM_T):                                                                               # a rolling tide of tentacles
            a = ph + TAU * k / MM_T * 2
            p["ten%d" % k] = (0.25 * math.sin(a), 0.2 * math.cos(a), 0)
        walk.append(p)
    def base(body=(0, 0, 0, 0, 0, 0)):
        p = {"body": body, "crown": (0, 0, 0)}
        for k in range(MM_T): p["ten%d" % k] = (0, 0, 0)
        return p
    attack = []
    # the root lash: the two front tentacles reared high (the tell), whipped down in front, recoiling
    for lift, body in ((-1.4, -0.12), (0.7, 0.15), (0.2, 0.05)):
        p = base((body, 0, 0, 0, 0, 0))
        for k in (4, 5):                                                                     # the two at the front
            p["ten%d" % k] = (lift, 0, 0)
        attack.append(p)
    special = []
    # the spore mortar: the crown swollen up and back (the tell), thrown forward as the spores fly, settling
    for crown, rise in ((-0.35, 0.25), (0.3, 0.0), (0.1, 0.05)):
        p = base((0, 0, 0, 0, rise, 0))
        p["crown"] = (crown, 0, 0)
        special.append(p)
    summon = []
    # the maw opens: sunk low with the tentacles splayed (gather), then heaved up (release)
    for rise, splay in ((-0.35, 0.5), (0.15, -0.2)):
        p = base((0.1, 0, 0, 0, rise, 0))
        for k in range(MM_T): p["ten%d" % k] = (splay * 0.6, 0, 0)
        summon.append(p)
    return [walk, attack, special, summon]

# ───────────────────────── level 6: the penitent ──────────────────────

def make_penitent():
    M = Model()
    hz = 1.0
    humanoid_rig(M, hz, 0.4, 0.4, 0.6, 0.2, 0.36, 0.42, 0.24, 0.12)
    wool = (0.05, 0.045, 0.05); wool2 = (0.09, 0.075, 0.07); mask = (0.78, 0.76, 0.72); eye = (2.2, 3.0, 4.5); hand = (0.45, 0.45, 0.47)
    tw = torso_weights(hz, 0.05, 0.25, 0.3, 0.5)
    # the robe trailing into tatters (no legs: the thighs carry the tatters so they sway), matte wool
    M.limb("spine", (0, 0.02, hz + 0.55), (0, 0.04, hz - 0.1), 0.24, 0.3, wool, w=tw, seg=8, noise_amt=0.3)
    for k in range(8):
        a = TAU * k / 8 + 0.2
        top = (0.28 * math.cos(a), 0.04 + 0.26 * math.sin(a), hz - 0.05)
        bot = (0.36 * math.cos(a), 0.06 + 0.34 * math.sin(a), 0.04 + 0.08 * ((k * 5) % 3))
        M.limb("hips", top, bot, 0.1, 0.02, wool2 if k % 2 else wool, seg=4, noise_amt=0.35,
               w={"thighL": 0.7, "hips": 0.3} if top[0] > 0 else {"thighR": 0.7, "hips": 0.3})
    M.ell("chest", (0, 0.03, hz + 0.45), (0.3, 0.2, 0.22), wool, seg=(8, 5), noise_amt=0.3)
    # the hood with a pale mask inside it and two cold points of light
    M.ell("head", (0, 0.02, hz + 0.72), (0.2, 0.22, 0.24), wool, seg=(9, 6), noise_amt=0.3)
    M.limb("head", (0, 0.06, hz + 0.9), (0, 0.22, hz + 1.0), 0.09, 0.01, wool, seg=5)               # the hood's point
    M.ell("head", (0, -0.13, hz + 0.7), (0.1, 0.06, 0.13), mask, seg=(7, 5))
    for s in (1, -1):
        M.ell("head", (s * 0.04, -0.185, hz + 0.73), (0.022, 0.012, 0.015), eye, seg=(4, 3), emissive=True)
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 0.6 - 0.08
        M.limb("uarm" + side, (s * 0.24, 0, sz), (s * 0.25, 0, sz - 0.36), 0.08, 0.1, wool, seg=6, noise_amt=0.3)   # long sleeves
        M.limb("farm" + side, (s * 0.25, 0, sz - 0.36), (s * 0.25, 0, sz - 0.68), 0.1, 0.13, wool2, seg=6, noise_amt=0.3)
        M.limb("farm" + side, (s * 0.25, 0, sz - 0.66), (s * 0.25, -0.02, sz - 0.86), 0.03, 0.02, hand, seg=5)
        for k in (-1, 0, 1):
            M.spike("farm" + side, (s * 0.25 + k * 0.02, -0.02, sz - 0.85), (s * 0.25 + k * 0.03, -0.06, sz - 1.0), 0.012, hand, seg=4)
    return M

def penitent_poses():
    walk = []
    for f in range(WALK):
        ph = TAU * f / WALK
        # it glides a hand's width off the floor, swaying; the tatters swing with the "legs"
        p = {"hips": (0.05 * math.sin(ph), 0, 0.12 * math.sin(ph), 0, 0.18 + 0.05 * math.sin(ph * 2), 0),
             "spine": (0.15, 0, -0.08 * math.sin(ph)), "chest": (0.1, 0, 0), "head": (-0.1, 0, 0.1 * math.sin(ph)),
             "thighL": (0.3 + 0.15 * math.sin(ph), 0, 0), "thighR": (0.3 - 0.15 * math.sin(ph), 0, 0),
             "uarmL": (-0.15 + 0.1 * math.sin(ph), 0, 0.15), "uarmR": (-0.15 - 0.1 * math.sin(ph), 0, -0.15),
             "farmL": (-0.3, 0, 0), "farmR": (-0.3, 0, 0)}
        walk.append(p)
    attack = []
    # the lunge: drawn back and risen with the hands up (the tell), rushing forward with the hands out, settling
    for rise, lean, arm in ((0.6, -0.35, -2.4), (0.2, 0.7, -1.6), (0.3, 0.3, -0.8)):
        p = {"hips": (lean * 0.3, 0, 0, 0, rise, 0), "spine": (lean * 0.4, 0, 0), "chest": (lean * 0.3, 0, 0), "head": (-lean * 0.5, 0, 0),
             "thighL": (0.6 * max(lean, 0) + 0.2, 0, 0), "thighR": (0.6 * max(lean, 0) + 0.2, 0, 0),
             "uarmL": (arm, 0, 0.25), "uarmR": (arm, 0, -0.25), "farmL": (-0.2, 0, 0), "farmR": (-0.2, 0, 0)}
        attack.append(p)
    return [walk, attack]

# ───────────────────────── level 6 boss: Seraphel ─────────────────────

def make_seraphel():
    M = Model()
    hz = 1.9
    humanoid_rig(M, hz, 0.9, 0.9, 1.45, 0.42, 0.75, 0.8, 0.62, 0.25)
    for s, side in ((1, "L"), (-1, "R")):
        M.bone("wing" + side, "chest", (s * 0.4, 0.35, hz + 1.25), (s * 1.6, 0.6, hz + 2.2))
    vest = (0.05, 0.04, 0.05); vest2 = (0.16, 0.03, 0.04); gold = (0.62, 0.48, 0.2); mask = (0.85, 0.82, 0.76)
    feather = (0.08, 0.07, 0.07); glow = (5.0, 0.8, 0.4); iron = (0.18, 0.17, 0.16); ember = (4.5, 1.4, 0.3)
    tw = torso_weights(hz, 0.1, 0.6, 0.7, 1.2)
    M.limb("hips", (0, 0.05, hz + 0.3), (0, 0.1, 0.03), 0.5, 0.82, vest, seg=10, noise_amt=0.25,
           w=lambda p: {"hips": 0.4 + 0.6 * smoothstep(0.0, hz, p.z), "thighL": 0.6 * (1 - smoothstep(0.0, hz, p.z)) * (1 if p.x > 0 else 0.2), "thighR": 0.6 * (1 - smoothstep(0.0, hz, p.z)) * (1 if p.x < 0 else 0.2)})
    M.box("hips", (0, -0.6, hz - 0.6), (0.4, 0.05, 1.9), vest2, bevel=0.01)                              # a crimson stole
    M.box("hips", (0, -0.62, hz - 0.6), (0.06, 0.02, 1.8), gold, bevel=0.0)
    M.ell("spine", (0, 0, hz + 0.7), (0.5, 0.36, 0.55), vest, w=tw, seg=(10, 7), noise_amt=0.2)
    M.ell("chest", (0, 0.05, hz + 1.15), (0.68, 0.42, 0.42), vest, seg=(10, 7), noise_amt=0.25)
    M.box("chest", (0, -0.36, hz + 1.1), (0.5, 0.06, 0.6), vest2, bevel=0.02)
    # the mask with its sewn mouth, and the cracked mitre
    M.ell("head", (0, -0.08, hz + 1.68), (0.24, 0.24, 0.3), mask, seg=(10, 7))
    M.box("head", (0, -0.3, hz + 1.55), (0.14, 0.04, 0.06), glow, bevel=0.0)                              # the mouth glow
    for s in (1, -1):
        M.ell("head", (s * 0.09, -0.29, hz + 1.74), (0.04, 0.02, 0.025), (0.02, 0.02, 0.02), seg=(5, 3))
    M.limb("head", (0, 0.0, hz + 1.85), (0, 0.05, hz + 2.55), 0.26, 0.04, gold, seg=8, noise_amt=0.15)
    M.limb("head", (0.0, -0.01, hz + 1.9), (0, 0.04, hz + 2.4), 0.2, 0.02, vest2, seg=8)
    for s, side in ((1, "L"), (-1, "R")):                                                                  # the charred wings
        root = (s * 0.4, 0.35, hz + 1.25)
        for k in range(6):
            a = 0.3 + 0.22 * k
            tip = (s * (0.6 + 1.9 * math.sin(a)), 0.55 + 0.2 * k, hz + 1.25 + 1.9 * math.cos(a) - 0.4)
            M.limb("wing" + side, root, tip, 0.12, 0.02, feather, seg=4, noise_amt=0.3)
        M.limb("wing" + side, root, (s * 1.6, 0.6, hz + 2.2), 0.09, 0.05, iron, seg=5)
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 1.45 - 0.1
        M.ell("chest", (s * 0.62, 0, sz + 0.06), (0.24, 0.22, 0.2), vest, seg=(8, 5))
        M.limb("uarm" + side, (s * 0.62, 0, sz), (s * 0.62, 0, sz - 0.75), 0.17, 0.2, vest, seg=7, noise_amt=0.25)
        M.limb("farm" + side, (s * 0.62, 0, sz - 0.75), (s * 0.62, 0, sz - 1.4), 0.2, 0.26, vest2, seg=7, noise_amt=0.25)
        M.limb("farm" + side, (s * 0.62, 0, sz - 1.38), (s * 0.62, -0.03, sz - 1.62), 0.05, 0.04, mask, seg=5)
    # the censer on a chain from the right hand
    sz = hz + 1.45 - 0.1
    M.limb("farmR", (-0.62, -0.03, sz - 1.6), (-0.62, -0.03, sz - 2.3), 0.015, 0.015, iron, seg=4)
    M.ell("farmR", (-0.62, -0.03, sz - 2.45), (0.2, 0.2, 0.22), iron, seg=(8, 5))
    M.ell("farmR", (-0.62, -0.03, sz - 2.45), (0.12, 0.12, 0.24), ember, seg=(6, 4), emissive=True)
    return M

def seraphel_poses():
    walk = []
    for f in range(BOSS_WALK):
        ph = TAU * f / BOSS_WALK
        p = walk_pose(f, BOSS_WALK, A_leg=0.2, K=0.25, arm=0.08, bounce=0.02, spine_lean=0.06, twist=0.08, arm_bend=0.25)
        p["wingL"] = (0.05 * math.sin(ph), 0, 0.1); p["wingR"] = (0.05 * math.sin(ph), 0, -0.1)
        p["uarmR"] = (-0.25 + 0.15 * math.sin(ph), 0, 0); p["farmR"] = (-0.2, 0, 0)            # the censer swinging
        walk.append(p)
    attack = []
    # the censer swing: the arm whirled back and up (the tell), swept round in front, following through
    for twist, arm in ((0.8, (-1.6, 0, -1.3)), (-0.9, (-1.5, 0, 1.2)), (-0.4, (-0.8, 0, 0.6))):
        p = stance(0.15, 0.15)
        p["spine"] = (0.1, 0, twist * 0.5); p["chest"] = (0.1, 0, twist * 0.5)
        p["uarmR"] = arm; p["farmR"] = (-0.3, 0, 0)
        p["wingL"] = (0, 0, 0.2); p["wingR"] = (0, 0, -0.2)
        attack.append(p)
    special = []
    # the choir beam: wings spread wide and the head tipped back with the mouth burning (the tell), the head
    # snapped forward as the beam sweeps, wings folding
    for wing, head in ((-0.9, -0.5), (-0.6, 0.35), (-0.2, 0.1)):
        p = stance(0.1, 0.15)
        p["wingL"] = (0, 0, wing); p["wingR"] = (0, 0, -wing)
        p["head"] = (head, 0, 0); p["chest"] = (-head * 0.3, 0, 0)
        p["uarmL"] = (-0.6, 0, 0.9); p["uarmR"] = (-0.6, 0, -0.9)
        special.append(p)
    summon = []
    # the shroud step: wings folded round the body (gather), then flung open (release)
    for wing, crouch in ((0.9, 0.35), (-0.7, 0.05)):
        p = stance(crouch, 0.15)
        p["wingL"] = (0, 0, wing); p["wingR"] = (0, 0, -wing)
        p["uarmL"] = (-0.4, 0, -0.3); p["uarmR"] = (-0.4, 0, 0.3)
        summon.append(p)
    return [walk, attack, special, summon]

# ───────────────────────── level 7: the gargoyle ──────────────────────

def make_gargoyle():
    M = Model()
    hz = 0.55
    humanoid_rig(M, hz, 0.28, 0.32, 0.45, 0.2, 0.3, 0.36, 0.2, 0.12)
    for s, side in ((1, "L"), (-1, "R")):
        M.bone("wing" + side, "chest", (s * 0.12, 0.15, hz + 0.4), (s * 0.7, 0.3, hz + 0.8))
    stone = (0.1, 0.1, 0.11); stone2 = (0.06, 0.06, 0.07); crack = (1.8, 2.2, 5.5); horn = (0.35, 0.33, 0.32)  # near-black stone: it stands out on the grey glass
    tw = torso_weights(hz, 0.02, 0.2, 0.22, 0.38)
    M.ell("spine", (0, 0.02, hz + 0.2), (0.17, 0.14, 0.24), stone, w=tw, seg=(8, 6), noise_amt=0.35)
    M.ell("chest", (0, -0.02, hz + 0.36), (0.23, 0.17, 0.16), stone, seg=(8, 5), noise_amt=0.35)
    M.ell("chest", (0, -0.17, hz + 0.33), (0.06, 0.02, 0.09), crack, seg=(5, 3), emissive=True)          # a glowing crack
    M.ell("hips", (0, 0, hz), (0.15, 0.13, 0.11), stone2, seg=(7, 5), noise_amt=0.35)
    M.ell("head", (0, -0.1, hz + 0.48), (0.13, 0.15, 0.12), stone, seg=(8, 5), noise_amt=0.3)
    M.box("head", (0, -0.24, hz + 0.43), (0.12, 0.08, 0.06), stone2, bevel=0.015)
    for s in (1, -1):
        M.ell("head", (s * 0.05, -0.23, hz + 0.51), (0.025, 0.015, 0.02), crack, seg=(4, 3), emissive=True)
        M.spike("head", (s * 0.07, -0.02, hz + 0.56), (s * 0.18, 0.18, hz + 0.75), 0.04, horn, seg=5)
    for s, side in ((1, "L"), (-1, "R")):                                                                # folded stone bat wings
        r0 = (s * 0.12, 0.15, hz + 0.4)
        for k in range(3):
            tip = (s * (0.45 + 0.15 * k), 0.25 + 0.12 * k, hz + 0.75 - 0.25 * k)
            M.limb("wing" + side, r0, tip, 0.06, 0.012, stone2, seg=4, noise_amt=0.3)
        M.ell("wing" + side, (s * 0.38, 0.28, hz + 0.55), (0.24, 0.05, 0.22), stone2, seg=(6, 4), noise_amt=0.3)
        sz = hz + 0.45 - 0.08
        M.limb("uarm" + side, (s * 0.2, 0, sz), (s * 0.2, 0, sz - 0.3), 0.05, 0.045, stone, seg=5)
        M.limb("farm" + side, (s * 0.2, 0, sz - 0.3), (s * 0.2, 0, sz - 0.64), 0.045, 0.04, stone, seg=5)
        for c in (-1, 0, 1):
            M.spike("farm" + side, (s * 0.2 + c * 0.025, 0, sz - 0.64), (s * 0.2 + c * 0.04, -0.06, sz - 0.78), 0.018, horn, seg=4)
        M.limb("thigh" + side, (s * 0.12, 0, hz), (s * 0.12, -0.12, hz - 0.28), 0.07, 0.055, stone, seg=5)
        M.limb("shin" + side, (s * 0.12, -0.12, hz - 0.28), (s * 0.12, 0.05, 0.05), 0.05, 0.035, stone, seg=5)  # digitigrade
        M.box("foot" + side, (s * 0.12, -0.04, 0.03), (0.09, 0.18, 0.05), stone2, bevel=0.012)
    return M

def gargoyle_poses():
    walk = []
    # a hop: crouch, spring (wings half open), glide, land, gathering again
    for crouch, rise, wing, lean in ((0.5, 0.0, 0.0, 0.3), (0.0, 0.35, -0.6, 0.45), (-0.2, 0.7, -0.9, 0.55), (-0.1, 0.6, -0.8, 0.5), (0.1, 0.25, -0.4, 0.4), (0.4, 0.02, -0.1, 0.3)):
        p = stance(max(crouch, 0) * 0.9, 0.1)
        p["hips"] = (lean * 0.3, 0, 0, 0, rise - max(crouch, 0) * 0.15, 0)
        p["spine"] = (lean * 0.4, 0, 0); p["chest"] = (lean * 0.3, 0, 0); p["head"] = (-lean * 0.6, 0, 0)
        if crouch < 0: p["thighL"] = (0.6, 0, 0.1); p["thighR"] = (0.6, 0, -0.1); p["shinL"] = (0.3, 0, 0); p["shinR"] = (0.3, 0, 0)
        p["wingL"] = (0, 0, wing); p["wingR"] = (0, 0, -wing)
        p["uarmL"] = (-0.6, 0, 0.3); p["uarmR"] = (-0.6, 0, -0.3)
        walk.append(p)
    attack = []
    # the dive: at the top of a hop, wings flared wide (the tell), dropping claws first, crouched on landing
    for rise, wing, lean, arm in ((0.8, -1.3, -0.2, -2.0), (0.25, -0.3, 0.9, -1.4), (0.0, -0.1, 0.4, -0.6)):
        p = stance(0.3 if rise < 0.1 else 0.0, 0.1)
        p["hips"] = (lean * 0.3, 0, 0, 0, rise, 0)
        p["spine"] = (lean * 0.4, 0, 0); p["head"] = (-lean * 0.5, 0, 0)
        p["wingL"] = (0, 0, wing); p["wingR"] = (0, 0, -wing)
        p["uarmL"] = (arm, 0, 0.3); p["uarmR"] = (arm, 0, -0.3)
        attack.append(p)
    return [walk, attack]

# ───────────────────────── level 7 boss: Khar-Vaal ────────────────────

def make_kharvaal():
    M = Model()
    hz = 1.9
    humanoid_rig(M, hz, 0.95, 0.95, 1.5, 0.45, 0.9, 1.0, 1.15, 0.5)
    M.bone("crown", "head", (0, 0, hz + 2.1), (0, 0, hz + 2.5))
    glass = (0.05, 0.05, 0.08); glass2 = (0.12, 0.11, 0.17); basalt = (0.18, 0.17, 0.17); void = (2.2, 1.0, 5.0); shard = (0.6, 0.55, 0.85)
    tw = torso_weights(hz, 0.1, 0.6, 0.7, 1.2)
    M.ell("spine", (0, 0, hz + 0.7), (0.7, 0.55, 0.7), glass, w=tw, seg=(9, 6), noise_amt=0.3)
    for k in range(6):                                                                                    # plates of black glass
        a = TAU * k / 6
        M.box("chest", (0.55 * math.cos(a), 0.4 * math.sin(a), hz + 1.2), (0.6, 0.5, 0.7), glass2 if k % 2 else glass, rot=(0.2, 0.1, a), bevel=0.03, noise_amt=0.2)
    M.ell("chest", (0, -0.45, hz + 1.15), (0.3, 0.08, 0.5), void, seg=(7, 5), emissive=True)                # void-light in the cracks
    M.ell("hips", (0, 0, hz), (0.6, 0.45, 0.35), basalt, seg=(8, 5), noise_amt=0.3)
    M.box("head", (0, -0.1, hz + 1.75), (0.5, 0.55, 0.55), glass2, rot=(0, 0, 0.785), bevel=0.04)
    for s in (1, -1):
        M.ell("head", (s * 0.12, -0.38, hz + 1.8), (0.06, 0.03, 0.04), void, seg=(5, 3), emissive=True)
    for k in range(7):                                                                                    # the floating crown of shards
        a = TAU * k / 7
        b = (0.55 * math.cos(a), 0.55 * math.sin(a), hz + 2.25)
        M.spike("crown", b, (0.75 * math.cos(a), 0.75 * math.sin(a), hz + 2.95 + 0.15 * (k % 2)), 0.1, shard, seg=4)
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 1.5 - 0.1
        M.box("chest", (s * 1.15, 0, sz + 0.1), (0.7, 0.7, 0.55), basalt, bevel=0.06, noise_amt=0.3)
        M.limb("uarm" + side, (s * 1.15, 0, sz), (s * 1.15, 0, sz - 0.9), 0.3, 0.26, glass, seg=6, noise_amt=0.2)
        M.limb("thigh" + side, (s * 0.5, 0, hz), (s * 0.5, 0, hz - 0.95), 0.34, 0.28, glass, seg=6, noise_amt=0.2)
        M.limb("shin" + side, (s * 0.5, 0, hz - 0.95), (s * 0.5, 0, hz - 1.8), 0.3, 0.36, basalt, seg=6, noise_amt=0.3)
        M.box("foot" + side, (s * 0.5, -0.2, 0.12), (0.55, 0.85, 0.24), basalt, bevel=0.06)
    sz = hz + 1.5 - 0.1
    # the left arm a fist of basalt, the right a huge crystal blade
    M.limb("farmL", (1.15, 0, sz - 0.9), (1.15, 0, sz - 1.8), 0.28, 0.32, basalt, seg=6, noise_amt=0.3)
    M.ell("farmL", (1.15, -0.05, sz - 2.0), (0.48, 0.45, 0.42), basalt, seg=(8, 6), noise_amt=0.35)
    M.limb("farmR", (-1.15, 0, sz - 0.9), (-1.15, 0, sz - 1.5), 0.28, 0.24, glass, seg=6)
    M.spike("farmR", (-1.15, -0.05, sz - 1.4), (-1.15, -0.25, sz - 3.6), 0.32, shard, seg=4)
    M.spike("farmR", (-1.15, -0.05, sz - 1.6), (-1.15, -0.15, sz - 3.0), 0.12, void, seg=4)
    return M

def kharvaal_poses():
    walk = []
    for f in range(BOSS_WALK):
        ph = TAU * f / BOSS_WALK
        p = walk_pose(f, BOSS_WALK, A_leg=0.35, K=0.45, arm=0.2, bounce=0.08, spine_lean=0.1, twist=0.18, arm_bend=0.25)
        p["uarmR"] = (-0.35, 0, -0.1); p["farmR"] = (-0.6, 0, 0)       # the blade held point-forward, low
        p["crown"] = (0, ph, 0)
        walk.append(p)
    attack = []
    # the blade cleave: blade raised across the body to the left (the tell), cleaved down to the right, recovering
    for twist, arm in ((0.7, (-2.4, 0, 0.9)), (-0.8, (-1.0, 0, -0.8)), (-0.3, (-0.6, 0, -0.3))):
        p = stance(0.25, 0.2)
        p["spine"] = (0.15, 0, twist * 0.5); p["chest"] = (0.1, 0, twist * 0.5)
        p["uarmR"] = arm; p["farmR"] = (-0.4, 0, 0)
        p["uarmL"] = (-0.4, 0, 0.4); p["farmL"] = (-0.3, 0, 0)
        attack.append(p)
    special = []
    # the shard nova: the crown spun up high (the tell), arms flung wide as it fires, settling
    for crown_up, arm in ((0.35, -0.6), (0.1, -1.4), (0.05, -0.5)):
        p = stance(0.15, 0.2)
        p["crown"] = (0, 1.2, 0, 0, crown_up, 0)
        p["uarmL"] = (arm, 0, 1.0); p["uarmR"] = (arm, 0, -1.0)
        p["head"] = (-0.25, 0, 0)
        special.append(p)
    summon = []
    # the fracture stamp: the right leg raised high (gather), stamped down (release)
    for thigh, crouch in ((-1.2, 0.0), (0.1, 0.35)):
        p = stance(crouch, 0.25)
        p["thighR"] = (thigh, 0, -0.15); p["shinR"] = (0.9 if thigh < -0.5 else 0.4, 0, 0)
        p["uarmL"] = (-0.5, 0, 0.6); p["uarmR"] = (-0.5, 0, -0.6)
        summon.append(p)
    return [walk, attack, special, summon]

# ───────────────────────── level 8: the hellhound ─────────────────────

def make_hellhound():
    M = Model()
    bz = 0.62
    M.bone("body", None, (0, 0.05, bz - 0.1), (0, 0.05, bz + 0.1))
    M.bone("chest", "body", (0, -0.25, bz), (0, -0.25, bz + 0.2))
    M.bone("head", "chest", (0, -0.5, bz + 0.15), (0, -0.5, bz + 0.35))
    M.bone("tail", "body", (0, 0.4, bz + 0.05), (0, 0.4, bz - 0.15))
    for s, side in ((1, "L"), (-1, "R")):
        M.bone("fleg" + side, "chest", (s * 0.15, -0.3, bz), (s * 0.15, -0.3, bz - 0.3))
        M.bone("fpaw" + side, "fleg" + side, (s * 0.15, -0.3, bz - 0.3), (s * 0.15, -0.3, 0.02))
        M.bone("hleg" + side, "body", (s * 0.15, 0.3, bz), (s * 0.15, 0.3, bz - 0.3))
        M.bone("hpaw" + side, "hleg" + side, (s * 0.15, 0.3, bz - 0.3), (s * 0.15, 0.3, 0.02))
    hide = (0.1, 0.05, 0.05); hide2 = (0.2, 0.08, 0.06); horn = (0.35, 0.3, 0.26); glow = (5.0, 1.6, 0.25); bone_c = (0.7, 0.62, 0.5)
    M.ell("body", (0, 0.12, bz + 0.02), (0.18, 0.4, 0.17), hide, seg=(8, 6), noise_amt=0.3)
    M.ell("chest", (0, -0.25, bz + 0.08), (0.22, 0.24, 0.22), hide2, seg=(8, 6), noise_amt=0.3)
    for k in range(6):                                                                                   # the ridge of horn spines
        y = -0.32 + 0.13 * k
        M.spike("body" if y > -0.1 else "chest", (0, y, bz + 0.2), (0, y + 0.12, bz + 0.38 - 0.03 * k), 0.04, horn, seg=4)
    M.ell("head", (0, -0.58, bz + 0.25), (0.13, 0.16, 0.12), hide2, seg=(8, 5), noise_amt=0.25)
    M.box("head", (0, -0.75, bz + 0.2), (0.12, 0.2, 0.09), bone_c, bevel=0.02)                           # skull-like muzzle
    M.box("head", (0, -0.74, bz + 0.15), (0.1, 0.18, 0.025), glow, bevel=0.0)                             # the glowing split
    for s in (1, -1):
        M.ell("head", (s * 0.06, -0.68, bz + 0.3), (0.025, 0.02, 0.02), glow, seg=(4, 3), emissive=True)
        M.spike("head", (s * 0.07, -0.52, bz + 0.33), (s * 0.15, -0.38, bz + 0.5), 0.04, horn, seg=4)
    M.ell("chest", (0, -0.38, bz + 0.0), (0.09, 0.06, 0.1), glow, seg=(5, 4), emissive=True)             # the glowing throat
    M.limb("tail", (0, 0.45, bz + 0.05), (0, 0.85, bz + 0.2), 0.04, 0.015, hide, seg=5)
    M.ell("tail", (0, 0.88, bz + 0.22), (0.05, 0.08, 0.05), glow, seg=(5, 3), emissive=True)
    for s, side in ((1, "L"), (-1, "R")):
        M.limb("fleg" + side, (s * 0.15, -0.3, bz), (s * 0.15, -0.3, bz - 0.3), 0.07, 0.05, hide2, seg=5)
        M.limb("fpaw" + side, (s * 0.15, -0.3, bz - 0.3), (s * 0.15, -0.32, 0.03), 0.045, 0.035, hide, seg=5)
        M.box("fpaw" + side, (s * 0.15, -0.36, 0.025), (0.08, 0.12, 0.05), horn, bevel=0.01)
        M.limb("hleg" + side, (s * 0.15, 0.3, bz), (s * 0.15, 0.4, bz - 0.3), 0.08, 0.05, hide2, seg=5)
        M.limb("hpaw" + side, (s * 0.15, 0.4, bz - 0.3), (s * 0.15, 0.32, 0.03), 0.045, 0.035, hide, seg=5)
        M.box("hpaw" + side, (s * 0.15, 0.28, 0.025), (0.08, 0.12, 0.05), horn, bevel=0.01)
    return M

def hellhound_poses():
    walk = []
    for f in range(WALK):                                                                                # a loping gallop
        ph = TAU * f / WALK
        a, b = math.sin(ph), math.sin(ph + 0.6)
        p = {"body": (0.08 * math.sin(ph * 1), 0, 0, 0, 0.06 * abs(math.cos(ph)), 0), "chest": (-0.12 * a, 0, 0), "head": (0.1 * a, 0, 0),
             "tail": (-0.3 + 0.2 * a, 0, 0)}
        for side in ("L", "R"):
            o = 0.3 if side == "L" else 0.0
            p["fleg" + side] = (-0.7 * math.sin(ph + o), 0, 0); p["fpaw" + side] = (0.5 * max(0, math.sin(ph + o + 1.2)), 0, 0)
            p["hleg" + side] = (0.7 * math.sin(ph + o + 0.6), 0, 0); p["hpaw" + side] = (-0.5 * max(0, math.sin(ph + o + 1.8)), 0, 0)
        walk.append(p)
    attack = []
    # the pounce: crouched with the throat flaring (the tell), stretched out in the leap, landing
    for crouch, stretch, head in ((-0.2, 0.0, -0.25), (0.25, 1.0, 0.3), (0.0, 0.3, 0.1)):
        p = {"body": (-0.25 * stretch + 0.15 * (crouch < 0), 0, 0, 0, crouch, 0), "chest": (-0.1, 0, 0), "head": (head, 0, 0), "tail": (0.2 * stretch, 0, 0)}
        for side in ("L", "R"):
            p["fleg" + side] = (-1.2 * stretch + 0.5 * (crouch < 0), 0, 0); p["fpaw" + side] = (0.3 * (crouch < 0), 0, 0)
            p["hleg" + side] = (1.0 * stretch - 0.5 * (crouch < 0), 0, 0); p["hpaw" + side] = (-0.3 * (crouch < 0), 0, 0)
        attack.append(p)
    return [walk, attack]

# ───────────────────────── level 8 boss: the Unmaker ──────────────────

def make_unmaker():
    M = Model()
    hz = 2.2
    humanoid_rig(M, hz, 1.05, 1.05, 1.75, 0.5, 1.0, 1.1, 1.2, 0.55)
    sz = hz + 1.75 - 0.1
    for s, side in ((1, "L"), (-1, "R")):                                                                 # the second pair of arms
        M.bone("xarm" + side, "chest", (s * 0.95, -0.1, sz - 0.6), (s * 1.7, -0.3, sz - 1.3))
        M.bone("wing" + side, "chest", (s * 0.5, 0.45, sz), (s * 2.2, 0.9, sz + 1.0))
    hide = (0.16, 0.04, 0.03); hide2 = (0.28, 0.07, 0.04); horn = (0.12, 0.1, 0.09); bone_c = (0.62, 0.55, 0.45)
    fire = (6.0, 2.2, 0.35); ember = (4.5, 1.0, 0.15); iron = (0.15, 0.14, 0.14); smoke = (0.06, 0.05, 0.05)
    tw = torso_weights(hz, 0.1, 0.7, 0.8, 1.4)
    M.ell("spine", (0, 0, hz + 0.8), (0.75, 0.55, 0.8), hide, w=tw, seg=(10, 7), noise_amt=0.25)
    M.ell("chest", (0, 0.05, hz + 1.45), (1.05, 0.65, 0.55), hide2, seg=(10, 7), noise_amt=0.25)
    # the burning ribcage
    M.ell("chest", (0, -0.42, hz + 1.25), (0.38, 0.14, 0.42), fire, seg=(8, 5), emissive=True)
    for k in range(5):
        M.ell("chest", (0, -0.4, hz + 0.95 + 0.14 * k), (0.48 - abs(k - 2) * 0.05, 0.16, 0.03), bone_c, seg=(9, 3))
    M.ell("hips", (0, 0, hz), (0.65, 0.5, 0.38), hide, seg=(9, 6), noise_amt=0.25)
    for k in range(6):                                                                                     # a skirt of chains
        a = math.pi + TAU * k / 12 - math.pi / 4
        M.limb("hips", (0.55 * math.cos(a), 0.4 * math.sin(a), hz - 0.1), (0.62 * math.cos(a), 0.45 * math.sin(a), hz - 0.95), 0.03, 0.03, iron, seg=4)
    # the head and its cracked crown of horns
    M.ell("head", (0, -0.15, hz + 2.05), (0.32, 0.36, 0.34), hide2, seg=(9, 6), noise_amt=0.25)
    M.box("head", (0, -0.45, hz + 1.92), (0.36, 0.14, 0.2), bone_c, bevel=0.03)
    for s in (1, -1):
        M.ell("head", (s * 0.13, -0.45, hz + 2.12), (0.06, 0.03, 0.04), fire, seg=(5, 3), emissive=True)
        M.spike("head", (s * 0.22, -0.05, hz + 2.3), (s * 0.95, 0.1, hz + 3.15), 0.16, horn, seg=6)
        M.spike("head", (s * 0.12, 0.05, hz + 2.35), (s * 0.4, 0.35, hz + 3.0), 0.1, horn, seg=5)
    for s, side in ((1, "L"), (-1, "R")):
        root = (s * 0.5, 0.45, sz)                                                                          # wings of smoke and chains
        for k in range(4):
            a = 0.4 + 0.3 * k
            tip = (s * (0.7 + 2.0 * math.sin(a)), 0.7 + 0.2 * k, sz + 2.0 * math.cos(a) - 0.6)
            M.limb("wing" + side, root, tip, 0.14, 0.03, smoke, seg=4, noise_amt=0.35)
            M.limb("wing" + side, tip, (tip[0], tip[1], tip[2] - 0.9), 0.02, 0.02, iron, seg=3)
        M.ell("chest", (s * 1.2, 0, sz + 0.1), (0.42, 0.4, 0.35), hide2, seg=(8, 6), noise_amt=0.25)
        M.spike("chest", (s * 1.25, 0, sz + 0.35), (s * 1.6, 0.1, sz + 0.95), 0.12, horn, seg=5)
        M.limb("uarm" + side, (s * 1.2, 0, sz), (s * 1.2, 0, sz - 1.0), 0.3, 0.26, hide, seg=7)
        M.limb("farm" + side, (s * 1.2, 0, sz - 1.0), (s * 1.2, 0, sz - 2.1), 0.26, 0.3, hide2, seg=7)
        M.limb("xarm" + side, (s * 0.95, -0.1, sz - 0.6), (s * 1.75, -0.35, sz - 1.4), 0.17, 0.14, hide, seg=6)
        M.limb("xarm" + side, (s * 1.75, -0.35, sz - 1.4), (s * 1.9, -0.75, sz - 1.9), 0.14, 0.1, hide2, seg=6)
        for c in (-1, 0, 1):
            M.spike("xarm" + side, (s * 1.9 + c * 0.05, -0.75, sz - 1.9), (s * 1.95 + c * 0.08, -0.95, sz - 2.15), 0.04, horn, seg=4)
        M.limb("thigh" + side, (s * 0.55, 0, hz), (s * 0.55, 0.1, hz - 1.05), 0.36, 0.28, hide, seg=7)
        M.limb("shin" + side, (s * 0.55, 0.1, hz - 1.05), (s * 0.55, -0.05, 0.25), 0.28, 0.2, hide2, seg=7)
        M.box("foot" + side, (s * 0.55, -0.08, 0.13), (0.38, 0.45, 0.26), horn, bevel=0.06)               # hooves
    # the great split blade, held in both main hands
    M.limb("farmR", (-1.2, -0.05, sz - 2.2), (-1.2, -0.05, sz - 1.6), 0.08, 0.08, iron, seg=6)
    for d in (-0.14, 0.14):
        M.box("farmR", (-1.2, -0.05 + d, sz - 2.2 - 1.35), (0.06, 0.18, 2.5), (0.26, 0.24, 0.24), bevel=0.01, noise_amt=0.2)
    M.box("farmR", (-1.2, -0.05, sz - 2.2 - 1.35), (0.03, 0.1, 2.4), ember, bevel=0.0)
    return M

def unmaker_poses():
    walk = []
    for f in range(BOSS_WALK):
        ph = TAU * f / BOSS_WALK
        p = walk_pose(f, BOSS_WALK, A_leg=0.35, K=0.5, arm=0.15, bounce=0.1, spine_lean=0.15, twist=0.2, arm_bend=0.25)
        p["uarmR"] = (0.2, 0, 0.1); p["farmR"] = (0.0, 0, 0)                    # the blade dragged point-down
        p["wingL"] = (0.1 * math.sin(ph), 0, 0.1); p["wingR"] = (0.1 * math.sin(ph), 0, -0.1)
        p["xarmL"] = (-0.2 * math.sin(ph), 0, 0); p["xarmR"] = (0.2 * math.sin(ph), 0, 0)
        walk.append(p)
    attack = []
    # the riven blade: raised high over the head point-up (the tell), brought down along a line in front, recovering
    for arm, lean, crouch in ((-3.0, -0.2, 0.1), (-1.25, 0.55, 0.45), (-1.6, 0.3, 0.3)):
        p = stance(crouch, 0.2)
        p["spine"] = (lean * 0.4, 0, 0); p["chest"] = (lean * 0.3, 0, 0); p["head"] = (-lean * 0.5, 0, 0)
        p["uarmR"] = (arm, 0, -0.2); p["farmR"] = (-0.2, 0, 0)
        p["uarmL"] = (arm, 0, 0.6); p["farmL"] = (-0.6, 0, 0)
        attack.append(p)
    special = []
    # the hellstorm: all four arms raised and the ribcage thrust forward (the tell), thrown down, settling
    for arm, chest in ((-2.6, -0.35), (-0.8, 0.3), (-0.4, 0.1)):
        p = stance(0.15, 0.25)
        p["chest"] = (chest, 0, 0); p["head"] = (-chest, 0, 0)
        p["uarmL"] = (arm, 0, 0.5); p["uarmR"] = (arm * 0.7, 0, -0.5)
        p["xarmL"] = (arm * 0.6, 0, 0.6); p["xarmR"] = (arm * 0.6, 0, -0.6)
        p["wingL"] = (0, 0, -0.5 if arm < -2 else 0); p["wingR"] = (0, 0, 0.5 if arm < -2 else 0)
        special.append(p)
    summon = []
    # the legion roar: head thrown back with the wings spread (gather), then lurched forward (release)
    for head, wing in ((-0.7, -0.8), (0.3, 0.1)):
        p = stance(0.2, 0.25)
        p["head"] = (head, 0, 0); p["chest"] = (head * 0.3, 0, 0)
        p["wingL"] = (0, 0, wing); p["wingR"] = (0, 0, -wing)
        p["xarmL"] = (-0.8, 0, 0.8); p["xarmR"] = (-0.8, 0, -0.8)
        summon.append(p)
    return [walk, attack, special, summon]

CHARS = {
    "sluice_skitter": (make_skitter, skitter_poses),
    "sluice_boss": (make_vhessa, vhessa_poses),
    "ossuary_bonewalker": (make_bonewalker, bonewalker_poses),
    "ossuary_boss": (make_ossivar, ossivar_poses),
    "foundry_slagborn": (make_slagborn, slagborn_poses),
    "foundry_boss": (make_anvilgrave, anvilgrave_poses),
    "grotto_sporeling": (make_sporeling, sporeling_poses),
    "grotto_boss": (make_mycel, mycel_poses),
    "cathedral_penitent": (make_penitent, penitent_poses),
    "cathedral_boss": (make_seraphel, seraphel_poses),
    "rift_gargoyle": (make_gargoyle, gargoyle_poses),
    "rift_boss": (make_kharvaal, kharvaal_poses),
    "core_hellhound": (make_hellhound, hellhound_poses),
    "core_boss": (make_unmaker, unmaker_poses),
}

# ───────────────────────── contact sheets ─────────────────────────────

def sheet(name, ob, ao, poses):
    """Every frame of every clip from the game's camera angle (62 degrees down, from the south-east a little)
    and from the side, Cycles on the CPU, one PNG per view per frame (montage tiles them)."""
    if not SHEET: return
    os.makedirs(SHEET, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'; sc.cycles.samples = 12
    sc.render.resolution_x = 320; sc.render.resolution_y = 320
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam); sc.camera = cam
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN')); sun.data.energy = 3.5
    sun.rotation_euler = (0.8, 0.25, 0.7); sc.collection.objects.link(sun)
    rim = bpy.data.objects.new("rim", bpy.data.lights.new("rim", 'SUN')); rim.data.energy = 1.5; rim.data.color = (0.5, 0.65, 1.0)
    rim.rotation_euler = (1.2, 0, 3.6); sc.collection.objects.link(rim)
    w = bpy.data.worlds.new("w"); w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.22, 0.24, 0.28, 1); sc.world = w
    mat = bpy.data.materials.new("m"); mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Roughness"].default_value = 0.65
    attr = nt.nodes.new("ShaderNodeAttribute"); attr.attribute_name = "col"
    nt.links.new(attr.outputs["Color"], bsdf.inputs["Base Color"])
    em = [k for k in bsdf.inputs.keys() if k.startswith("Emission")]
    ob.data.materials.append(mat)
    # the baked colours as an attribute for the preview (the emissive ones glow)
    me = ob.data
    ca = me.color_attributes.new("col", 'FLOAT_COLOR', 'POINT')
    for vi, c in enumerate(ob["_cols"]):
        ca.data[vi].color = (min(c[0], 1), min(c[1], 1), min(c[2], 1), 1)
    if "Emission Color" in bsdf.inputs:
        ea = nt.nodes.new("ShaderNodeAttribute"); ea.attribute_name = "emi"
        eatt = me.color_attributes.new("emi", 'FLOAT_COLOR', 'POINT')
        for vi, c in enumerate(ob["_cols"]):
            g = max(c) > 1.5
            eatt.data[vi].color = ((c[0] / 4, c[1] / 4, c[2] / 4, 1) if g else (0, 0, 0, 1))
        nt.links.new(ea.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = 3.0
    ground = bpy.data.meshes.new("g"); ground.from_pydata([(-20, -20, 0), (20, -20, 0), (20, 20, 0), (-20, 20, 0)], [], [(0, 1, 2, 3)])
    go = bpy.data.objects.new("g", ground); sc.collection.objects.link(go)
    gm = bpy.data.materials.new("gm"); gm.use_nodes = True
    next(n for n in gm.node_tree.nodes if n.type == "BSDF_PRINCIPLED").inputs["Base Color"].default_value = (0.12, 0.12, 0.13, 1)
    ground.materials.append(gm)
    bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    H = max(max(v.z for v in bb), 1.0); R = max(max(abs(v.x) for v in bb), max(abs(v.y) for v in bb))
    D = max(H, R) * 2.6
    for ci, clip in enumerate(poses):
        for fi, pose in enumerate(clip):
            pose_frame(ao, pose)
            for tag, (el, az) in (("game", (math.radians(62), math.radians(200))), ("side", (math.radians(10), math.radians(270)))):
                cam.location = (D * math.cos(el) * math.sin(az), D * math.cos(el) * math.cos(az), D * math.sin(el) + H * 0.4)
                d = Vector((0, 0, H * 0.4)) - cam.location
                cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
                cam.data.lens = 70
                sc.render.filepath = os.path.join(SHEET, "%s_%s_c%d_%02d.png" % (name, tag, ci, fi))
                bpy.ops.render.render(write_still=True)

for name in (kinds or list(CHARS)):
    mk, pf = CHARS[name]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    M = mk()
    ob, ao = build_object(M, name)
    pose_frame(ao, {})
    ao_col = bake_ao(ob, M)
    clips = [bake_clip(ob, M, ao, c) for c in pf()]
    size = write_cwchar(os.path.join(OUT, name + ".cwchar"), M, ao_col, clips, ob)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    print("LEVELCHAR %s: %d verts, %d tris, clips %s, %d bytes" % (name, len(M.v), tris, [len(c) for c in clips], size))
    ob["_cols"] = [tuple(c[k] * ao_col[vi] for k in range(3)) for vi, c in enumerate(M.col)]
    sheet(name, ob, ao, pf())
