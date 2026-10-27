# A lineup render of the characters for checking the generated models (CPU path tracing, no GPU use).
# flatpak run --filesystem=home org.blender.Blender --background --factory-startup --python <abs>/lineup.py -- \
#   <models dir> <out.png> <clip> <fraction 0..1> <width> <height> <yaw degrees> [name ...]
import bpy, sys, math, os

argv = sys.argv[sys.argv.index('--') + 1:]
mdir, out, clip, frac, W, H, yaw = argv[0], argv[1], argv[2], float(argv[3]), int(argv[4]), int(argv[5]), float(argv[6])
names = argv[7:] or ['rook', 'mika', 'jet', 'thug', 'punk', 'runner', 'knifer', 'bruiser', 'foreman', 'matron', 'ringmaster', 'colossus']
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
x = 0.0
for n in names:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(mdir, n + '.glb'))
    new = [o for o in bpy.data.objects if o not in before]
    arm = [o for o in new if o.type == 'ARMATURE'][0]
    acts = [a for a in bpy.data.actions if a.name.startswith(clip) and n in a.name] or [a for a in bpy.data.actions if a.name == clip]
    if not acts:
        acts = [a for a in bpy.data.actions if a.name.split('_')[0] == clip.split('_')[0]]
    for o in new:
        if o.parent is None:
            o.location.x += x
    arm.animation_data_create()
    # the importer names actions per armature; pick the one for this clip on this armature
    a = None
    for act in bpy.data.actions:
        if act.name == clip or act.name.startswith(clip + '_') or act.name.startswith(clip + '.'):
            if a is None or (arm.name in act.name):
                a = act
    if a is None:
        a = bpy.data.actions[0]
    arm.animation_data.action = a
    try:
        arm.animation_data.action_slot = a.slots[0]
    except Exception:
        pass
    x += 1.1 if n not in ('bruiser', 'foreman', 'colossus') else 1.45
f0, f1 = 1, 1
for a in bpy.data.actions:
    f0, f1 = a.frame_range
    break
sc.frame_set(int(f0 + (f1 - f0) * frac))
cx = (x - (1.45 if names[-1] in ('bruiser', 'foreman', 'colossus') else 1.1)) / 2
cam = bpy.data.cameras.new('c'); cam.type = 'ORTHO'; cam.ortho_scale = max(2.4, x * 1.02)
co = bpy.data.objects.new('c', cam); sc.collection.objects.link(co)
dist = max(4.5, x * 1.05)
ya = math.radians(yaw)
co.location = (cx + 12 * math.sin(ya), -12 * math.cos(ya), 1.15 + 12 * math.tan(math.radians(6))); co.rotation_euler = (math.radians(84), 0, ya)
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
sc.cycles.device = 'CPU'; sc.cycles.samples = 24; sc.cycles.use_denoising = True
sc.render.resolution_x = W; sc.render.resolution_y = H
sc.render.filepath = out
sc.render.threads_mode = 'FIXED'; sc.render.threads = 6
bpy.ops.render.render(write_still=True)
