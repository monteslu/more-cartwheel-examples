# Arena's motion: every clip the fighters play, on the source rig (Quaternius' Universal Animation Library 1 and 2,
# CC0 1.0: motion capture cleaned by hand, on the humanoid mannequin rig the soldier is built on, tools/blender/soldier.py).
# The clips the game needs are picked, cut, joined and timed here; where the libraries have no clip, one is made here
# from their motion or keyed in the rig's own space:
#   - locomotion in eight directions at a walk and a run: the library's walk and sprint, turned at the hips by 45 or 90
#     degrees with the trunk turned back up the spine (a third at each of the three spine joints), so the legs travel
#     that way while the chest keeps facing the aim; the back half from the same cycles run backwards;
#   - the rifle: the libraries hold a pistol, never a rifle, so the arms are solved here (a two-bone solve per arm, the
#     elbow's hinge kept on its own axis, the forearm taking half the wrist's twist) to grip a rifle shouldered on the
#     chest: the right hand on the pistol grip, the left under the handguard; the recoil, the reload (the weapon change),
#     the rifle jab and the rifle lowered for a throw are the same solve along keyed rifle and hand paths;
#   - the grenade throw: the library's overhand throw mirrored onto the left arm (the right hand keeps the rifle);
#   - hits from behind and from either side: the chest hit's spine motion turned to come from that side.
# The result, anim/ual.glb (the rig with every clip), is the source the soldiers' .cwimport retargets at pack time
# (cartwheel's retargeting: rotations relative to the rest poses, the hips scaled by leg length, the feet placed by
# IK, the planted feet of the cycles held still). out/hold.json carries the rifle's place in the right hand for
# soldier.py.
#   flatpak run --filesystem=<game> --command=blender org.blender.Blender -b --factory-startup \
#     --python <abs>/tools/blender/anims.py -- <ual1 AL_Standard.fbx> <ual2 UAL2_Standard.glb> <out anims.glb> <out hold.json> [sheet dir]
# (tools/fetch-anims.sh downloads the two libraries, checks them and runs this.)
import bpy, sys, math, os, json
from mathutils import Vector, Quaternion, Matrix

argv = sys.argv[sys.argv.index('--') + 1:]
FBX1, GLB2, OUT, HOLD = argv[:4]
SHEET = argv[4] if len(argv) > 4 else None
FPS = 30


def log(*a):
    print('[anims]', *a, flush=True)


bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.fps = FPS
bpy.ops.import_scene.fbx(filepath=FBX1)
ARM = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
bpy.ops.import_scene.gltf(filepath=GLB2)
for o in list(bpy.data.objects):
    if o != ARM and o.parent != ARM:
        bpy.data.objects.remove(o)
SRC = {}
for a in bpy.data.actions:
    SRC.setdefault(a.name.split('|')[-1], a)
BONES = [b.name for b in ARM.data.bones]
PB = ARM.pose.bones
for pb in PB:
    pb.rotation_mode = 'QUATERNION'
ARM.animation_data_create()
UP, FWD, SIDE = Vector((0, 0, 1)), Vector((0, -1, 0)), Vector((1, 0, 0))  # the rig faces -Y, its left is +X


def upd():
    bpy.context.view_layer.update()


# ── a clip is a list of frames; a frame is {bone: (location, rotation)} in each bone's own (basis) space
def assign(act):
    ARM.animation_data.action = act
    try:
        ARM.animation_data.action_slot = act.slots[0]
    except Exception:
        pass


def sample(name, a=None, b=None):
    act = SRC[name]
    assign(act)
    f0, f1 = (int(round(x)) for x in act.frame_range)
    a = f0 if a is None else a
    b = f1 if b is None else b
    out = []
    for f in range(a, b + 1):
        sc.frame_set(f)
        out.append({n: (PB[n].location.copy(), PB[n].rotation_quaternion.copy()) for n in BONES})
    ARM.animation_data.action = None
    return out


def in_place(c):
    return [{**f, 'root': (Vector((0, 0, 0)), f['root'][1].copy())} for f in c]


def cat(*cs):
    out = []
    for c in cs:
        out += c
    return out


def rev(c):
    return list(reversed(c))


