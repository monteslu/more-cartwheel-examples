# A portrait check render of one generated fighter's head (CPU path tracing, no GPU use), with expressions set by name.
# flatpak run --filesystem=home org.blender.Blender --background --factory-startup --python <abs>/closeup.py -- \
#   <model.glb> <out.png> <width> <height> <yaw degrees> <distance m> [target=weight,...] [jaw=radians]
import bpy, sys, math
from mathutils import Vector, Quaternion

argv = sys.argv[sys.argv.index('--') + 1:]
glb, out, W, H, yaw, dist = argv[0], argv[1], int(argv[2]), int(argv[3]), float(argv[4]), float(argv[5])
keys = {}
jaw = 0.0
for a in argv[6:]:
    for kv in a.split(','):
        if '=' in kv:
            k, v = kv.split('=')
            if k == 'jaw': jaw = float(v)
            else: keys[k] = float(v)
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
bpy.ops.import_scene.gltf(filepath=glb)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
for o in bpy.data.objects:
    if o.type == 'MESH' and o.data.shape_keys:
        for kb in o.data.shape_keys.key_blocks:
            if kb.name in keys: kb.value = keys[kb.name]
if jaw and 'Jaw' in arm.pose.bones:
    pb = arm.pose.bones['Jaw']; pb.rotation_mode = 'QUATERNION'; pb.rotation_quaternion = Quaternion((1, 0, 0), jaw)
bpy.context.view_layer.update()
eye = arm.matrix_world @ arm.data.bones['EyeL'].head_local
eyer = arm.matrix_world @ arm.data.bones['EyeR'].head_local
tgt = (eye + eyer) / 2 + Vector((0, 0, -0.03))
ya = math.radians(yaw)
cam = bpy.data.cameras.new('c'); cam.lens = 85
co = bpy.data.objects.new('c', cam); sc.collection.objects.link(co)
# glTF +z (front) is Blender -y
co.location = tgt + Vector((dist * math.sin(ya), -dist * math.cos(ya), 0.03))
d = tgt - co.location
co.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
sc.camera = co
def light(name, kind, energy, rot, color, size=1.0):
    l = bpy.data.lights.new(name, kind); l.energy = energy; l.color = color
    if kind == 'SUN': l.angle = math.radians(size)
    o = bpy.data.objects.new(name, l); o.rotation_euler = rot; sc.collection.objects.link(o)
light('key', 'SUN', 3.0, (math.radians(55), 0, math.radians(-40)), (1.0, 0.92, 0.84), 4)
light('rim', 'SUN', 4.5, (math.radians(75), 0, math.radians(165)), (0.7, 0.8, 1.0), 2)
light('fill', 'SUN', 0.7, (math.radians(70), 0, math.radians(55)), (0.75, 0.85, 1.0), 25)
w = bpy.data.worlds.new('w'); w.use_nodes = True
bg = next(n for n in w.node_tree.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.1, 0.11, 0.14, 1); bg.inputs[1].default_value = 0.7
sc.world = w
try:
    sc.render.engine = 'CYCLES'
except TypeError:
    pass
sc.cycles.device = 'CPU'; sc.cycles.samples = 24; sc.cycles.use_denoising = True
sc.render.resolution_x = W; sc.render.resolution_y = H
sc.render.filepath = out
sc.render.threads_mode = 'FIXED'; sc.render.threads = 4
bpy.ops.render.render(write_still=True)
