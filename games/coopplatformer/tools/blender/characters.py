# Builds the game's four rigged characters (hero, walker, flyer, boss) and exports one .glb each.
#
#   flatpak run org.blender.Blender --background --factory-startup \
#     --python /abs/path/to/characters.py -- /abs/out/dir [--sheet /abs/sheet_prefix]
#
# Every character is a skinned mesh (an armature with automatic weights) with PBR materials; the game poses the bones
# in code (run cycle, squash, wing flap) so there are no baked clips. Blender faces -Y, glTF exports that as +Z.
# The hero has four headgear objects (acc0..acc3), one per player, so a player is never told apart by colour alone.
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(argv[0])
SHEET = os.path.abspath(argv[argv.index("--sheet") + 1]) if "--sheet" in argv else None
os.makedirs(OUT, exist_ok=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, rgb, rough=0.5, metal=0.0, emit=None, emit_strength=0.0, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if coat:
        b.inputs["Coat Weight"].default_value = coat
        b.inputs["Coat Roughness"].default_value = 0.2
    if emit:
        b.inputs["Emission Color"].default_value = (emit[0], emit[1], emit[2], 1)
        b.inputs["Emission Strength"].default_value = emit_strength
    return m


def blob(name, loc, scale, mat, rot=(0, 0, 0), seg=24, ring=14):
    """An ellipsoid: a UV sphere scaled, smooth shaded."""
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=ring, radius=1.0, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    o.rotation_euler = rot
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat)
    return o


def cone(name, loc, r, h, mat, rot=(0, 0, 0), verts=16):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=0.0, depth=h, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat)
    return o


def torus(name, loc, major, minor, mat, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=28, minor_segments=10, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat)
    return o


def box(name, loc, size, mat, rot=(0, 0, 0), bevel=0.03):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bv = o.modifiers.new("bevel", "BEVEL")
    bv.width = bevel
    bv.segments = 3
    bpy.ops.object.modifier_apply(modifier="bevel")
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat)
    return o


def rig(bones):
    """bones: list of (name, parent, head, tail). Returns the armature object."""
    bpy.ops.object.armature_add(location=(0, 0, 0))
    arm = bpy.context.active_object
    arm.name = "Rig"
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    for b in list(eb):
        eb.remove(b)
    made = {}
    for name, parent, head, tail in bones:
        e = eb.new(name)
        e.head = Vector(head)
        e.tail = Vector(tail)
        if parent:
            e.parent = made[parent]
        made[name] = e
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm


def skin_with_extras(body_parts, extras, arm, extra_bone):
    """Join the body and skin it with automatic weights; each extra (an accessory) is its own object, weighted fully to
    one bone, so the exporter keeps it a separate skinned mesh."""
    bpy.ops.object.select_all(action="DESELECT")
    for p in body_parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = body_parts[0]
    bpy.ops.object.join()
    mesh = bpy.context.active_object
    mesh.name = "Body"
    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    groups = {}
    for e in extras:
        groups.setdefault(e.name.split(".")[0], []).append(e)
    for n, mine in sorted(groups.items()):
        bpy.ops.object.select_all(action="DESELECT")
        for e in mine:
            e.select_set(True)
        bpy.context.view_layer.objects.active = mine[0]
        bpy.ops.object.join()
        o = bpy.context.active_object
        o.name = n
        vg = o.vertex_groups.new(name=extra_bone["acc"])
        vg.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
        o.parent = arm
        mod = o.modifiers.new("Armature", "ARMATURE")
        mod.object = arm


def export(name):
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_skins=True, export_animations=False,
                              export_apply=False, export_yup=True, export_materials="EXPORT", export_image_format="AUTO")
    print("exported", path)


