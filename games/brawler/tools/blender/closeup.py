# Close-up renders of one character's face or hand at one clip time, from several angles, in one contact sheet row
# (CPU path tracing, no GPU use). Checks the sculpted faces, expressions and hand poses.
# flatpak run --filesystem=home org.blender.Blender --background --factory-startup --python <abs>/closeup.py -- \
#   <models dir> <out.png> <name> <clip> <fraction 0..1> <size px> <target: head|handL|handR|legs|body> <yaw degrees,...> [clay]
import bpy, sys, math, os
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
mdir, out, name, clip, frac, S, target, yaws = argv[0], argv[1], argv[2], argv[3], float(argv[4]), int(argv[5]), argv[6], [float(y) for y in argv[7].split(',')]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
bpy.ops.import_scene.gltf(filepath=os.path.join(mdir, name + '.glb'))
arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
arm.animation_data_create()
act = None
for a in bpy.data.actions:
    if a.name == clip or a.name.startswith(clip + '_') or a.name.startswith(clip + '.'):
        act = a
        break
arm.animation_data.action = act or bpy.data.actions[0]
try:
    arm.animation_data.action_slot = arm.animation_data.action.slots[0]
except Exception:
    pass
f0, f1 = arm.animation_data.action.frame_range
sc.frame_set(int(f0 + (f1 - f0) * frac))
# SHAPES="grin:1,blink:0.5" sets the face's morph targets (the game drives them by name; the clips only key the jaw)
for kv in filter(None, os.environ.get('SHAPES', '').split(',')):
    k, v = kv.split(':')
    for o in bpy.data.objects:
        if o.type == 'MESH' and o.data.shape_keys and k in o.data.shape_keys.key_blocks:
            o.data.shape_keys.key_blocks[k].value = float(v)
if len(argv) > 8 and argv[8] == 'clay':  # every material grey: shows the geometry alone
    clay = bpy.data.materials.new('clay')
    for o in bpy.data.objects:
        if o.type == 'MESH':
            for sl in o.material_slots: sl.material = clay
bone = {'head': 'Head', 'handL': 'HandL', 'handR': 'HandR', 'legs': 'Hips', 'body': 'Spine'}[target]
pb = arm.pose.bones[bone]
m = arm.matrix_world @ pb.matrix
if target in ('legs', 'body'):  # the legs and crotch (leg webbing, coat skirts) or the whole figure
    hips = arm.matrix_world @ pb.head
    focus = hips + Vector((0, 0, -0.35 if target == 'legs' else 0.05))
    dist, lens = (2.9, 50) if target == 'legs' else (3.6, 50)
elif target == 'head':
    focus = (arm.matrix_world @ pb.head) + Vector((0, 0, 0.14))
    dist, lens = 0.95, 85
else:
    focus = arm.matrix_world @ arm.pose.bones['Fing' + target[-1] + '1'].head
    dist, lens = 0.55, 85

def light(n, kind, energy, rot, color, size=1.0):
    l = bpy.data.lights.new(n, kind); l.energy = energy; l.color = color
    if kind == 'SUN': l.angle = math.radians(size)
    o = bpy.data.objects.new(n, l); o.rotation_euler = rot; sc.collection.objects.link(o)
light('key', 'SUN', 3.0, (math.radians(55), 0, math.radians(-30)), (1.0, 0.93, 0.85), 3)
light('rim', 'SUN', 3.0, (math.radians(70), 0, math.radians(165)), (0.6, 0.75, 1.0), 2)
light('fill', 'SUN', 0.9, (math.radians(75), 0, math.radians(70)), (0.75, 0.82, 1.0), 20)
w = bpy.data.worlds.new('w'); w.use_nodes = True
bg = next(n for n in w.node_tree.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.1, 0.11, 0.13, 1); bg.inputs[1].default_value = 0.5
sc.world = w
cam = bpy.data.cameras.new('c'); cam.lens = lens
co = bpy.data.objects.new('c', cam); sc.collection.objects.link(co); sc.camera = co
try:
    sc.render.engine = 'CYCLES'
except TypeError:
    pass
sc.cycles.device = 'CPU'; sc.cycles.samples = 20; sc.cycles.use_denoising = True
sc.render.resolution_x = S; sc.render.resolution_y = S
sc.render.threads_mode = 'FIXED'; sc.render.threads = 4
base, ext = os.path.splitext(out)
for i, yaw in enumerate(yaws):
    ya = math.radians(yaw)
    # the model faces -Y in Blender after the glTF import (+Z forward in glTF)
    co.location = focus + Vector((dist * math.sin(ya), -dist * math.cos(ya), 0.03))
    d = focus - co.location
    co.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = '%s_%d%s' % (base, i, ext)
    bpy.ops.render.render(write_still=True)