def lerp_frame(a, b, t):
    return {n: (a[n][0].lerp(b[n][0], t), a[n][1].slerp(b[n][1], t)) for n in BONES}


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def loopify(c, n):
    out = list(c)
    for k in range(n):
        i = len(c) - n + k
        out[i] = lerp_frame(c[i], c[0], ease((k + 1) / n))
    return out


def retime(c, length_frames):
    out = []
    for k in range(length_frames):
        x = k * (len(c) - 1) / max(1, length_frames - 1)
        i = min(int(x), len(c) - 2)
        out.append(lerp_frame(c[i], c[i + 1], x - i))
    return out


def put(f):
    for n in BONES:
        PB[n].location = f[n][0]
        PB[n].rotation_quaternion = f[n][1]
    upd()


def take():
    return {n: (PB[n].location.copy(), PB[n].rotation_quaternion.copy()) for n in BONES}


def rot_about(bone, axis, ang):
    """turn one bone (and everything below) by ang radians about an armature-space axis through its head"""
    pb = PB[bone]
    h = pb.head.copy()
    R = Matrix.Translation(h) @ Quaternion(axis, ang).to_matrix().to_4x4() @ Matrix.Translation(-h)
    pb.matrix = R @ pb.matrix
    upd()


def map_frames(c, fn):
    out = []
    for i, f in enumerate(c):
        put(f)
        fn(i, len(c))
        out.append(take())
    return out


def turned(c, ang):
    """the legs turned ang about the vertical at the hips, the trunk turned back a third at each spine joint: the cycle
    travels ang off forward (to the left for ang > 0) while the chest faces forward"""
    def fn(i, n):
        rot_about('pelvis', UP, ang)
        for b in ('spine_01', 'spine_02', 'spine_03'):
            rot_about(b, UP, -ang / 3)
    return map_frames(c, fn)


# ── the rifle and the arms ─────────────────────────────────────────────────────────────────────────────────────
# a frame of the rifle is a 4x4 in armature space: columns its right, its forward (along the bore), its up, and the
# butt's centre on the bore line. Points on it are given as (right, forward, up) in metres.
def rifle_frame(origin, fwd, up):
    f = fwd.normalized()
    u = (up - f * up.dot(f)).normalized()
    r = f.cross(u)
    M = Matrix.Identity(4)
    for i in range(3):
        M[i][0], M[i][1], M[i][2], M[i][3] = r[i], f[i], u[i], origin[i]
    return M


def on_rifle(RF, p):
    return (RF @ Vector((p[0], p[1], p[2], 1))).xyz


def rdir(RF, d):
    return (RF.to_3x3() @ Vector(d)).normalized()


GRIP = (0.0, 0.205, -0.075)     # the pistol grip's middle, under the receiver
SUPPORT = (0.0, 0.40, -0.05)    # the handguard's underside, where the left palm holds it
MUZZLE = (0.0, 0.86, 0.0)
# the hand's own frame at rest (the T-pose: fingers out along the arm, palm down)
HAND_F0 = {'r': Vector((-1, 0, 0)), 'l': Vector((1, 0, 0))}
HAND_P0 = Vector((0, 0, -1))


def hand_rot(s, f1, p1):
    """the hand's armature rotation with its fingers along f1 and its palm facing p1"""
    f0, p0 = HAND_F0[s], HAND_P0
    B0 = Matrix((f0, p0, f0.cross(p0))).transposed()
    f1 = f1.normalized()
    p1 = (p1 - f1 * p1.dot(f1)).normalized()
    B1 = Matrix((f1, p1, f1.cross(p1))).transposed()
    return B1 @ B0.transposed() @ ARM.data.bones['hand_' + s].matrix_local.to_3x3()


HINGE = {}  # each upper arm's elbow hinge, in its own frame (from a pose with the elbows bent)


def calibrate_hinges():
    put(sample('Pistol_Idle_Loop', 1, 1)[0])
    for s in ('l', 'r'):
        up, lo, ha = PB['upperarm_' + s], PB['lowerarm_' + s], PB['hand_' + s]
        a = (lo.head - up.head).normalized()
        b = (ha.head - lo.head).normalized()
        h = a.cross(b).normalized()
        HINGE[s] = up.matrix.to_3x3().inverted() @ h
        log('hinge', s, tuple(round(x, 3) for x in HINGE[s]), 'bend deg', round(math.degrees(a.angle(b)), 1))


