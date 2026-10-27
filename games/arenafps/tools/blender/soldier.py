# The armoured fighter: one skinned mesh, 20 bones, clips idle / run / die. Usage (see build.sh):
#   blender -b --python soldier.py -- <out_dir> [sheet.png]
# Everything is modelled here from primitives (our own work, same licence as the game).
import bpy, bmesh, math, sys, os
from mathutils import Vector, Matrix, Quaternion
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

TEAMS = {'a': (0.15, 0.55, 1.0), 'b': (1.0, 0.25, 0.15)}

# bone: (parent, head, tail)   model faces +Y, Z up, metres
BONES = {
    'hips':     (None,       (0, 0, 0.95),     (0, 0, 1.12)),
    'spine':    ('hips',     (0, 0, 1.12),     (0, 0, 1.38)),
    'chest':    ('spine',    (0, 0, 1.38),     (0, 0, 1.58)),
    'neck':     ('chest',    (0, 0, 1.58),     (0, 0, 1.68)),
    'head':     ('neck',     (0, 0, 1.68),     (0, 0, 1.90)),
    'upper_l':  ('chest',    (0.26, 0.0, 1.50), (0.30, 0.20, 1.34)),
    'fore_l':   ('upper_l',  (0.30, 0.20, 1.34), (0.14, 0.42, 1.32)),
    'upper_r':  ('chest',    (-0.26, 0.0, 1.50), (-0.20, 0.18, 1.32)),
    'fore_r':   ('upper_r',  (-0.20, 0.18, 1.32), (-0.04, 0.50, 1.33)),
    'thigh_l':  ('hips',     (0.14, 0, 0.95),  (0.14, 0, 0.52)),
    'shin_l':   ('thigh_l',  (0.14, 0, 0.52),  (0.14, 0, 0.10)),
    'foot_l':   ('shin_l',   (0.14, 0, 0.10),  (0.14, 0.18, 0.04)),
    'thigh_r':  ('hips',     (-0.14, 0, 0.95), (-0.14, 0, 0.52)),
    'shin_r':   ('thigh_r',  (-0.14, 0, 0.52), (-0.14, 0, 0.10)),
    'foot_r':   ('shin_r',   (-0.14, 0, 0.10), (-0.14, 0.18, 0.04)),
}

