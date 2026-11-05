# Renders .cwchar files exactly as written (the bytes the game reads, not the Blender scene that made them)
# into a contact sheet, headless, on the CPU (Cycles):
#   flatpak run --filesystem=home org.blender.Blender --background --factory-startup --python \
#     <abs>/tools/blender/cwchar_view.py -- <abs out.png> <abs file.cwchar> [options]
# options:
#   --frames all | first | c:f,c:f...   which frames get a row (default: the first frame of each clip)
#   --views front,right,back,left,top,q,game   the columns (default all seven)
#   --tile N        a tile's size in pixels (default 300)
#   --tint r,g,b    the colour the game multiplies group 1 by (default a team blue 0.4,0.7,1.0)
#   --patch <abs spec.json>   instead of a model: a tile patch, [{"file":..,"frame":n,"x":..,"z":..,"rot":k,"s":scale}, ...]
#                             in game metres (rot in 90 degree steps, s optional), rendered from the game camera, native size
#                             and twice native
# The game camera: 62 degrees down from the south (+Z in game), 22 m away, a 38 degree vertical field of view,
# 1920 x 1080; the "game" column is that render cropped to the model at its in-game pixel size.
import sys, os, math, struct, json
import bpy, bmesh
import numpy as np
from mathutils import Vector, Matrix
from bpy_extras.object_utils import world_to_camera_view

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(argv) < 2:
    sys.exit("usage: -- <out.png> <file.cwchar | --patch spec.json> [options]")
OUT = os.path.abspath(argv[0])
opts = {"cam": "62,38,22", "floor": "0.05,0.047,0.045", "frames": "first", "views": "front,right,back,left,top,q,game", "tile": "300", "tint": "0.4,0.7,1.0", "patch": None}
files = []
i = 1
while i < len(argv):
    if argv[i].startswith("--"):
        opts[argv[i][2:]] = argv[i + 1]; i += 2
    else:
        files.append(argv[i]); i += 1
TILE = int(opts["tile"])
TINT = tuple(float(x) for x in opts["tint"].split(","))
TMP = os.path.join(os.path.dirname(OUT), ".cwview_tmp_" + os.path.basename(OUT))
os.makedirs(TMP, exist_ok=True)

def read_cwchar(path):
    b = open(path, "rb").read()
    assert b[:4] == b"CWCH", path
    _, nclip, ngroup = struct.unpack_from("<III", b, 4)
    p = 16
    clips = list(struct.unpack_from("<%dI" % nclip, b, p)); p += 4 * nclip
    total = sum(clips)
    groups = []
    for g in range(ngroup):
        nv, nt = struct.unpack_from("<II", b, p); p += 8
        p += 8 * nv
        col = np.frombuffer(b, np.float32, 3 * nv, p).reshape(nv, 3); p += 12 * nv
        idx = np.frombuffer(b, np.uint16, 3 * nt, p).reshape(nt, 3); p += (nt * 6 + 3) & ~3
        frames = []
        for f in range(total):
            pos = np.frombuffer(b, np.float32, 3 * nv, p).reshape(nv, 3); p += 12 * nv
            nrm = np.frombuffer(b, np.float32, 3 * nv, p).reshape(nv, 3); p += 12 * nv
            frames.append((pos, nrm))
        groups.append((col, idx, frames))
    assert p == len(b), path
    return clips, groups

def s2l(c):
    c = max(0.0, c)
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