def set_rot(name, R3, head=None):
    pb = PB[name]
    M = R3.to_4x4()
    M.translation = pb.head if head is None else head
    pb.matrix = M
    upd()


REACH = {'l': 0.0, 'r': 0.0}


def solve_arm(s, W, Rh, pole, twist_share=0.5):
    """the arm s reaches the wrist W with the hand turned to Rh; the elbow toward pole, bending on its own hinge"""
    up, lo, ha = PB['upperarm_' + s], PB['lowerarm_' + s], PB['hand_' + s]
    S = up.head.copy()
    L1 = (lo.head - up.head).length
    L2 = (ha.head - lo.head).length
    d = W - S
    dist = max(abs(L1 - L2) * 1.01, min(d.length, (L1 + L2) * 0.999))
    REACH[s] = max(REACH[s], d.length - dist)
    n = d.normalized()
    a = (L1 * L1 + dist * dist - L2 * L2) / (2 * L1 * dist)
    pv = pole - S
    b = (pv - n * pv.dot(n)).normalized()
    E = S + n * (L1 * a) + b * (L1 * math.sqrt(max(0.0, 1 - a * a)))
    Wc = S + n * dist
    # the upper arm: along S->E, rolled so its hinge is the bend's own axis
    R = up.matrix.to_3x3()
    y0 = R.col[1].normalized()
    y1 = (E - S).normalized()
    R = y0.rotation_difference(y1).to_matrix() @ R
    hn = (R @ HINGE[s]).normalized()
    hw = y1.cross((Wc - E).normalized())
    if hw.length > 1e-6:
        hw.normalize()
        hn_p = (hn - y1 * hn.dot(y1)).normalized()
        ang = math.atan2(y1.dot(hn_p.cross(hw)), hn_p.dot(hw))
        R = Matrix.Rotation(ang, 3, y1) @ R
    set_rot('upperarm_' + s, R)
    # the forearm: along E->W, its roll carried from the upper arm, then half the wrist's twist
    R2 = lo.matrix.to_3x3()
    y0 = R2.col[1].normalized()
    y2 = (Wc - lo.head).normalized()
    R2 = y0.rotation_difference(y2).to_matrix() @ R2
    rel = R2.inverted() @ Rh
    q = rel.to_quaternion()
    # twist of rel about the forearm's own Y (swing-twist decomposition)
    tw = Quaternion((q.w, 0, q.y, 0))
    if tw.magnitude > 1e-6:
        tw.normalize()
        ang = 2 * math.atan2(tw.y, tw.w)
        R2 = R2 @ Matrix.Rotation(ang * twist_share, 3, 'Y')
    set_rot('lowerarm_' + s, R2)
    set_rot('hand_' + s, Rh)
    return Wc


FINGERS = ('index', 'middle', 'ring', 'pinky')


def curl(s, amounts, thumb=0.35):
    """the fingers bent toward the palm by amounts (radians per joint 1, 2, 3; index first)"""
    ax = Vector((0, -1, 0)) if s == 'r' else Vector((0, 1, 0))  # at rest the palm faces down, the fingers point out
    for fi, f in enumerate(FINGERS):
        for k in (1, 2, 3):
            n = '%s_0%d_%s' % (f, k, s)
            b = ARM.data.bones[n]
            a = amounts[fi][k - 1]
            PB[n].rotation_quaternion = Quaternion(b.matrix_local.to_3x3().inverted() @ ax, a)
    for k in (1, 2, 3):
        n = 'thumb_0%d_%s' % (k, s)
        b = ARM.data.bones[n]
        PB[n].rotation_quaternion = Quaternion(b.matrix_local.to_3x3().inverted() @ ax, thumb * (0.5 if k == 1 else 1.0))
    upd()


GRIP_R = [(0.35, 0.45, 0.25), (1.2, 1.3, 0.8), (1.25, 1.35, 0.8), (1.25, 1.3, 0.8)]  # the trigger finger half out
GRIP_L = [(0.8, 0.9, 0.5), (0.9, 1.0, 0.6), (0.95, 1.0, 0.6), (1.0, 1.0, 0.6)]


def rf_in_chest(RF):
    return PB['spine_03'].matrix.inverted() @ RF


