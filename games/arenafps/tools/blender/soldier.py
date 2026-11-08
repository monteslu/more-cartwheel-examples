# The armoured fighter: one skinned mesh, 15 bones, clips idle / run / die. Usage (see build.sh):
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
    # cloth is matte (a fabric suit and webbing); the armour is painted plate in the team colour, the trims glow it a little
    suit = material('suit', (0.055, 0.06, 0.055), 0.95, 0.0)
    web = material('webbing', (0.11, 0.105, 0.085), 0.95, 0.0)
    armor = material('armor', (0.06 + col[0] * 0.34, 0.06 + col[1] * 0.32, 0.06 + col[2] * 0.34), 0.42, 0.25)
    plate = material('plate', (0.24, 0.25, 0.27), 0.5, 0.55)
    rubber = material('rubber', (0.03, 0.03, 0.03), 0.8, 0.0)
    trim = material('trim', col, 0.4, 0.0, emit=col, emit_strength=2.2)
    visor = material('visor', (col[0] * 0.15, col[1] * 0.15, col[2] * 0.15), 0.1, 0.0, emit=col, emit_strength=3.0)
    gun = material('gunmetal', (0.06, 0.06, 0.065), 0.4, 0.85)
    parts = []   # (object, bone name or weights fn)

    def add(ob, bone):
        g = ob.vertex_groups.new(name=bone)
        g.add([v.index for v in ob.data.vertices], 1.0, 'REPLACE')
        parts.append(ob)
        return ob

    def rbox(name, size, loc, bone, mat, bevel=0.015, segs=2, rot=(0, 0, 0)):
        return add(bevel_box(name, size, loc, bevel, segs, mat, rot), bone)

    # --- hips: a belt of webbing with pouches, a groin plate, the seat
    rbox('pelvis', (0.30, 0.20, 0.17), (0, 0, 1.0), 'hips', suit, 0.05, 3)
    rbox('belt', (0.33, 0.23, 0.05), (0, 0, 1.08), 'hips', web, 0.015, 2)
    rbox('buckle', (0.06, 0.02, 0.04), (0, 0.118, 1.08), 'hips', plate, 0.008, 1)
    rbox('groin', (0.13, 0.04, 0.12), (0, 0.105, 0.98), 'hips', armor, 0.02, 2)
    for i, x in enumerate((-0.12, 0.12, -0.155, 0.155)):
        y = 0.105 if i < 2 else -0.06
        rbox('beltpouch%d' % i, (0.07, 0.05, 0.08), (x, y, 1.05), 'hips', web, 0.012, 2)
    # --- the waist and torso: a fabric waist, a shaped chest plate over it, two stomach plates, a chest rig of pouches
    add(cyl('waist', 0.13, 0.22, (0, 0, 1.22), verts=16, mat=suit, r2=0.15), 'spine')
    rbox('abs1', (0.24, 0.04, 0.07), (0, 0.13, 1.19), 'spine', armor, 0.02, 2)
    rbox('abs2', (0.26, 0.04, 0.07), (0, 0.135, 1.265), 'spine', armor, 0.02, 2)
    add(sphere('torso', 0.2, (0, 0.0, 1.46), (1.12, 0.72, 0.85), suit, seg=20, rings=12), 'chest')
    rbox('chestplate', (0.38, 0.06, 0.24), (0, 0.13, 1.47), 'chest', armor, 0.035, 3, rot=(0.12, 0, 0))
    rbox('backplate', (0.36, 0.05, 0.28), (0, -0.14, 1.46), 'chest', armor, 0.03, 3, rot=(-0.08, 0, 0))
    rbox('chesttrim', (0.22, 0.01, 0.025), (0, 0.168, 1.565), 'chest', trim, 0.004, 1, rot=(0.12, 0, 0))
    for i, x in enumerate((-0.09, 0.0, 0.09)):   # three magazine pouches across the stomach
        rbox('magpouch%d' % i, (0.075, 0.05, 0.11), (x, 0.17, 1.33), 'chest', web, 0.012, 2)
        rbox('magflap%d' % i, (0.078, 0.054, 0.025), (x, 0.172, 1.385), 'chest', web, 0.008, 1)
    rbox('radio', (0.06, 0.045, 0.1), (0.13, 0.15, 1.5), 'chest', web, 0.012, 2)
    add(cyl('radio_ant', 0.006, 0.14, (0.15, 0.15, 1.6), verts=6, mat=rubber), 'chest')
    for x in (-0.12, 0.12):   # shoulder straps over the plates
        rbox('strap%.2f' % x, (0.05, 0.3, 0.025), (x, 0.0, 1.62), 'chest', web, 0.008, 1, rot=(0, 0, 0))
    add(cyl('collar', 0.085, 0.07, (0, 0, 1.62), verts=16, mat=armor, r2=0.075), 'neck')
    add(cyl('neck', 0.05, 0.1, (0, 0.005, 1.66), verts=12, mat=suit), 'neck')
    # --- the pack: a frame, a bag with a rolled mat, an antenna
    rbox('pack', (0.26, 0.12, 0.3), (0, -0.23, 1.42), 'chest', web, 0.03, 3)
    rbox('packlid', (0.27, 0.13, 0.05), (0, -0.23, 1.58), 'chest', web, 0.015, 2)
    add(cyl('packroll', 0.045, 0.28, (0, -0.24, 1.25), rot=(0, math.pi / 2, 0), verts=12, mat=plate), 'chest')
    rbox('packtrim', (0.06, 0.012, 0.16), (0, -0.292, 1.42), 'chest', trim, 0.004, 1)
    add(cyl('antenna', 0.006, 0.42, (-0.1, -0.26, 1.78), verts=6, mat=rubber), 'chest')
    # --- the head: a helmet with a brim, ear cups and a wrap visor; a dark face mask under it
    add(sphere('helmet', 0.135, (0, -0.005, 1.8), (0.95, 1.08, 0.92), armor, seg=24, rings=14), 'head')
    add(cyl('brim', 0.138, 0.03, (0, -0.005, 1.765), verts=24, mat=armor, r2=0.13), 'head')
    rbox('neckguard', (0.2, 0.05, 0.07), (0, -0.12, 1.73), 'head', armor, 0.02, 2, rot=(-0.35, 0, 0))
    for sx in (-1, 1):
        add(cyl('earcup%d' % sx, 0.045, 0.04, (sx * 0.13, 0.0, 1.77), rot=(0, math.pi / 2, 0), verts=14, mat=plate), 'head')
    rbox('visor', (0.2, 0.05, 0.055), (0, 0.118, 1.79), 'head', visor, 0.02, 3)
    for sx in (-1, 1):
        rbox('visorside%d' % sx, (0.05, 0.08, 0.05), (sx * 0.1, 0.075, 1.79), 'head', visor, 0.015, 2, rot=(0, 0, sx * 0.6))
    rbox('mask', (0.13, 0.08, 0.07), (0, 0.085, 1.715), 'head', suit, 0.025, 2)
    rbox('crest', (0.03, 0.18, 0.03), (0, -0.01, 1.92), 'head', trim, 0.01, 2)
    # --- shoulders, elbows, forearms, gloves; knees, shins, thighs, boots
    for s, sg in (('l', 1), ('r', -1)):
        add(sphere('pauld_' + s, 0.095, (sg * 0.27, 0, 1.585), (1.15, 1.1, 0.7), armor, seg=18, rings=10), 'upper_' + s)
        rbox('pauld2_' + s, (0.1, 0.17, 0.04), (sg * 0.305, 0, 1.52), 'upper_' + s, armor, 0.015, 2, rot=(0, sg * 0.5, 0))
        rbox('pauldtrim_' + s, (0.03, 0.14, 0.015), (sg * 0.29, 0, 1.645), 'upper_' + s, trim, 0.005, 1)
        hip = Vector(BONES['thigh_' + s][1]); knee = Vector(BONES['thigh_' + s][2]); ankle = Vector(BONES['shin_' + s][2])
        rbox('thighplate_' + s, (0.04, 0.15, 0.2), (sg * 0.225, 0.01, 0.8), 'thigh_' + s, armor, 0.018, 2)
        if s == 'r':
            rbox('holster', (0.06, 0.1, 0.16), (sg * 0.215, -0.02, 0.7), 'thigh_' + s, web, 0.015, 2)
        add(sphere('kneepad_' + s, 0.062, (sg * 0.14, 0.07, 0.53), (1.0, 0.55, 1.1), armor, seg=14, rings=8), 'shin_' + s)
        rbox('shinguard_' + s, (0.09, 0.04, 0.24), (sg * 0.14, 0.06, 0.3), 'shin_' + s, plate, 0.018, 2)
        rbox('boot_' + s, (0.11, 0.25, 0.14), (sg * 0.14, 0.04, 0.075), 'foot_' + s, suit, 0.035, 3)
        rbox('sole_' + s, (0.115, 0.26, 0.03), (sg * 0.14, 0.04, 0.015), 'foot_' + s, rubber, 0.01, 1)
        rbox('toecap_' + s, (0.1, 0.06, 0.07), (sg * 0.14, 0.15, 0.055), 'foot_' + s, plate, 0.02, 2)
        sh = Vector(BONES['upper_' + s][1]); el = Vector(BONES['upper_' + s][2]); hand = Vector(BONES['fore_' + s][2])
        add(sphere('elbow_' + s, 0.05, tuple(el + Vector((sg * 0.02, -0.03, 0))), (1, 1, 1), plate, seg=12, rings=8), 'fore_' + s)
        mid = el.lerp(hand, 0.45)
        add(cyl('bracer_' + s, 0.055, 0.15, tuple(mid), rot=((hand - el).to_track_quat('Z', 'Y').to_euler()), verts=12, mat=armor, r2=0.05), 'fore_' + s)
        add(sphere('glove_' + s, 0.05, tuple(hand), (1.0, 1.3, 0.9), rubber, seg=12, rings=8), 'fore_' + s)

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

    def arm(s, shoulder, elbow, hand, ra=0.062, rb=0.045):
        n = 7
        pts = []; rad = []
        for i in range(n):
            t = i / (n - 1)
            pts.append(Vector(shoulder).lerp(Vector(elbow), t)); rad.append(ra + (ra * 0.85 - ra) * t)
        for i in range(1, n):
            t = i / (n - 1)
            pts.append(Vector(elbow).lerp(Vector(hand), t)); rad.append(ra * 0.85 + (rb - ra * 0.85) * t)
        tube('arm_' + s, pts, rad, suit, 'upper_' + s, 'fore_' + s, Vector(elbow))
    arm('l', BONES['upper_l'][1], BONES['upper_l'][2], BONES['fore_l'][2])
    arm('r', BONES['upper_r'][1], BONES['upper_r'][2], BONES['fore_r'][2])

    def leg(s, hip, knee, ankle):
        pts = []; rad = []
        for i in range(8):
            t = i / 7
            pts.append(Vector(hip).lerp(Vector(knee), t)); rad.append(0.1 - 0.025 * t)
        for i in range(1, 8):
            t = i / 7
            pts.append(Vector(knee).lerp(Vector(ankle), t)); rad.append(0.075 - 0.025 * t)
        tube('leg_' + s, pts, rad, suit, 'thigh_' + s, 'shin_' + s, Vector(knee), 0.12)
    leg('l', BONES['thigh_l'][1], BONES['thigh_l'][2], BONES['shin_l'][2])
    leg('r', BONES['thigh_r'][1], BONES['thigh_r'][2], BONES['shin_r'][2])

    # --- the rifle, held by both hands, rigid to the right forearm: receiver, magazine, shroud and barrel, stock, sight, grip
    rbox('rifle_body', (0.06, 0.34, 0.1), (-0.04, 0.36, 1.35), 'fore_r', gun, 0.012, 2)
    rbox('rifle_shroud', (0.055, 0.26, 0.07), (-0.04, 0.63, 1.36), 'fore_r', plate, 0.015, 2)
    add(cyl('rifle_barrel', 0.016, 0.22, (-0.04, 0.86, 1.365), rot=(math.pi / 2, 0, 0), verts=10, mat=gun), 'fore_r')
    add(cyl('rifle_muzzle', 0.024, 0.06, (-0.04, 0.97, 1.365), rot=(math.pi / 2, 0, 0), verts=10, mat=gun), 'fore_r')
    rbox('rifle_mag', (0.04, 0.07, 0.14), (-0.04, 0.42, 1.25), 'fore_r', gun, 0.01, 1, rot=(-0.25, 0, 0))
    rbox('rifle_grip', (0.035, 0.05, 0.1), (-0.04, 0.27, 1.27), 'fore_r', rubber, 0.01, 1, rot=(0.3, 0, 0))
    rbox('rifle_stock', (0.05, 0.2, 0.09), (-0.04, 0.1, 1.33), 'fore_r', plate, 0.015, 2)
    rbox('rifle_sight', (0.035, 0.08, 0.04), (-0.04, 0.38, 1.42), 'fore_r', gun, 0.008, 1)
    rbox('rifle_glow', (0.062, 0.12, 0.012), (-0.04, 0.5, 1.39), 'fore_r', visor, 0.004, 1)

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
