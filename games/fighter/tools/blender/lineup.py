# A check render of the generated fighters in their rest pose (CPU path tracing, no GPU use).
# flatpak run --filesystem=home org.blender.Blender --background --factory-startup --python <abs>/lineup.py -- \
#   <models dir> <out.png> <width> <height> <yaw degrees> <zoom: 1 whole body, 3 heads> [name ...]
import bpy, sys, math, os

argv = sys.argv[sys.argv.index('--') + 1:]
mdir, out, W, H, yaw, zoom = argv[0], argv[1], int(argv[2]), int(argv[3]), float(argv[4]), float(argv[5])
names = argv[6:] or ['bolt', 'grit', 'vex', 'mara']
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
x = 0.0
step = 0.95
for n in names:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(mdir, n + '.glb'))
    for o in [o for o in bpy.data.objects if o not in before]:
        if o.parent is None:
            o.location.x += x
    x += step
cx = x / 2 - step / 2
cam = bpy.data.cameras.new('c'); cam.type = 'ORTHO'; cam.ortho_scale = max(2.2, x * 1.02) / zoom
co = bpy.data.objects.new('c', cam); sc.collection.objects.link(co)
ya = math.radians(yaw)
cz = 0.95 if zoom <= 1 else 1.62
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
sc.cycles.device = 'CPU'; sc.cycles.samples = 20; sc.cycles.use_denoising = True
sc.render.resolution_x = W; sc.render.resolution_y = H
sc.render.filepath = out
sc.render.threads_mode = 'FIXED'; sc.render.threads = 4
bpy.ops.render.render(write_still=True)