def build(team):
    reset()
    col = TEAMS[team]
    # team readability at split-screen sizes: the armour itself carries the team colour, the trims glow it
    suit = material('suit', (0.06, 0.065, 0.07), 0.85, 0.0)
    armor = material('armor', (0.08 + col[0] * 0.32, 0.08 + col[1] * 0.3, 0.08 + col[2] * 0.32), 0.38, 0.35)
    plate = material('plate', (0.30, 0.31, 0.33), 0.45, 0.6)
    trim = material('trim', col, 0.4, 0.0, emit=col, emit_strength=2.5)
    visor = material('visor', (col[0] * 0.2, col[1] * 0.2, col[2] * 0.2), 0.15, 0.0, emit=col, emit_strength=8.0)
    gun = material('gunmetal', (0.07, 0.07, 0.08), 0.35, 0.9)
    parts = []   # (object, bone name or weights fn)

    def add(ob, bone):
        g = ob.vertex_groups.new(name=bone)
        g.add([v.index for v in ob.data.vertices], 1.0, 'REPLACE')
        parts.append(ob)
        return ob

    # --- torso
    add(bevel_box('pelvis', (0.36, 0.24, 0.20), (0, 0, 1.0), 0.04, 3, armor), 'hips')
    add(bevel_box('belly', (0.30, 0.20, 0.26), (0, 0, 1.25), 0.05, 3, suit), 'spine')
    add(bevel_box('chestplate', (0.46, 0.28, 0.34), (0, 0.02, 1.47), 0.07, 4, armor), 'chest')
    add(bevel_box('chesttrim', (0.30, 0.02, 0.07), (0, 0.165, 1.52), 0.01, 2, trim), 'chest')
    add(bevel_box('backpack', (0.30, 0.14, 0.34), (0, -0.20, 1.45), 0.04, 3, plate), 'chest')
    add(bevel_box('packtrim', (0.08, 0.02, 0.22), (0, -0.275, 1.45), 0.01, 2, trim), 'chest')
    add(bevel_box('belt', (0.37, 0.25, 0.05), (0, 0, 1.12), 0.015, 2, plate), 'hips')
    add(cyl('neck', 0.055, 0.12, (0, 0, 1.64), verts=14, mat=suit), 'neck')
    # --- head: helmet, visor
    add(sphere('helmet', 0.15, (0, 0, 1.80), (0.95, 1.1, 1.0), armor), 'head')
    add(bevel_box('visor', (0.20, 0.05, 0.065), (0, 0.115, 1.81), 0.02, 3, visor), 'head')
    add(bevel_box('crest', (0.04, 0.2, 0.05), (0, -0.02, 1.925), 0.015, 2, trim), 'head')
    add(bevel_box('jaw', (0.14, 0.09, 0.07), (0, 0.09, 1.72), 0.025, 2, suit), 'head')
    # --- shoulders
    for s, sg in (('l', 1), ('r', -1)):
        add(sphere('pauld_' + s, 0.115, (sg * 0.30, 0, 1.56), (1.1, 1.0, 0.85), armor), 'upper_' + s)
        add(bevel_box('pauldtrim_' + s, (0.05, 0.16, 0.03), (sg * 0.31, 0, 1.64), 0.01, 2, trim), 'upper_' + s)
        add(bevel_box('kneepad_' + s, (0.12, 0.08, 0.13), (sg * 0.14, 0.085, 0.52), 0.03, 3, plate), 'shin_' + s)
        add(bevel_box('boot_' + s, (0.12, 0.28, 0.12), (sg * 0.14, 0.05, 0.06), 0.035, 3, armor), 'foot_' + s)
        add(bevel_box('hip_' + s, (0.12, 0.2, 0.2), (sg * 0.20, 0, 0.98), 0.04, 3, plate), 'thigh_' + s)
        add(bevel_box('glove_' + s, (0.10, 0.12, 0.10), ((0.14 if sg > 0 else -0.04), 0.46 if sg > 0 else 0.55, 1.32), 0.03, 3, armor), 'fore_' + s)

    # --- limbs as tubes with a blend across the joint
    def tube(name, pts, radii, mat, bone_a, bone_b, blend_at, blend_w=0.10, seg=12):
        bm = bmesh.new()
        rings = []
        for i, (p, r) in enumerate(zip(pts, radii)):
            p = Vector(p)
            t = (Vector(pts[min(i + 1, len(pts) - 1)]) - Vector(pts[max(i - 1, 0)])).normalized()
            a = Vector((1, 0, 0)) if abs(t.x) < 0.9 else Vector((0, 1, 0))
            u = t.cross(a).normalized(); v = t.cross(u).normalized()
            ring = []
            for k in range(seg):
                ang = 2 * math.pi * k / seg
                ring.append(bm.verts.new(p + (u * math.cos(ang) + v * math.sin(ang)) * r))
            rings.append(ring)
        for i in range(len(rings) - 1):
            for k in range(seg):
                bm.faces.new((rings[i][k], rings[i][(k + 1) % seg], rings[i + 1][(k + 1) % seg], rings[i + 1][k]))
        bmesh.ops.contextual_create(bm, geom=rings[0]); bmesh.ops.contextual_create(bm, geom=rings[-1])
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        ob = new_obj(name, bm, mat)
        smooth(ob, 1.0)
        ga = ob.vertex_groups.new(name=bone_a); gb = ob.vertex_groups.new(name=bone_b)
        for v in ob.data.vertices:
            d = (v.co - blend_at).length
            # signed distance along the limb: towards the proximal end is negative
            prox = Vector(pts[0]); s = 1 if (v.co - prox).length < (blend_at - prox).length else -1
            t = max(0.0, min(1.0, 0.5 - s * d / (2 * blend_w))) if d < blend_w else (1.0 if s > 0 else 0.0)
            # t = weight of the proximal bone
            ga.add([v.index], t, 'REPLACE'); gb.add([v.index], 1 - t, 'REPLACE')
        parts.append(ob)
        return ob

    def arm(s, shoulder, elbow, hand, ra=0.07, rb=0.055):
        n = 7
        pts = []; rad = []
        for i in range(n):
            t = i / (n - 1)
            pts.append(Vector(shoulder).lerp(Vector(elbow), t)); rad.append(ra + (ra * 0.9 - ra) * t)
        for i in range(1, n):
            t = i / (n - 1)
            pts.append(Vector(elbow).lerp(Vector(hand), t)); rad.append(ra * 0.9 + (rb - ra * 0.9) * t)
        tube('arm_' + s, pts, rad, suit, 'upper_' + s, 'fore_' + s, Vector(elbow))
    arm('l', BONES['upper_l'][1], BONES['upper_l'][2], BONES['fore_l'][2])
    arm('r', BONES['upper_r'][1], BONES['upper_r'][2], BONES['fore_r'][2])

    def leg(s, hip, knee, ankle):
        pts = []; rad = []
        for i in range(8):
            t = i / 7
            pts.append(Vector(hip).lerp(Vector(knee), t)); rad.append(0.12 - 0.03 * t)
        for i in range(1, 8):
            t = i / 7
            pts.append(Vector(knee).lerp(Vector(ankle), t)); rad.append(0.09 - 0.025 * t)
        tube('leg_' + s, pts, rad, suit, 'thigh_' + s, 'shin_' + s, Vector(knee), 0.12)
    leg('l', BONES['thigh_l'][1], BONES['thigh_l'][2], BONES['shin_l'][2])
    leg('r', BONES['thigh_r'][1], BONES['thigh_r'][2], BONES['shin_r'][2])

    # --- the rifle, held by both hands, rigid to the right forearm
    add(bevel_box('rifle_body', (0.07, 0.62, 0.11), (-0.04, 0.38, 1.35), 0.02, 3, gun), 'fore_r')
    add(bevel_box('rifle_stock', (0.06, 0.2, 0.13), (-0.04, 0.02, 1.33), 0.02, 3, plate), 'fore_r')
    add(cyl('rifle_barrel', 0.022, 0.4, (-0.04, 0.82, 1.37), rot=(math.pi / 2, 0, 0), verts=14, mat=gun), 'fore_r')
    add(bevel_box('rifle_glow', (0.075, 0.18, 0.02), (-0.04, 0.42, 1.415), 0.005, 2, visor), 'fore_r')

    ob = join(parts, 'Soldier')
    arm_data = bpy.data.armatures.new('Rig')
    rig = bpy.data.objects.new('Rig', arm_data)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for n, (par, h, t) in BONES.items():
        eb = arm_data.edit_bones.new(n)
        eb.head, eb.tail = Vector(h), Vector(t)
        if par:
            eb.parent = arm_data.edit_bones[par]
    bpy.ops.object.mode_set(mode='OBJECT')
    ob.parent = rig
    mod = ob.modifiers.new('Armature', 'ARMATURE')
    mod.object = rig
    return ob, rig


