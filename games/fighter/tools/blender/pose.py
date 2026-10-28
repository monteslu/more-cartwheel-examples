# A check render of the fighters in the game's guard pose (the pose chars.c builds for standing), CPU path tracing.
# flatpak run --filesystem=home org.blender.Blender --background --factory-startup --python <abs>/pose.py -- \
#   <models dir> <out.png> <width> <height> <yaw degrees> <zoom> <centre height> [name ...]
# Rotations are the game's: per bone euler (x, y, z) applied as qy * qx * qz on top of the rest pose (identity), in the
# glTF frame (y up, +z the fighter's front, +x the fighter's left).
import bpy, sys, math, os
from mathutils import Matrix, Quaternion, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
mdir, out, W, H, yaw, zoom, cz = argv[0], argv[1], int(argv[2]), int(argv[3]), float(argv[4]), float(argv[5]), float(argv[6])
names = argv[7:] or ['bolt', 'grit', 'vex', 'mara']
STANCE = {'bolt': (0.12, 1.0), 'grit': (0.17, 0.8), 'vex': (0.14, 1.1), 'mara': (0.11, 1.2)}

def guard(stance, g):
    r = {
        'UpperLegR': (-0.35, 0, -stance), 'LowerLegR': (0.55, 0, 0), 'FootR': (-0.06, 0, 0),
        'UpperLegL': (0.2, 0, stance), 'LowerLegL': (0.45, 0, 0), 'FootL': (-0.195, 0, 0),
        'Spine': (0.08, 0.22, 0), 'Chest': (0.06, 0.1, 0), 'Head': (-0.1, -0.28, 0),
        'UpperArmR': (-1.15 * g, 0, -0.22), 'LowerArmR': (-1.5, 0, 0), 'UpperArmL': (-0.95 * g, 0, 0.3), 'LowerArmL': (-1.7, 0, 0),
    }
    f = 1.0
    k1, k2 = 0.25 + 1.3 * f, 0.3 + 1.45 * f
    for sd, sg in (('L', -1), ('R', 1)):
        for fn in ('Index', 'Middle', 'Ring', 'Pinky'):
            r[fn + '1' + sd] = (0.04 * f, 0, sg * k1); r[fn + '2' + sd] = (0, 0, sg * k2)
        r['Thumb' + sd] = (1.0 * f, sg * 0.15 * f, sg * (0.1 + 0.35 * f)); r['Thumb2' + sd] = (0.3 * f, 0, sg * (0.15 + 0.5 * f))
    return r

def q_game(e):
    x, y, z = e
    qx = Quaternion((math.cos(x / 2), math.sin(x / 2), 0, 0)); qy = Quaternion((math.cos(y / 2), 0, math.sin(y / 2), 0)); qz = Quaternion((math.cos(z / 2), 0, 0, math.sin(z / 2)))
    return qy @ qx @ qz

C = Matrix.Rotation(math.radians(90), 4, 'X')  # glTF y-up to Blender z-up
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
x = 0.0
step = 1.0
for n in names:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(mdir, n + '.glb'))
    new = [o for o in bpy.data.objects if o not in before]
    arm = [o for o in new if o.type == 'ARMATURE'][0]
    # the rest skeleton in glTF space from the pose bones' rest heads (armature space is Blender's frame)
    st, g = STANCE.get(n, (0.12, 1.0))
    rot = guard(st, g)
    rest = {b.name: (C.inverted() @ arm.data.bones[b.name].matrix_local) for b in arm.pose.bones}
    # glTF-space rest transforms: pure translations of each head
    glocal = {}
    for b in arm.data.bones:
        h = (C.inverted() @ b.matrix_local).to_translation()
        ph = (C.inverted() @ b.parent.matrix_local).to_translation() if b.parent else Vector((0, 0, 0))
        glocal[b.name] = h - ph
    posed = {}
    def walk(b):
        q = q_game(rot.get(b.name, (0, 0, 0)))
        t = glocal[b.name].copy()
        if b.name == 'Hips': t.y -= 0.07
        M = Matrix.Translation(t) @ q.to_matrix().to_4x4()
        posed[b.name] = (posed[b.parent.name] @ M) if b.parent else M
        for c in b.children: walk(c)
    for b in arm.data.bones:
        if b.parent is None: walk(b)
    for b in arm.data.bones:
        restg = Matrix.Translation((C.inverted() @ b.matrix_local).to_translation())
        pb = arm.pose.bones[b.name]
        pb.matrix = C @ posed[b.name] @ restg.inverted() @ C.inverted() @ b.matrix_local
        bpy.context.view_layer.update()
    for o in new:
        if o.parent is None: o.location.x += x
    x += step
cx = x / 2 - step / 2
cam = bpy.data.cameras.new('c'); cam.type = 'ORTHO'; cam.ortho_scale = max(2.4, x * 1.02) / zoom
co = bpy.data.objects.new('c', cam); sc.collection.objects.link(co)
ya = math.radians(yaw)
co.location = (cx + 12 * math.sin(ya), -12 * math.cos(ya), cz + 12 * math.tan(math.radians(4))); co.rotation_euler = (math.radians(86), 0, ya)
sc.camera = co
def light(name, kind, energy, rot, color, size=1.0):
    l = bpy.data.lights.new(name, kind); l.energy = energy; l.color = color
    if kind == 'SUN': l.angle = math.radians(size)
    o = bpy.data.objects.new(name, l); o.rotation_euler = rot; sc.collection.objects.link(o)
light('key', 'SUN', 3.2, (math.radians(50), 0, math.radians(-35)), (1.0, 0.9, 0.8), 3)
light('rim', 'SUN', 4.0, (math.radians(70), 0, math.radians(160)), (0.6, 0.75, 1.0), 2)
light('fill', 'SUN', 0.8, (math.radians(70), 0, math.radians(60)), (0.7, 0.8, 1.0), 20)
w = bpy.data.worlds.new('w'); w.use_nodes = True
bg = next(n for n in w.node_tree.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.12, 0.13, 0.16, 1); bg.inputs[1].default_value = 0.6
sc.world = w
bpy.ops.mesh.primitive_plane_add(size=60, location=(cx, 0, 0))
try:
    sc.render.engine = 'CYCLES'
except TypeError:
    pass
sc.cycles.device = 'CPU'; sc.cycles.samples = 16; sc.cycles.use_denoising = True
sc.render.resolution_x = W; sc.render.resolution_y = H
sc.render.filepath = out
sc.render.threads_mode = 'FIXED'; sc.render.threads = 4
bpy.ops.render.render(write_still=True)