def chest_rf(RFc):
    return PB['spine_03'].matrix @ RFc


def hold_arms(RF, left=None, right=True, rpole=None, lpole=None, curl_l=True):
    """both hands on the rifle at RF (armature space): the right on the grip; the left under the handguard, or at
    left=(point, fingers, palm) when it is busy elsewhere (the reload)"""
    sh_r, sh_l = PB['upperarm_r'].head.copy(), PB['upperarm_l'].head.copy()
    fwd = rdir(RF, (0, 1, 0))
    upv = rdir(RF, (0, 0, 1))
    rgt = rdir(RF, (1, 0, 0))
    if right:
        f1 = (fwd - upv * 0.35).normalized()
        p1 = -rgt
        g = on_rifle(RF, GRIP)
        W = g - p1 * 0.035 - f1 * 0.065
        solve_arm('r', W, hand_rot('r', f1, p1), rpole if rpole is not None else sh_r + Vector((-0.3, 0.25, -0.45)))
        curl('r', GRIP_R)
    if left is None:
        f1 = (rgt * 0.75 + fwd * 0.65).normalized()
        p1 = upv
        sp = on_rifle(RF, SUPPORT)
        W = sp - p1 * 0.045 - f1 * 0.06
        solve_arm('l', W, hand_rot('l', f1, p1), lpole if lpole is not None else sp + Vector((0.15, 0.25, -0.5)))
    elif left is not False:
        pt, f1, p1 = left
        W = pt - p1 * 0.04 - f1 * 0.06
        solve_arm('l', W, hand_rot('l', f1, p1), lpole if lpole is not None else sh_l + Vector((0.25, 0.15, -0.5)))
    if left is not False and curl_l:
        curl('l', GRIP_L)


calibrate_hinges()
OUT_CLIPS = {}


def clip(name, c, loop=False):
    OUT_CLIPS[name] = (c, loop)


# ── locomotion ───────────────────────────────────────────────────────────────────────────────────────────────────
idle = sample('Idle_Loop')
walk = sample('Walk_Loop')
run = sample('Sprint_Loop')
clip('idle', idle, True)
for nm, c in (('walk', walk), ('run', run)):
    clip(nm + '_f', c, True)
    clip(nm + '_fl', turned(c, math.radians(45)), True)
    clip(nm + '_fr', turned(c, math.radians(-45)), True)
    clip(nm + '_l', turned(c, math.radians(90)), True)
    clip(nm + '_r', turned(c, math.radians(-90)), True)
    b = rev(c)
    clip(nm + '_b', b, True)
    clip(nm + '_bl', turned(b, math.radians(-45)), True)   # backwards, to the left behind
    clip(nm + '_br', turned(b, math.radians(45)), True)
    clip(nm + '_bl90', turned(b, math.radians(-90)), True)  # backwards cycle facing away, travelling left
    clip(nm + '_br90', turned(b, math.radians(90)), True)
# jumping: the take-off (the library's start from the push), the air (its loop), the landing taken in the knees
clip('jump', sample('Jump_Start', 2, 14))
clip('air', sample('Jump_Loop'), True)
clip('land', sample('Jump_Land', 1, 26))

# ── the rifle hold: the rifle's place on the chest, from the stand ────────────────────────────────────────────────
base = idle[0]
put(base)
# shoulders a little forward (the stance of someone holding a rifle up)
rot_about('clavicle_l', UP, math.radians(-16))
rot_about('clavicle_r', UP, math.radians(6))
base_arms = take()
sh = PB['upperarm_r'].head.copy()
pocket = sh + Vector((0.085, -0.06, -0.005))   # the butt in the shoulder's pocket
RF0 = rifle_frame(pocket + Vector((0, 0, 0.035)), FWD + Vector((0, 0, -0.02)), UP)
RFc = rf_in_chest(RF0)  # the rifle in the chest's frame: every arm pose below keeps it there unless moved