def sheet(name):
    """A quick CPU render of front, side and three-quarter views (Cycles, few samples) to look at the model."""
    if not SHEET:
        return
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 24
    sc.cycles.use_denoising = False
    sc.render.resolution_x = 360
    sc.render.resolution_y = 360
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.65, 0.8, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    bpy.ops.object.light_add(type="SUN", location=(3, -4, 5))
    sun = bpy.context.active_object
    sun.rotation_euler = (math.radians(55), 0, math.radians(35))
    sun.data.energy = 3.5
    meshes = [o for o in sc.objects if o.type == "MESH"]
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in meshes:
        for c in o.bound_box:
            v = o.matrix_world @ Vector(c)
            lo = Vector((min(lo[i], v[i]) for i in range(3)))
            hi = Vector((max(hi[i], v[i]) for i in range(3)))
    ctr = (lo + hi) / 2
    r = max(hi - lo) * 1.3
    bpy.ops.object.camera_add()
    cam = bpy.context.active_object
    sc.camera = cam
    cam.data.lens = 70
    for i, (az, el) in enumerate([(0, 8), (90, 8), (35, 18)]):
        a, e = math.radians(az), math.radians(el)
        cam.location = ctr + Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e))) * r * 2.4
        d = ctr - cam.location
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = f"{SHEET}_{name}_{i}.png"
        bpy.ops.render.render(write_still=True)


# ── hero: a round, friendly explorer ──
def hero():
    reset()
    fur = material("fur", (0.9, 0.9, 0.9), 0.55, 0.0, coat=0.35)  # the game recolours "fur" per player
    belly = material("belly", (0.96, 0.9, 0.78), 0.6)
    eyew = material("eye", (0.98, 0.98, 1.0), 0.15)
    pupil = material("pupil", (0.02, 0.02, 0.05), 0.1)
    boot = material("boot", (0.16, 0.1, 0.07), 0.45, 0.1)
    scarf = material("scarf", (0.95, 0.75, 0.2), 0.7)  # recoloured per player: the accent
    acc = material("acc", (0.95, 0.95, 0.95), 0.5)
    parts = []
    parts.append(blob("torso", (0, 0, 0.62), (0.4, 0.33, 0.42), fur))
    parts.append(blob("belly", (0, -0.2, 0.55), (0.27, 0.16, 0.3), belly))
    parts.append(blob("head", (0, 0, 1.1), (0.42, 0.37, 0.35), fur))
    parts.append(blob("muzzle", (0, -0.3, 1.02), (0.17, 0.12, 0.11), belly))
    parts.append(blob("nose", (0, -0.41, 1.07), (0.05, 0.04, 0.035), pupil, seg=12, ring=8))
    for s in (-1, 1):
        parts.append(blob("eye", (0.15 * s, -0.29, 1.17), (0.1, 0.07, 0.115), eyew))
        parts.append(blob("pupil", (0.15 * s, -0.355, 1.17), (0.05, 0.03, 0.065), pupil, seg=12, ring=8))
        parts.append(blob("arm", (0.46 * s, 0.0, 0.66), (0.11, 0.11, 0.26), fur, rot=(0, math.radians(-12 * s), 0)))
        parts.append(blob("hand", (0.5 * s, -0.02, 0.43), (0.12, 0.12, 0.12), belly, seg=14, ring=10))
        parts.append(blob("leg", (0.17 * s, 0, 0.27), (0.14, 0.14, 0.2), fur))
        parts.append(blob("boot", (0.17 * s, -0.06, 0.1), (0.17, 0.25, 0.11), boot))
    parts.append(torus("scarf", (0, 0, 0.9), 0.33, 0.07, scarf, scale=(1.0, 0.9, 1.0)))
    parts.append(blob("tail", (0, 0.38, 0.4), (0.13, 0.3, 0.13), fur, rot=(math.radians(-25), 0, 0)))
    parts.append(blob("tailtip", (0, 0.58, 0.5), (0.1, 0.12, 0.1), belly, seg=12, ring=8))
    # headgear: variant k is the object "acc<k>" (the game shows one)
    a = []
    for s in (-1, 1):
        a.append(cone("acc0", (0.22 * s, 0.0, 1.5), 0.13, 0.32, acc, rot=(0, math.radians(14 * s), 0)))
    a.append(blob("acc1", (0, 0, 1.64), (0.05, 0.05, 0.05), acc, seg=12, ring=8))
    bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.025, depth=0.28, location=(0, 0, 1.55))
    stalk = bpy.context.active_object
    stalk.name = "acc1"
    bpy.ops.object.shade_smooth()
    stalk.data.materials.append(acc)
    a.append(stalk)
    for s in (-1, 1):
        a.append(blob("acc2", (0.12 * s, 0.0, 1.5), (0.14, 0.04, 0.07), acc, rot=(0, math.radians(30 * s), math.radians(20 * s))))
    for s in (-1, 1):
        a.append(cone("acc3", (0.27 * s, -0.02, 1.44), 0.07, 0.3, acc, rot=(0, math.radians(35 * s), 0), verts=10))
    arm = rig([
        ("root", None, (0, 0, 0.0), (0, 0, 0.25)),
        ("body", "root", (0, 0, 0.35), (0, 0, 0.8)),
        ("head", "body", (0, 0, 0.85), (0, 0, 1.45)),
        ("arm.L", "body", (0.4, 0, 0.88), (0.5, 0, 0.4)),
        ("arm.R", "body", (-0.4, 0, 0.88), (-0.5, 0, 0.4)),
        ("leg.L", "root", (0.17, 0, 0.4), (0.17, 0, 0.05)),
        ("leg.R", "root", (-0.17, 0, 0.4), (-0.17, 0, 0.05)),
        ("tail", "body", (0, 0.3, 0.4), (0, 0.65, 0.55)),
    ])
    skin_with_extras(parts, a, arm, {"acc": "head"})
    sheet("hero")
    export("hero")


