# The armoured fighter, one per team colour: one skinned mesh with one material, on the full humanoid skeleton of the
# motion libraries the game's clips come from (Quaternius' Universal Animation Library mannequin, CC0: the pelvis,
# three spine joints, the neck and head, the shoulders, every finger joint, the toes; 65 bones), so the clips in
# anim/ual.glb (tools/blender/anims.py) retarget onto it at pack time (models/soldier_*.glb.cwimport).
#   blender -b --python soldier.py -- <models dir> <ual1 AL_Standard.fbx> <out/hold.json> [sheet dir]
# The body is the library's mannequin (CC0), decimated, worn as the fabric suit (matte); over it this script models the
# kit from primitives, each piece riding one bone: the helmet with its brim, ear cups, wrap visor and crest, a face
# mask, the collar, the chest and back plates, the stomach plates and magazine pouches, the radio, the shoulder
# straps, the pack with its rolled mat and antenna, the belt with its pouches, the groin plate, pauldrons, elbow
# pads, bracers, gloves, thigh plates, a holster, knee pads, shin guards, boots with soles and toe caps, and the
# rifle, which rides the right hand where anims.py's hold puts it (its muzzle and stock are two extra bones,
# `rifle` and `muzzle`, that the game reads for the muzzle flash and the aim). Every piece takes its colour from one
# small palette texture (base colour, roughness and metal, glow), so a soldier draws as one primitive. The model
# faces -Y here (+Z in glTF) like the rig; the game turns it to face its yaw.
import bpy, bmesh, math, sys, os, json
from mathutils import Vector, Matrix, Quaternion
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

TEAMS = {'a': (0.15, 0.55, 1.0), 'b': (1.0, 0.25, 0.15)}
SCALE = 0.95  # the mannequin is 1.83 m; the soldier's eyes at the first person eye height (1.6 m)
BODY_RATIO = 0.42  # the mannequin's 13.7k triangles decimated (most of it is under the kit)


def log(*a):
    print('[soldier]', *a, flush=True)


def srgb(c):
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


# the palette: cell -> (base colour linear, roughness, metal, glow linear); 4 x 4 cells of 16 px
CELLS = ['suit', 'webbing', 'armor', 'plate', 'rubber', 'trim', 'visor', 'gunmetal', 'glove', 'joint']


def palette(col):
    return {
        'suit': ((0.055, 0.06, 0.055), 0.95, 0.0, None),
        'webbing': ((0.11, 0.105, 0.085), 0.95, 0.0, None),
        'armor': ((0.06 + col[0] * 0.34, 0.06 + col[1] * 0.32, 0.06 + col[2] * 0.34), 0.42, 0.25, None),
        'plate': ((0.24, 0.25, 0.27), 0.5, 0.55, None),
        'rubber': ((0.03, 0.03, 0.03), 0.8, 0.0, None),
        'trim': (col, 0.4, 0.0, tuple(c * 2.2 / 3.0 for c in col)),
        'visor': ((col[0] * 0.15, col[1] * 0.15, col[2] * 0.15), 0.1, 0.0, col),
        'gunmetal': ((0.06, 0.06, 0.065), 0.4, 0.85, None),
        'glove': ((0.045, 0.04, 0.035), 0.75, 0.0, None),
        'joint': ((0.075, 0.08, 0.075), 0.9, 0.0, None),
    }