def rifle_at(off_pos=(0, 0, 0), pitch=0.0, roll=0.0, yaw=0.0, about=GRIP):
    """the rifle moved from its hold: turned about a point on it (pitch: the muzzle up, roll: the top to its left,
    yaw: the muzzle to its left), then shifted by off_pos in its own (right, forward, up), with the chest as it is now"""
    RF = chest_rf(RFc)
    p = on_rifle(RF, about)
    R = (Quaternion(rdir(RF, (0, 0, 1)), yaw) @ Quaternion(rdir(RF, (1, 0, 0)), pitch) @ Quaternion(rdir(RF, (0, 1, 0)), -roll)).to_matrix().to_4x4()
    M = Matrix.Translation(p) @ R @ Matrix.Translation(-p) @ RF
    off = RF.to_3x3() @ Vector(off_pos)
    return Matrix.Translation(off) @ M


def held(frame_fn, n, base_frame=None, keep=None):
    """n frames of the arms solved onto the rifle: frame_fn(i) poses the trunk (optional) and returns the hold's
    arguments for that frame"""
    out = []
    for i in range(n):
        put(base_frame if base_frame is not None else base_arms)
        args = frame_fn(i)
        hold_arms(**args)
        out.append(take())
    return out


hold = held(lambda i: {'RF': rifle_at()}, 2)
clip('hold', hold, True)
put(hold[0])
HR = PB['hand_r'].matrix.copy()
rf_in_hand = HR.inverted() @ chest_rf(RFc)
log('reach shortfall (m): left %.3f right %.3f' % (REACH['l'], REACH['r']))
REACH['l'] = REACH['r'] = 0
barrel_chest = PB['spine_03'].matrix.to_3x3().inverted() @ rdir(chest_rf(RFc), (0, 1, 0))
chest_rest = ARM.data.bones['spine_03'].matrix_local.to_3x3()
fwd_rest = chest_rest @ barrel_chest  # the barrel's way when the chest is at rest (Blender axes)
head_chest = PB['spine_03'].matrix.inverted() @ PB['Head'].head

# the recoil: the rifle driven back into the shoulder and its muzzle up, then home (a pose the game blends toward,
# 0..1, by its own spring: the kick never restarts a clip mid-burst)
clip('kick', held(lambda i: {'RF': rifle_at((0, -0.045, 0.012), math.radians(7))}, 2), True)


def path(keys, t):
    """keys [(t, value)] eased between: a float or a tuple"""
    if t <= keys[0][0]:
        return keys[0][1]
    for k in range(len(keys) - 1):
        (t0, a), (t1, b) = keys[k], keys[k + 1]
        if t0 <= t <= t1:
            s = ease((t - t0) / max(1e-6, t1 - t0))
            if isinstance(a, tuple):
                return tuple(x + (y - x) * s for x, y in zip(a, b))
            return a + (b - a) * s
    return keys[-1][1]


# the weapon change (a reload): the rifle canted and lowered, the left hand to the magazine, down (the empty one
# out), to the chest rig, back up into the well, a slap on the side, and home under the handguard
RL = 48


def reload_frame(i):
    tilt = path([(0, 0.0), (7, 1.0), (40, 1.0), (47, 0.0)], i)
    RF = rifle_at((0.03 * tilt, -0.06 * tilt, -0.07 * tilt), math.radians(14) * tilt, math.radians(28) * tilt)
    fwd, upv, rgt = rdir(RF, (0, 1, 0)), rdir(RF, (0, 0, 1)), rdir(RF, (1, 0, 0))
    well = on_rifle(RF, (0.0, 0.27, -0.12))
    out = on_rifle(RF, (0.0, 0.27, -0.33))
    side = on_rifle(RF, (-0.07, 0.16, 0.02))
    pouch = (PB['spine_03'].matrix @ Vector((0, 0, 0, 1))).xyz + Vector((0.07, -0.17, -0.14))
    # where the left hand is, as a weight per station along the way
    st = [(0, 'sup'), (9, 'well'), (14, 'out'), (22, 'pouch'), (29, 'out'), (33, 'well'), (37, 'side'), (41, 'side'), (47, 'sup')]
    pts = {'sup': on_rifle(RF, SUPPORT) - upv * 0.045 + rgt * 0.0, 'well': well, 'out': out, 'pouch': pouch, 'side': side}
    fing = {'sup': ((rgt * 0.75 + fwd * 0.65), upv), 'well': ((upv * 0.6 + fwd * 0.4), rgt), 'out': ((upv * 0.6 + fwd * 0.4), rgt), 'pouch': ((UP * 0.5 + FWD * 0.6), -FWD), 'side': ((fwd * 0.9 + upv * 0.2), -rgt)}
    for k in range(len(st) - 1):
        (t0, a), (t1, b) = st[k], st[k + 1]
        if t0 <= i <= t1:
            s = ease((i - t0) / max(1, t1 - t0))
            p = pts[a].lerp(pts[b], s)
            f = fing[a][0].normalized().lerp(fing[b][0].normalized(), s)
            pl = fing[a][1].normalized().lerp(fing[b][1].normalized(), s)
            return {'RF': RF, 'left': (p, f, pl), 'curl_l': True}
    return {'RF': RF}