# ── walker: a spiked shell beetle with six stubby legs ──
def walker():
    reset()
    shell = material("shell", (0.8, 0.25, 0.12), 0.35, 0.1, coat=0.6)
    belly = material("belly", (0.85, 0.7, 0.5), 0.7)
    dark = material("dark", (0.12, 0.07, 0.06), 0.5, 0.2)
    eyem = material("eyeglow", (1.0, 0.9, 0.4), 0.2, 0.0, emit=(1.0, 0.75, 0.2), emit_strength=1.5)
    pupil = material("pupil", (0.02, 0.02, 0.03), 0.1)
    parts = [blob("shell", (0, 0.05, 0.5), (0.52, 0.5, 0.4), shell)]
    parts.append(blob("under", (0, -0.05, 0.28), (0.45, 0.45, 0.2), belly))
    parts.append(blob("head", (0, -0.42, 0.42), (0.28, 0.26, 0.24), dark))
    for s in (-1, 1):
        parts.append(blob("eye", (0.12 * s, -0.62, 0.5), (0.09, 0.06, 0.1), eyem))
        parts.append(blob("pupil", (0.12 * s, -0.68, 0.5), (0.04, 0.03, 0.06), pupil, seg=10, ring=8))
        parts.append(cone("horn", (0.15 * s, -0.55, 0.7), 0.06, 0.3, dark, rot=(math.radians(-20), math.radians(15 * s), 0), verts=10))
        for y in (-0.25, 0.05, 0.35):
            parts.append(blob("leg", (0.44 * s, y, 0.14), (0.16, 0.09, 0.13), dark, seg=12, ring=8))
    for k in range(5):
        parts.append(cone("spike", (0, 0.05 + (k - 2) * 0.17, 0.86 - 0.04 * abs(k - 2)), 0.08, 0.28, dark, verts=8))
    bones = [("root", None, (0, 0, 0), (0, 0, 0.25)), ("body", "root", (0, 0, 0.3), (0, 0, 0.8)), ("head", "body", (0, -0.3, 0.42), (0, -0.62, 0.45))]
    for s, n in ((1, "L"), (-1, "R")):
        for k, y in enumerate((-0.25, 0.05, 0.35)):
            bones.append((f"leg{k}.{n}", "root", (0.38 * s, y, 0.3), (0.5 * s, y, 0.02)))
    arm = rig(bones)
    skin_with_extras(parts, [], arm, {})
    sheet("walker")
    export("walker")


