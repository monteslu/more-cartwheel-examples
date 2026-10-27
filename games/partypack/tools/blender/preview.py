# A quick CPU preview of generated glb models (Cycles on the CPU, so it never touches a display GPU).
# flatpak run --filesystem=home org.blender.Blender --background --factory-startup --python <abs>/preview.py -- out.png w h x0,y0,z0 yaw model.glb [model.glb ...]
# Models are laid out left to right, 3 m apart. The camera looks at them from the front and above.
import bpy, sys, math
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
out, W, H = args[0], int(args[1]), int(args[2])
ctr = [float(v) for v in args[3].split(",")]
dist = float(args[4])
models = args[5:]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = "CYCLES"
sc.cycles.device = "CPU"
sc.cycles.samples = 24
sc.cycles.use_denoising = False
sc.render.resolution_x, sc.render.resolution_y = W, H
sc.render.filepath = out
x = 0.0
for m in models:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=m)
    new = [o for o in bpy.data.objects if o not in before]
    for o in new:
        if o.parent is None:
            o.location.x += x
    x += 3.0
world = bpy.data.worlds.new("w")
sc.world = world
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs[0].default_value = (0.35, 0.4, 0.55, 1)
bg.inputs[1].default_value = 1.0
sun = bpy.data.lights.new("sun", "SUN")
sun.energy = 4
so = bpy.data.objects.new("sun", sun)
bpy.context.collection.objects.link(so)
so.rotation_euler = (math.radians(50), math.radians(10), math.radians(-35))
cam = bpy.data.cameras.new("cam")
cam.lens = 40
co = bpy.data.objects.new("cam", cam)
bpy.context.collection.objects.link(co)
sc.camera = co
tgt = Vector((ctr[0] + (x - 3.0) / 2.0, ctr[1], ctr[2]))
co.location = tgt + Vector((0, -dist, dist * 0.5))
d = tgt - co.location
co.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
bpy.ops.render.render(write_still=True)