clip('reload', held(reload_frame, RL))
# the rifle jab: drawn back with the chest turned, driven forward a third of a metre, and back to the hold
BJ = 22


def bash_frame(i):
    d = path([(0, 0.0), (5, -0.08), (9, 0.34), (13, 0.30), (21, 0.0)], i)
    tw = path([(0, 0.0), (5, 0.18), (9, -0.16), (13, -0.14), (21, 0.0)], i)
    rot_about('spine_02', UP, tw * 0.5)
    rot_about('spine_03', UP, tw * 0.5)
    lean = path([(0, 0.0), (5, -0.04), (9, 0.12), (21, 0.0)], i)
    rot_about('spine_01', SIDE, -lean)
    return {'RF': rifle_at((0, d, 0.02 * d), 0.15 * d)}


clip('bash', held(bash_frame, BJ))


# ── the throw: the library's overhand throw on the left arm (mirrored), the rifle lowered in the right hand ─────────
def mirror_frames(c):
    S = Matrix.Diagonal((-1, 1, 1, 1))
    other = {}
    for n in BONES:
        if n.endswith('_l'):
            other[n] = n[:-2] + '_r'
        elif n.endswith('_r'):
            other[n] = n[:-2] + '_l'
        else:
            other[n] = n
    order = []
    def walk_b(b):
        order.append(b.name)
        for ch in b.children:
            walk_b(ch)
    for b in ARM.data.bones:
        if b.parent is None:
            walk_b(b)
    out = []
    for f in c:
        put(f)
        D = {n: PB[n].matrix @ ARM.data.bones[n].matrix_local.inverted() for n in BONES}  # each bone's turn from rest
        for n in order:
            m = other[n]
            Md = S @ D[m] @ S
            PB[n].matrix = Md @ ARM.data.bones[n].matrix_local
            upd()
        out.append(take())
    return out


throw_src = in_place(sample('OverhandThrow'))
throw_m = mirror_frames(throw_src)


def throw_frame(i):
    put(throw_m[i])
    low = path([(0, 0.0), (6, 1.0), (len(throw_m) - 8, 1.0), (len(throw_m) - 1, 0.0)], i)
    return {'RF': rifle_at((0.04 * low, -0.04 * low, -0.10 * low), math.radians(-38) * low, math.radians(-8) * low), 'left': False}


throw = []
for i in range(len(throw_m)):
    a = throw_frame(i)
    hold_arms(**a)
    throw.append(take())
clip('throw', throw)


# ── hits: the chest hit from the front, the head hit, and the chest hit's trunk motion turned to come from behind
# and from either side (each spine joint's own turn from the first frame, its axis turned about the vertical)
CHAIN = ['pelvis', 'spine_01', 'spine_02', 'spine_03', 'neck_01', 'Head']


def turned_hit(c, yaw):
    put(c[0])
    R0 = {n: PB[n].matrix.to_3x3() for n in CHAIN}
    P0 = {n: (PB[n].parent.matrix.to_3x3() if PB[n].parent else Matrix.Identity(3)) for n in CHAIN}
    deltas = []
    for f in c:
        put(f)
        d = {}
        for n in CHAIN:
            Rp = PB[n].parent.matrix.to_3x3() if PB[n].parent else Matrix.Identity(3)
            loc_now = Rp.inverted() @ PB[n].matrix.to_3x3()
            loc_0 = P0[n].inverted() @ R0[n]
            dq = (loc_now @ loc_0.inverted()).to_quaternion()  # in the parent's frame
            ax, ang = dq.to_axis_angle()
            d[n] = (P0[n] @ ax, ang)  # its axis in armature space at the first frame
        deltas.append(d)
    Y = Quaternion(UP, yaw)
    out = []
    for d in deltas:
        put(c[0])
        for n in CHAIN:
            ax, ang = d[n]
            if abs(ang) > 1e-6:
                rot_about(n, Y @ ax, ang)
        out.append(take())
    return out