# ── flyer: a fat moth with big wings and one glowing eye ──
def flyer():
    reset()
    fur = material("fur", (0.48, 0.25, 0.7), 0.6, 0.0)
    wing = material("wing", (0.65, 0.4, 0.85), 0.5, 0.0, coat=0.3)
    dark = material("dark", (0.1, 0.06, 0.16), 0.5)
    eyem = material("eyeglow", (1.0, 0.95, 0.6), 0.2, 0.0, emit=(1.0, 0.85, 0.3), emit_strength=2.0)
    pupil = material("pupil", (0.02, 0.0, 0.04), 0.1)
    parts = [blob("body", (0, 0, 0.5), (0.3, 0.3, 0.38), fur)]
    parts.append(blob("head", (0, -0.2, 0.82), (0.25, 0.23, 0.22), fur))
    parts.append(blob("eye", (0, -0.38, 0.84), (0.13, 0.07, 0.13), eyem))
    parts.append(blob("pupil", (0, -0.44, 0.84), (0.06, 0.03, 0.08), pupil, seg=10, ring=8))
    for s in (-1, 1):
        parts.append(blob("wing", (0.62 * s, 0.1, 0.62), (0.5, 0.04, 0.32), wing, rot=(0, math.radians(-14 * s), 0)))
        parts.append(blob("wingtip", (1.0 * s, 0.1, 0.55), (0.28, 0.03, 0.2), wing, rot=(0, math.radians(-20 * s), 0)))
        parts.append(cone("feeler", (0.1 * s, -0.25, 1.1), 0.03, 0.3, dark, rot=(math.radians(-10), math.radians(20 * s), 0), verts=8))
        parts.append(blob("foot", (0.1 * s, -0.05, 0.14), (0.06, 0.06, 0.1), dark, seg=8, ring=6))
    bones = [("root", None, (0, 0, 0.3), (0, 0, 0.5)), ("head", "root", (0, -0.1, 0.7), (0, -0.2, 0.95))]
    for s, n in ((1, "L"), (-1, "R")):
        bones.append((f"wing.{n}", "root", (0.25 * s, 0.1, 0.62), (0.7 * s, 0.1, 0.62)))
        bones.append((f"wingtip.{n}", f"wing.{n}", (0.7 * s, 0.1, 0.62), (1.2 * s, 0.1, 0.56)))
    arm = rig(bones)
    skin_with_extras(parts, [], arm, {})
    sheet("flyer")
    export("flyer")


# ── boss: a stone golem with a glowing core, two heavy arms ──
def boss():
    reset()
    stone = material("stone", (0.42, 0.18, 0.16), 0.8, 0.0)
    stone2 = material("stone2", (0.3, 0.12, 0.11), 0.85, 0.0)
    core = material("core", (1.0, 0.45, 0.1), 0.3, 0.0, emit=(1.0, 0.4, 0.08), emit_strength=3.0)
    eyem = material("eyeglow", (1.0, 0.8, 0.3), 0.2, 0.0, emit=(1.0, 0.7, 0.2), emit_strength=3.0)
    parts = [box("torso", (0, 0, 0.75), (0.95, 0.7, 0.8), stone, bevel=0.1)]
    parts.append(blob("core", (0, -0.34, 0.8), (0.2, 0.08, 0.22), core, seg=16, ring=10))
    parts.append(box("head", (0, -0.05, 1.45), (0.55, 0.5, 0.42), stone2, bevel=0.08))
    for s in (-1, 1):
        parts.append(blob("eye", (0.15 * s, -0.3, 1.5), (0.09, 0.05, 0.05), eyem, seg=10, ring=8))
        parts.append(box("shoulder", (0.62 * s, 0, 1.05), (0.4, 0.45, 0.35), stone2, bevel=0.08))
        parts.append(box("arm", (0.74 * s, 0, 0.6), (0.3, 0.3, 0.62), stone, bevel=0.07))
        parts.append(box("fist", (0.78 * s, -0.03, 0.2), (0.42, 0.42, 0.34), stone2, bevel=0.08))
        parts.append(box("leg", (0.28 * s, 0, 0.2), (0.34, 0.4, 0.4), stone2, bevel=0.07))
    for k in range(3):
        parts.append(cone("crag", (-0.3 + 0.3 * k, 0.15, 1.28 + 0.06 * (k % 2)), 0.1, 0.4, stone2, verts=7))
    bones = [("root", None, (0, 0, 0), (0, 0, 0.4)), ("body", "root", (0, 0, 0.4), (0, 0, 1.2)), ("head", "body", (0, 0, 1.25), (0, 0, 1.7))]
    for s, n in ((1, "L"), (-1, "R")):
        bones.append((f"arm.{n}", "body", (0.62 * s, 0, 1.05), (0.78 * s, 0, 0.3)))
        bones.append((f"leg.{n}", "root", (0.28 * s, 0, 0.4), (0.28 * s, 0, 0.02)))
    arm = rig(bones)
    skin_with_extras(parts, [], arm, {})
    sheet("boss")
    export("boss")


hero()
walker()
flyer()
boss()