def cell_uv(name):
    i = CELLS.index(name)
    return ((i % 4) + 0.5) / 4.0, 1.0 - ((i // 4) + 0.5) / 4.0


def make_material(team, col):
    P = palette(col)
    N = 64
    imgs = {}
    for kind, cs in (('base', 'sRGB'), ('mr', 'Non-Color'), ('glow', 'sRGB')):
        im = bpy.data.images.new('soldier_%s_%s' % (team, kind), N, N, alpha=False)
        im.colorspace_settings.name = cs
        px = [0.0] * (N * N * 4)
        for i, nm in enumerate(CELLS):
            base, rough, metal, glow = P[nm]
            if kind == 'base':
                c = [srgb(x) for x in base]
            elif kind == 'mr':
                c = [1.0, rough, metal]
            else:
                c = [srgb(x) for x in glow] if glow else [0.0, 0.0, 0.0]
            cx, cy = i % 4, i // 4
            for y in range(N // 4):
                row = N - 1 - (cy * (N // 4) + y)  # images are stored bottom row first
                for x in range(N // 4):
                    k = (row * N + cx * (N // 4) + x) * 4
                    px[k:k + 4] = [c[0], c[1], c[2], 1.0]
        im.pixels = px
        im.pack()
        imgs[kind] = im
    m = bpy.data.materials.new('soldier_' + team)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    def tex(im):
        t = nt.nodes.new('ShaderNodeTexImage')
        t.image = im
        t.interpolation = 'Linear'
        return t
    tb, tm, tg = tex(imgs['base']), tex(imgs['mr']), tex(imgs['glow'])
    nt.links.new(tb.outputs['Color'], bsdf.inputs['Base Color'])
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(tm.outputs['Color'], sep.inputs['Color'])
    nt.links.new(sep.outputs['Green'], bsdf.inputs['Roughness'])
    nt.links.new(sep.outputs['Blue'], bsdf.inputs['Metallic'])
    nt.links.new(tg.outputs['Color'], bsdf.inputs['Emission Color'])
    bsdf.inputs['Emission Strength'].default_value = 3.0
    return m


def paint(ob, cell):
    me = ob.data
    if not me.uv_layers:
        me.uv_layers.new(name='UVMap')
    uv = me.uv_layers.active.data
    u, v = cell_uv(cell)
    for l in uv:
        l.uv = (u, v)


def build(team, fbx, hold):
    reset()
    col = TEAMS[team]
    bpy.ops.import_scene.fbx(filepath=fbx)
    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    rig = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    body = [o for o in bpy.data.objects if o.type == 'MESH'][0]
    rig.animation_data_clear()
    rig.name = 'Rig'
    body.name = 'Soldier'
    mat = make_material(team, col)
    B = {b.name: b for b in rig.data.bones}
    J = lambda n: (rig.matrix_world @ B[n].head_local).copy()

    # the body: its own colours by the bone each vertex follows most (hands gloved, the joint rings as seams)
    gi = {g.index: g.name for g in body.vertex_groups}
    me = body.data
    if not me.uv_layers:
        me.uv_layers.new(name='UVMap')
    jm = [i for i, m in enumerate(me.materials) if m and 'Joint' in m.name]
    vcell = []
    for v in me.vertices:
        g = max(v.groups, key=lambda g: g.weight, default=None)
        n = gi.get(g.group, '') if g else ''
        vcell.append('glove' if (n.startswith('hand_') or n.split('_')[0] in ('index', 'middle', 'ring', 'pinky', 'thumb')) else 'suit')
    uvd = me.uv_layers.active.data
    for p in me.polygons:
        for li in p.loop_indices:
            c = vcell[me.loops[li].vertex_index]
            if p.material_index in jm and c == 'suit':
                c = 'joint'
            uvd[li].uv = cell_uv(c)
    me.materials.clear()
    me.materials.append(mat)
    for p in me.polygons:
        p.material_index = 0
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    dm = body.modifiers.new('dec', 'DECIMATE')
    dm.ratio = BODY_RATIO
    # (the armature modifier stays last: apply the decimation alone)
    bpy.ops.object.modifier_move_to_index(modifier='dec', index=0)
    bpy.ops.object.modifier_apply(modifier='dec')
    body.select_set(False)

    parts = []

    def add(ob, bone, cell):
        paint(ob, cell)
        ob.data.materials.clear()
        ob.data.materials.append(mat)
        g = ob.vertex_groups.new(name=bone)
        apply_xform(ob)
        g.add([v.index for v in ob.data.vertices], 1.0, 'REPLACE')
        parts.append(ob)
        return ob

    def box(name, size, loc, bone, cell, bevel=0.015, segs=2, rot=(0, 0, 0)):
        return add(bevel_box(name, size, loc, bevel, 2 if bevel >= 0.03 else 1, None, rot), bone, cell)

    def ball(name, r, loc, scale, bone, cell, seg=16, rings=10):
        return add(sphere(name, r, loc, scale, None, seg, rings), bone, cell)

    def tube(name, r, depth, loc, bone, cell, rot=(0, 0, 0), verts=12, r2=None):
        return add(cyl(name, r, depth, loc, rot, verts, None, 0.0, r2), bone, cell)

    # the body's landmarks (the rig at rest: a T-pose facing -Y, its left at +X)
    head_c = Vector((0, -0.003, 1.70))
    chest_f = -0.125   # the chest's front (y)
    # --- the hips: a belt of webbing with pouches, a groin plate
    box('belt', (0.30, 0.25, 0.05), (0, 0.0, 1.06), 'pelvis', 'webbing', 0.02, 2)
    box('buckle', (0.06, 0.02, 0.04), (0, -0.128, 1.06), 'pelvis', 'plate', 0.008, 1)
    box('groin', (0.13, 0.04, 0.12), (0, -0.118, 0.96), 'pelvis', 'armor', 0.02, 2, rot=(-0.12, 0, 0))
    for i, x in enumerate((-0.11, 0.11, -0.145, 0.145)):
        y = -0.12 if i < 2 else 0.06
        box('beltpouch%d' % i, (0.07, 0.05, 0.08), (x, y, 1.03), 'pelvis', 'webbing', 0.012, 2)
    # --- the trunk: two stomach plates, a shaped chest plate and a back plate, a chest rig of pouches, a radio, straps
    box('abs1', (0.22, 0.04, 0.07), (0, -0.112, 1.13), 'spine_01', 'armor', 0.02, 2)
    box('abs2', (0.25, 0.04, 0.075), (0, -0.135, 1.215), 'spine_02', 'armor', 0.02, 2)
    box('chestplate', (0.34, 0.06, 0.22), (0, chest_f - 0.03, 1.40), 'spine_03', 'armor', 0.035, 3, rot=(-0.12, 0, 0))
    box('backplate', (0.33, 0.05, 0.26), (0, 0.13, 1.38), 'spine_03', 'armor', 0.03, 3, rot=(0.08, 0, 0))
    box('chesttrim', (0.2, 0.01, 0.025), (0, chest_f - 0.052, 1.495), 'spine_03', 'trim', 0.004, 1, rot=(-0.12, 0, 0))
    for i, x in enumerate((-0.085, 0.0, 0.085)):
        box('magpouch%d' % i, (0.07, 0.05, 0.10), (x, chest_f - 0.055, 1.27), 'spine_02', 'webbing', 0.012, 2)
        box('magflap%d' % i, (0.074, 0.054, 0.022), (x, chest_f - 0.057, 1.322), 'spine_02', 'webbing', 0.008, 1)
    box('radio', (0.055, 0.045, 0.09), (0.115, chest_f - 0.07, 1.43), 'spine_03', 'webbing', 0.012, 2)
    tube('radio_ant', 0.005, 0.13, (0.13, chest_f - 0.07, 1.53), 'spine_03', 'rubber', verts=6)
    for x in (-0.11, 0.11):
        box('strap%.2f' % x, (0.05, 0.27, 0.025), (x, 0.0, 1.535), 'spine_03', 'webbing', 0.008, 1)
    box('collar', (0.2, 0.17, 0.05), (0, 0.0, 1.535), 'spine_03', 'armor', 0.02, 2)
    # --- the pack: a bag with a lid, a rolled mat under it, an antenna
    box('pack', (0.25, 0.12, 0.28), (0, 0.22, 1.36), 'spine_03', 'webbing', 0.03, 3)
    box('packlid', (0.26, 0.13, 0.05), (0, 0.22, 1.51), 'spine_03', 'webbing', 0.015, 2)
    tube('packroll', 0.042, 0.27, (0, 0.23, 1.19), 'spine_03', 'plate', rot=(0, math.pi / 2, 0))
    box('packtrim', (0.06, 0.012, 0.15), (0, 0.282, 1.36), 'spine_03', 'trim', 0.004, 1)
    tube('antenna', 0.005, 0.4, (-0.09, 0.24, 1.68), 'spine_03', 'rubber', verts=6)
    # --- the head: a helmet with a brim, ear cups and a wrap visor; a face mask under it; a crest
    ball('helmet', 0.128, (0, 0.008, 1.735), (0.92, 1.04, 0.9), "Head", "armor", 18, 10)
    tube('brim', 0.124, 0.028, (0, 0.008, 1.695), 'Head', 'armor', verts=22, r2=0.117)
    box('neckguard', (0.17, 0.05, 0.07), (0, 0.11, 1.65), 'Head', 'armor', 0.02, 2, rot=(0.35, 0, 0))
    for sx in (-1, 1):
        tube('earcup%d' % sx, 0.04, 0.035, (sx * 0.098, 0.005, 1.69), 'Head', 'plate', rot=(0, math.pi / 2, 0), verts=12)
    box('visor', (0.17, 0.05, 0.05), (0, -0.112, 1.705), 'Head', 'visor', 0.02, 3)
    for sx in (-1, 1):
        box('visorside%d' % sx, (0.045, 0.07, 0.045), (sx * 0.083, -0.075, 1.705), 'Head', 'visor', 0.015, 2, rot=(0, 0, -sx * 0.6))
    box('mask', (0.12, 0.07, 0.065), (0, -0.095, 1.625), 'Head', 'suit', 0.025, 2)
    box('crest', (0.026, 0.16, 0.026), (0, 0.0, 1.85), 'Head', 'trim', 0.01, 2)
    # --- shoulders and arms (along +-X at rest), hands gloved; thighs, knees, shins, boots
    for s, sg in (('l', 1), ('r', -1)):
        sh = J('upperarm_' + s)
        el = J('lowerarm_' + s)
        wr = J('hand_' + s)
        ball('pauld_' + s, 0.085, (sh.x + sg * 0.03, sh.y + 0.065, sh.z + 0.045), (1.25, 1.05, 0.72), "upperarm_" + s, "armor", 12, 7)
        box('pauldtrim_' + s, (0.12, 0.025, 0.012), (sh.x + sg * 0.035, sh.y + 0.065, sh.z + 0.105), 'upperarm_' + s, 'trim', 0.005, 1)
        ball('elbow_' + s, 0.042, (el.x, el.y + 0.075, el.z + 0.005), (1, 1, 1), "lowerarm_" + s, "plate", 8, 6)
        box('bracer_' + s, (0.15, 0.085, 0.075), ((el.x + wr.x) / 2 + sg * 0.02, wr.y + 0.005, wr.z - 0.005), 'lowerarm_' + s, 'armor', 0.025, 2)
        box('cuff_' + s, (0.04, 0.09, 0.08), (wr.x - sg * 0.005, wr.y + 0.003, wr.z - 0.01), 'hand_' + s, 'glove', 0.02, 2)
        th = J('thigh_' + s)
        kn = J('calf_' + s)
        an = J('foot_' + s)
        box('thighplate_' + s, (0.035, 0.13, 0.19), (th.x + sg * 0.068, th.y - 0.005, 0.76), 'thigh_' + s, 'armor', 0.018, 2)
        if s == 'r':
            box('holster', (0.06, 0.1, 0.15), (th.x - 0.09, th.y + 0.03, 0.66), 'thigh_' + s, 'webbing', 0.015, 2)
        ball('kneepad_' + s, 0.058, (kn.x, kn.y - 0.065, kn.z), (1.0, 0.55, 1.1), "calf_" + s, "armor", 10, 6)
        box('shinguard_' + s, (0.085, 0.035, 0.22), (kn.x, kn.y - 0.048, 0.31), 'calf_' + s, 'plate', 0.016, 2)
        box('boot_' + s, (0.115, 0.25, 0.13), (an.x, -0.04, 0.068), 'foot_' + s, 'suit', 0.035, 3)
        box('sole_' + s, (0.12, 0.26, 0.03), (an.x, -0.04, 0.015), 'foot_' + s, 'rubber', 0.01, 1)
        box('toecap_' + s, (0.105, 0.06, 0.07), (an.x, -0.16, 0.05), 'ball_' + s, 'plate', 0.02, 2)
        box('bootcuff_' + s, (0.11, 0.12, 0.06), (an.x, 0.02, 0.15), 'calf_' + s, 'webbing', 0.02, 2)

    # --- the rifle, in its own frame (x its right, y along the bore, z up; the butt's centre at the origin), placed on
    # the right hand where the hold puts it
    rparts = []
    def rbox(name, size, loc, cell, bevel=0.012, segs=2, rot=(0, 0, 0)):
        ob = bevel_box(name, size, loc, bevel, segs, None, rot)
        paint(ob, cell)
        rparts.append(ob)
    def rcyl(name, r, depth, loc, cell):
        ob = cyl(name, r, depth, loc, (math.pi / 2, 0, 0), 10, None)
        paint(ob, cell)
        rparts.append(ob)
    rbox('stock', (0.05, 0.19, 0.085), (0, 0.095, -0.02), 'plate', 0.015)
    rbox('buttpad', (0.052, 0.02, 0.1), (0, 0.006, -0.022), 'rubber', 0.008, 1)
    rbox('receiver', (0.06, 0.30, 0.095), (0, 0.33, -0.005), 'gunmetal')
    rbox('grip', (0.034, 0.05, 0.10), (0, 0.205, -0.085), 'rubber', 0.01, 1, rot=(0.3, 0, 0))
    rbox('mag', (0.038, 0.07, 0.14), (0, 0.28, -0.11), 'gunmetal', 0.01, 1, rot=(-0.25, 0, 0))
    rbox('handguard', (0.055, 0.25, 0.068), (0, 0.555, -0.005), 'plate', 0.015)
    rcyl('barrel', 0.015, 0.2, (0, 0.77, 0.0), 'gunmetal')
    rcyl('muzzlebrake', 0.023, 0.06, (0, 0.86, 0.0), 'gunmetal')
    rbox('sight', (0.032, 0.08, 0.04), (0, 0.33, 0.065), 'gunmetal', 0.008, 1)
    rbox('rifleglow', (0.061, 0.11, 0.012), (0, 0.52, 0.03), 'visor', 0.004, 1)
    for o in rparts:
        apply_xform(o)
    rifle = join(rparts, 'Rifle')
    RH = Matrix(hold['rf_in_hand'])
    M = B['hand_r'].matrix_local @ RH  # the rifle frame at rest: the hand's rest times the rifle in the hand
    rifle.data.transform(M)
    rifle.data.materials.clear()
    rifle.data.materials.append(mat)
    g = rifle.vertex_groups.new(name='hand_r')
    g.add([v.index for v in rifle.data.vertices], 1.0, 'REPLACE')
    parts.append(rifle)

    # no piece floats: each one's nearest vertex to the body (or to the piece it sits on: the pack's lid, roll and
    # antenna on the pack, the trims on their plates) within a centimetre (logged; the build stops otherwise)
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()
    trees = {o.name: BVHTree.FromObject(o, dg) for o in [body] + parts}
    on = {'packlid': 'pack', 'packroll': 'pack', 'packtrim': 'pack', 'antenna': 'pack', 'buckle': 'belt', 'chesttrim': 'chestplate',
          'radio_ant': 'radio', 'brim': 'helmet', 'crest': 'helmet', 'visor': 'helmet', 'neckguard': 'helmet', 'magflap0': 'magpouch0',
          'magflap1': 'magpouch1', 'magflap2': 'magpouch2', 'Rifle': None}
    worst = 0.0
    for o in parts:
        base = o.name.split('.')[0]
        key = next((k for k in on if base.startswith(k)), None)
        if key and on[key] is None:
            continue
        others = [trees[on[key]]] if key and on[key] in trees else [trees[body.name]] + [trees[q.name] for q in parts if q is not o and not q.name.startswith('Rifle')]
        # touching: an edge of the piece crosses the surface (it sits into it), else its nearest vertex's distance
        vs = o.data.vertices
        cross = any(t.ray_cast(vs[e.vertices[0]].co, vs[e.vertices[1]].co - vs[e.vertices[0]].co, (vs[e.vertices[1]].co - vs[e.vertices[0]].co).length)[0] is not None
                    for e in o.data.edges for t in others)
        gap = 0.0 if cross else min(min((t.find_nearest(v.co)[3] for t in others), default=1) for v in vs)
        worst = max(worst, gap)
        if gap > 0.01:
            log('FLOATS', o.name, '%.1f cm' % (gap * 100))
    log('pieces', len(parts), 'largest gap to what they sit on %.2f cm' % (worst * 100))
    assert worst <= 0.01, 'a piece of kit floats'

    # everything into the body: one mesh, one material
    for o in parts:
        o.data.materials.clear()
        o.data.materials.append(mat)
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
        o.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active

    # the rifle's two bones: its stock (along the bore) and its muzzle, under the right hand
    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    eb = rig.data.edit_bones
    hr = eb['hand_r']
    p0 = (M @ Vector((0, 0, 0, 1))).xyz
    pf = (M @ Vector((0, 0.1, 0, 1))).xyz
    pm = (M @ Vector((0, 0.89, 0, 1))).xyz
    b1 = eb.new('rifle')
    b1.head, b1.tail, b1.parent, b1.use_deform = p0, pf, hr, False
    b2 = eb.new('muzzle')
    b2.head, b2.tail, b2.parent, b2.use_deform = pm, pm + (pf - p0), hr, False
    bpy.ops.object.mode_set(mode='OBJECT')
    rig.select_set(False)

    # the soldier's size: the rig and the mesh scaled together
    rig.scale = (SCALE, SCALE, SCALE)
    bpy.context.view_layer.update()
    for o in (rig, body):
        o.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    bpy.ops.object.select_all(action='DESELECT')
    me = body.data
    me.calc_loop_triangles()
    log(team, 'triangles', len(me.loop_triangles), 'vertices', len(me.vertices), 'bones', len(rig.data.bones),
        'top %.3f' % max((body.matrix_world @ v.co).z for v in me.vertices))
    return body, rig


def main():
    a = args()
    out, fbx, holdp = a[0], a[1], a[2]
    sheet = a[3] if len(a) > 3 else None
    hold = json.load(open(holdp))
    for team in TEAMS:
        body, rig = build(team, fbx, hold)
        bpy.context.scene.render.fps = 30
        if sheet:
            render_sheet(os.path.join(sheet, 'soldier_%s.png' % team), [body], [(0.5, 0.1), (math.pi / 2, 0.1), (math.pi, 0.1)], size=2.2, center=(0, 0, 0.95), color='TEXTURE')
        path = os.path.join(out, 'soldier_%s.glb' % team)
        bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', export_yup=True, export_texcoords=True, export_normals=True,
                                  export_materials='EXPORT', export_cameras=False, export_lights=False, export_skins=True,
                                  export_animations=False)
        log('wrote', path, os.path.getsize(path), 'bytes')


if __name__ == '__main__':
    main()