MATS = {}
CUR = "patch"
def material(group):
    key = group
    if key in MATS: return MATS[key]
    m = bpy.data.materials.new("g%d" % group); m.use_nodes = True
    nt = m.node_tree; N = nt.nodes
    bsdf = next(n for n in N if n.type == "BSDF_PRINCIPLED")
    out = next(n for n in N if n.type == "OUTPUT_MATERIAL")
    a = N.new("ShaderNodeAttribute"); a.attribute_name = "col"
    e = N.new("ShaderNodeAttribute"); e.attribute_name = "emi"
    # back faces render magenta (the game culls them: a magenta patch is a face turned the wrong way)
    geo = N.new("ShaderNodeNewGeometry")
    bf = N.new("ShaderNodeEmission"); bf.inputs["Color"].default_value = (1, 0, 1, 1); bf.inputs["Strength"].default_value = 2.0
    ms = N.new("ShaderNodeMixShader")
    if group == 0:
        nt.links.new(a.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.8
        nt.links.new(e.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = 1.0
    else:
        em = N.new("ShaderNodeEmission")
        mix = N.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'
        mix.inputs[0].default_value = 1.0
        nt.links.new(a.outputs["Color"], mix.inputs[6])
        mix.inputs[7].default_value = (TINT[0], TINT[1], TINT[2], 1)
        nt.links.new(mix.outputs[2], em.inputs["Color"])
        em.inputs["Strength"].default_value = 1.0
        nt.links.new(em.outputs[0], out.inputs["Surface"])
    src = out.inputs["Surface"].links[0].from_socket
    nt.links.new(geo.outputs["Backfacing"], ms.inputs[0])
    nt.links.new(src, ms.inputs[1]); nt.links.new(bf.outputs[0], ms.inputs[2])
    nt.links.new(ms.outputs[0], out.inputs["Surface"])
    MATS[key] = m
    return m

def build(path, frame, name, xform=Matrix.Identity(4)):
    """one frame of a cwchar as Blender objects (one per group), game axes converted to Blender z-up"""
    clips, groups = read_cwchar(path)
    obs = []
    for g, (col, idx, frames) in enumerate(groups):
        pos, nrm = frames[frame]
        if len(idx) == 0: continue
        me = bpy.data.meshes.new("%s_g%d" % (name, g))
        verts = [(float(x), float(-z), float(y)) for x, y, z in pos]
        me.from_pydata(verts, [], [tuple(int(k) for k in t) for t in idx])
        me.update()
        ca = me.color_attributes.new("col", 'FLOAT_COLOR', 'POINT')
        ce = me.color_attributes.new("emi", 'FLOAT_COLOR', 'POINT')
        for vi, c in enumerate(col):
            emi = c[0] > 1.5 or c[1] > 1.5 or c[2] > 1.5
            if g == 1 or emi:
                ca.data[vi].color = (float(c[0]), float(c[1]), float(c[2]), 1)
            else:
                ca.data[vi].color = (s2l(c[0]), s2l(c[1]), s2l(c[2]), 1)
            ce.data[vi].color = (float(c[0]), float(c[1]), float(c[2]), 1) if (emi and g == 0) else (0, 0, 0, 1)
        me.normals_split_custom_set_from_vertices([(float(x), float(-z), float(y)) for x, y, z in nrm])
        me.materials.append(material(g))
        ob = bpy.data.objects.new(me.name, me)
        ob.matrix_world = xform
        bpy.context.scene.collection.objects.link(ob)
        obs.append(ob)
    return obs, clips

def bounds(obs):
    lo = Vector((1e9,) * 3); hi = Vector((-1e9,) * 3)
    for o in obs:
        for v in o.data.vertices:
            p = o.matrix_world @ v.co
            lo = Vector(map(min, lo, p)); hi = Vector(map(max, hi, p))
    return lo, hi

def scene_setup():
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'; sc.cycles.samples = 24; sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 3
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'Standard'
    w = bpy.data.worlds.new("w"); w.use_nodes = True
    bg = next(n for n in w.node_tree.nodes if n.type == "BACKGROUND")
    bg.inputs[0].default_value = (0.05, 0.055, 0.07, 1); bg.inputs[1].default_value = 1.0
    sc.world = w
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN')); sun.data.energy = 3.2; sun.data.angle = 0.2
    sun.rotation_euler = (0.75, 0.25, -0.5); sc.collection.objects.link(sun)
    fill = bpy.data.objects.new("fill", bpy.data.lights.new("fill", 'SUN')); fill.data.energy = 0.8
    fill.rotation_euler = (1.1, -0.3, 2.6); sc.collection.objects.link(fill)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam); sc.camera = cam
    return sc, cam

def floor(size, z=0.0, c=None):
    c = c or tuple(float(x) for x in opts["floor"].split(","))
    me = bpy.data.meshes.new("floor")
    s = size
    me.from_pydata([(-s, -s, z), (s, -s, z), (s, s, z), (-s, s, z)], [], [(0, 1, 2, 3)])
    m = bpy.data.materials.new("floor"); m.use_nodes = True
    bsdf = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = (c[0], c[1], c[2], 1); bsdf.inputs["Roughness"].default_value = 0.9
    me.materials.append(m)
    ob = bpy.data.objects.new("floor", me); bpy.context.scene.collection.objects.link(ob)
    return ob

def look(cam, eye, at):
    cam.location = eye
    cam.rotation_euler = (Vector(at) - Vector(eye)).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.view_layer.update()

def game_cam(cam, at):
    t_, f_, d_ = (float(x) for x in opts["cam"].split(","))
    tilt = math.radians(t_); d = d_
    cam.data.sensor_fit = 'VERTICAL'; cam.data.angle_y = math.radians(f_)
    cam.data.clip_end = d * 3
    # game: eye = at + (0, sin(tilt) d, cos(tilt) d), game +Z is Blender -Y
    look(cam, (at[0], at[1] - math.cos(tilt) * d, at[2] + math.sin(tilt) * d), at)

def render(sc, path, w, h, border=None, pct=100):
    sc.render.resolution_x = w; sc.render.resolution_y = h; sc.render.resolution_percentage = pct
    if border:
        sc.render.use_border = True; sc.render.use_crop_to_border = True
        sc.render.border_min_x, sc.render.border_max_x, sc.render.border_min_y, sc.render.border_max_y = border
    else:
        sc.render.use_border = False
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    im = bpy.data.images.load(path)
    W, H = im.size
    a = np.array(im.pixels[:], dtype=np.float32).reshape(H, W, 4)
    bpy.data.images.remove(im)
    os.remove(path)
    mag = int(np.sum((a[..., 0] > 0.85) & (a[..., 1] < 0.15) & (a[..., 2] > 0.85)))
    if mag: print("CWVIEW backfaces: %d magenta px in %s" % (mag, CUR))
    return a

def proj_border(sc, cam, lo, hi, pad=0.03):
    sc.render.resolution_x = 1920; sc.render.resolution_y = 1080; sc.render.resolution_percentage = 100
    xs = []; ys = []
    for cx in (lo.x, hi.x):
        for cy in (lo.y, hi.y):
            for cz in (lo.z, hi.z):
                v = world_to_camera_view(sc, cam, Vector((cx, cy, cz)))
                xs.append(v.x); ys.append(v.y)
    return (max(0, min(xs) - pad), min(1, max(xs) + pad), max(0, min(ys) - pad), min(1, max(ys) + pad))

def fit(a, T, up=False):
    """place an image in a T x T tile (scaled down if larger; with up, blown up by a whole number of pixels)"""
    H, W = a.shape[:2]
    if up and H < T and W < T:
        k = max(1, T // max(H, W))
        a = np.repeat(np.repeat(a, k, axis=0), k, axis=1); H, W = a.shape[:2]
    out = np.zeros((T, T, 4), np.float32); out[..., 3] = 1; out[..., :3] = 0.02
    if H > T or W > T:
        s = T / max(H, W)
        yi = (np.arange(int(H * s)) / s).astype(int); xi = (np.arange(int(W * s)) / s).astype(int)
        a = a[yi][:, xi]; H, W = a.shape[:2]
    y0 = (T - H) // 2; x0 = (T - W) // 2
    out[y0:y0 + H, x0:x0 + W] = a
    return out

def save(rows, path):
    # Blender images are stored bottom row first: the first row goes last
    img = np.concatenate([np.concatenate(r, axis=1) for r in rows[::-1]], axis=0)
    H, W = img.shape[:2]
    im = bpy.data.images.new("sheet", W, H, alpha=True)
    im.pixels = img.ravel()
    im.filepath_raw = path; im.file_format = 'PNG'; im.save()
    print("CWVIEW wrote %s (%dx%d)" % (path, W, H))

def clear_models():
    for o in list(bpy.context.scene.objects):
        if o.type == 'MESH' and o.name != "floor":
            bpy.data.objects.remove(o)

bpy.ops.wm.read_factory_settings(use_empty=True)
sc, cam = scene_setup()

if opts["patch"]:
    spec = json.load(open(opts["patch"]))
    obs = []
    for k, it in enumerate(spec):
        x, z = it["x"], it["z"]
        rot = it.get("rot", 0) * math.pi / 2
        M = Matrix.Translation((x, -z, 0)) @ Matrix.Rotation(rot, 4, 'Z') @ Matrix.Scale(it.get("s", 1.0), 4)
        o, _ = build(it["file"], it.get("frame", 0), "p%d" % k, M)
        obs += o
    lo, hi = bounds(obs)
    floor(60)
    at = ((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, 0.0)
    game_cam(cam, at)
    sc.cycles.samples = 32
    bd = proj_border(sc, cam, lo, hi, 0.01)
    a1 = render(sc, os.path.join(TMP, "p1.png"), 1920, 1080, bd, 100)
    a2 = render(sc, os.path.join(TMP, "p2.png"), 1920, 1080, bd, 200)
    T = max(a2.shape[0], a2.shape[1])
    pad1 = np.zeros((a2.shape[0], a1.shape[1], 4), np.float32); pad1[..., 3] = 1
    pad1[:a1.shape[0]] = a1
    save([[pad1, a2]], OUT)
    sys.exit(0)

views = opts["views"].split(",")
for path in files:
    clips, _ = read_cwchar(path)
    starts = [sum(clips[:c]) for c in range(len(clips))]
    total = sum(clips)
    if opts["frames"] == "all":
        sel = list(range(total))
    elif opts["frames"] == "first":
        sel = starts
    else:
        sel = [starts[int(s.split(":")[0])] + int(s.split(":")[1]) for s in opts["frames"].split(",")]
    # one framing for every frame (so motion between frames is visible), from the union of the bounds
    lo = Vector((1e9,) * 3); hi = Vector((-1e9,) * 3)
    for f in sel:
        o, _ = build(path, f, "b")
        l, h = bounds(o)
        lo = Vector(map(min, lo, l)); hi = Vector(map(max, hi, h))
        clear_models()
    fl = floor(max(hi.x - lo.x, hi.y - lo.y) * 3 + 10)
    ctr = (lo + hi) / 2
    R = max((hi - lo).length / 2, 0.05)
    rows = []
    for f in sel:
        obs, _ = build(path, f, "m")
        row = []
        for v in views:
            CUR = "%s frame %d view %s" % (os.path.basename(path), f, v)
            if v == "game":
                game_cam(cam, (ctr.x, ctr.y, 0.0))
                bd = proj_border(sc, cam, lo, hi, 0.004)
                row.append(fit(render(sc, os.path.join(TMP, "v.png"), 1920, 1080, bd), TILE, up=True))
                continue
            cam.data.sensor_fit = 'AUTO'; cam.data.angle = math.radians(30)
            D = R / math.tan(math.radians(15)) * 0.9
            dirs = {"front": (0, -1, 0.15), "back": (0, 1, 0.15), "right": (1, 0, 0.15), "left": (-1, 0, 0.15), "top": (0.0001, -0.0001, 1), "q": (0.7, -0.7, 0.7)}
            d = Vector(dirs[v]).normalized()
            look(cam, ctr + d * D, ctr)
            row.append(render(sc, os.path.join(TMP, "v.png"), TILE, TILE))
        rows.append(row)
        clear_models()
    save(rows, OUT if len(files) == 1 else OUT.replace(".png", "_" + os.path.basename(path).replace(".cwchar", ".png")))
try: os.rmdir(TMP)
except OSError: pass