hit = sample('Hit_Chest')
clip('hit_f', hit)
clip('hit_b', turned_hit(hit, math.pi))
clip('hit_l', turned_hit(hit, math.radians(90)))
clip('hit_r', turned_hit(hit, math.radians(-90)))
clip('hit_head', sample('Hit_Head'))
# a fall for when no ragdoll is free (the game's ragdolls are capped): the library's death, then lying still
death = sample('Death01')
clip('die', death)
clip('dead', [death[-1], death[-1]], True)

# ── out ──────────────────────────────────────────────────────────────────────────────────────────────────────────
log('reach shortfall in the actions (m): left %.3f right %.3f' % (REACH['l'], REACH['r']))
G = lambda v: [v.x, v.z, -v.y]  # Blender axes to glTF's
info = {
    'rf_in_hand': [list(r) for r in rf_in_hand],
    'grip': GRIP, 'support': SUPPORT, 'muzzle': MUZZLE,
    'barrel_rest_gltf': G(fwd_rest.normalized()),
    'head_in_chest': list(head_chest),
}
os.makedirs(os.path.dirname(HOLD), exist_ok=True)
with open(HOLD, 'w') as f:
    json.dump(info, f, indent=1)
log('hold', info['barrel_rest_gltf'])

if SHEET:
    os.makedirs(SHEET, exist_ok=True)
    # a rifle stand-in on the right hand, to judge the hold by eye
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * 0.05, v.co.y * 0.86 + 0.43, v.co.z * 0.07))
    me = bpy.data.meshes.new('rifle')
    bm.to_mesh(me)
    ob = bpy.data.objects.new('rifle', me)
    sc.collection.objects.link(ob)
    ob.parent = ARM
    ob.parent_type = 'BONE'
    ob.parent_bone = 'hand_r'
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.render.resolution_x, sc.render.resolution_y = 520, 620
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    sc.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = 2.3
    sc.camera = cam
    shots = [('hold', 0), ('kick', 0), ('reload', 18), ('reload', 36), ('bash', 9), ('throw', 12), ('throw', 22),
             ('run_f', 5), ('run_l', 5), ('walk_bl', 10), ('hit_l', 5), ('land', 6)]
    for nm, fr in shots:
        put(OUT_CLIPS[nm][0][min(fr, len(OUT_CLIPS[nm][0]) - 1)])
        # the stand-in follows the hand: its frame is the rifle's in the hand
        Mh = PB['hand_r'].matrix
        ob.parent = None
        ob.matrix_world = ARM.matrix_world @ Mh @ rf_in_hand
        for k, (az, el) in enumerate(((math.radians(-35), 0.15), (math.radians(90), 0.05))):
            d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
            cam.location = Vector((0, 0, 1.0)) + d * 6
            cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
            sc.render.filepath = os.path.join(SHEET, '%s_%d_%d.png' % (nm, fr, k))
            bpy.ops.render.render(write_still=True)

for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)
for name, (c, loop) in OUT_CLIPS.items():
    act = bpy.data.actions.new(name)
    assign(act)
    for i, f in enumerate(c):
        for n in BONES:
            PB[n].location = f[n][0]
            PB[n].rotation_quaternion = f[n][1]
            PB[n].keyframe_insert('location', frame=i)
            PB[n].keyframe_insert('rotation_quaternion', frame=i)
    act.use_fake_user = True
    log(name, len(c), 'frames', '(loop)' if loop else '')
ARM.animation_data.action = None
for pb in PB:
    pb.location = Vector()
    pb.rotation_quaternion = Quaternion()
for o in list(bpy.data.objects):
    if o.type not in ('ARMATURE', 'MESH') or o.name == 'rifle':
        bpy.data.objects.remove(o)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_animations=True, export_animation_mode='ACTIONS',
                          export_force_sampling=True, export_frame_step=1, export_optimize_animation_size=False,
                          export_materials='NONE', export_yup=True)
log('wrote', OUT, len(OUT_CLIPS), 'clips')