def rotg(pb, axis, ang):
    """pose bone rotation of `ang` about a global axis (the bone's own rest frame keeps the axes readable)"""
    rest = pb.bone.matrix_local.to_quaternion()
    q = rest.inverted() @ Quaternion(axis, ang) @ rest
    pb.rotation_mode = 'QUATERNION'
    pb.rotation_quaternion = q


def key_pose(rig, frame, pose):
    for name, (rot, loc) in pose.items():
        pb = rig.pose.bones[name]
        if rot is not None:
            rotg(pb, rot[0], rot[1])
        pb.keyframe_insert('rotation_quaternion', frame=frame)
        if loc is not None:
            rest = pb.bone.matrix_local.to_quaternion()
            pb.location = rest.inverted() @ Vector(loc)
            pb.keyframe_insert('location', frame=frame)


def clear_pose(rig):
    for pb in rig.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)


X = (1, 0, 0)
Z = (0, 0, 1)

def make_action(rig, name, frames, fn):
    act = bpy.data.actions.new(name)
    rig.animation_data_create()
    rig.animation_data.action = act
    clear_pose(rig)
    for f in frames:
        key_pose(rig, f, fn(f))
    # park it on an NLA track so every clip exports
    tr = rig.animation_data.nla_tracks.new()
    tr.name = name
    tr.strips.new(name, int(frames[0]), act)
    rig.animation_data.action = None
    clear_pose(rig)


def clips(rig):
    def idle(f):
        t = (f - 1) / 48 * 2 * math.pi
        b = math.sin(t)
        return {'hips': ((X, 0.0), (0, 0, -0.004 + 0.006 * b)), 'chest': ((X, 0.015 * b), None),
                'head': ((X, -0.015 * b), None), 'thigh_l': ((X, 0.0), None), 'thigh_r': ((X, 0.0), None)}
    def run(f):
        t = (f - 1) / 20 * 2 * math.pi
        s = math.sin(t); c = math.cos(t)
        def knee(ph):  # flexes while the leg swings forward
            return -(0.15 + 0.9 * max(0.0, math.cos(ph)))
        return {'hips': ((X, -0.08), (0, 0, -0.03 + 0.03 * abs(c))), 'spine': ((X, -0.06), None),
                'chest': ((Z, 0.12 * s), None), 'head': ((Z, -0.12 * s), None),
                'thigh_l': ((X, 0.75 * s), None), 'shin_l': ((X, knee(t + 1.2)), None),
                'thigh_r': ((X, -0.75 * s), None), 'shin_r': ((X, knee(t + 1.2 + math.pi)), None)}
    def die(f):
        k = min(1.0, (f - 1) / 26.0)
        e = 1 - (1 - k) ** 3
        return {'hips': ((X, -1.35 * e), (0, -0.55 * e, -0.62 * e)), 'spine': ((X, 0.2 * e), None), 'chest': ((X, 0.15 * e), None),
                'head': ((X, 0.3 * e), None), 'thigh_l': ((X, 0.9 * e), None), 'shin_l': ((X, -1.3 * e), None),
                'thigh_r': ((X, 0.5 * e), None), 'shin_r': ((X, -0.7 * e), None)}
    make_action(rig, 'idle', range(1, 50, 4), idle)
    make_action(rig, 'run', range(1, 22, 2), run)
    make_action(rig, 'die', range(1, 31, 3), die)


def main():
    a = args()
    out = a[0]
    sheet = a[1] if len(a) > 1 else None
    for team in TEAMS:
        reset()
        ob, rig = build(team)
        bpy.context.scene.render.fps = 24
        clips(rig)
        if sheet and team == 'a':
            render_sheet(sheet, [ob], [(0.0, 0.1), (math.pi / 2, 0.1)], size=2.6, center=(0, 0, 1.0))
        export_glb(os.path.join(out, 'soldier_%s.glb' % team))


if __name__ == '__main__':
    main()
