# Shared helpers for the arenafps Blender scripts (run headless, see tools/blender/build.sh).
# Models are authored in Blender (Z up, the model faces +Y), exported as glTF 2.0 binary (Y up, faces -Z).
import bpy, bmesh, math, os, sys
from mathutils import Vector, Matrix, Quaternion


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)


def material(name, color, rough=0.6, metal=0.0, emit=None, emit_strength=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    m.diffuse_color = (color[0], color[1], color[2], 1)
    n = next(x for x in m.node_tree.nodes if x.type == 'BSDF_PRINCIPLED')
    n.inputs['Base Color'].default_value = (color[0], color[1], color[2], 1)
    n.inputs['Roughness'].default_value = rough
    n.inputs['Metallic'].default_value = metal
    if emit:
        n.inputs['Emission Color'].default_value = (emit[0], emit[1], emit[2], 1)
        n.inputs['Emission Strength'].default_value = emit_strength
    return m


def new_obj(name, bm_or_mesh, mat=None, collection=None):
    if isinstance(bm_or_mesh, bmesh.types.BMesh):
        me = bpy.data.meshes.new(name)
        bm_or_mesh.to_mesh(me)
        bm_or_mesh.free()
    else:
        me = bm_or_mesh
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if mat is not None:
        ob.data.materials.append(mat)
    return ob


def smooth(ob, angle=0.9):
    for p in ob.data.polygons:
        p.use_smooth = True
    try:
        ob.data.set_sharp_from_angle(angle=angle)
    except Exception:
        pass


def bevel_box(name, size, loc=(0, 0, 0), bevel=0.02, segs=2, mat=None, rot=(0, 0, 0)):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x *= size[0]; v.co.y *= size[1]; v.co.z *= size[2]
    bmesh.ops.bevel(bm, geom=list(bm.edges) + list(bm.verts), offset=bevel, segments=segs, affect='EDGES', profile=0.6)
    ob = new_obj(name, bm, mat)
    ob.location = loc
    ob.rotation_euler = rot
    smooth(ob, 0.6)
    return ob


def cyl(name, radius, depth, loc=(0, 0, 0), rot=(0, 0, 0), verts=24, mat=None, bevel=0.0, r2=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=verts, radius1=radius, radius2=radius if r2 is None else r2, depth=depth)
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=2, affect='EDGES')
    ob = new_obj(name, bm, mat)
    ob.location = loc
    ob.rotation_euler = rot
    smooth(ob, 0.7)
    return ob


def sphere(name, radius, loc=(0, 0, 0), scale=(1, 1, 1), mat=None, seg=24, rings=14):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=radius)
    ob = new_obj(name, bm, mat)
    ob.location = loc
    ob.scale = scale
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    ob.select_set(False)
    smooth(ob, 1.2)
    return ob


def apply_xform(ob):
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    ob.select_set(False)


def join(objs, name):
    for o in objs:
        apply_xform(o)
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = name
    ob.data.name = name
    ob.select_set(False)
    return ob


def box_uv(ob, metres=1.0):
    """World-scale box projection UVs: one repeat per `metres` so the game's tiling textures keep a constant texel size."""
    me = ob.data
    if not me.uv_layers:
        me.uv_layers.new(name='UVMap')
    uv = me.uv_layers.active.data
    for p in me.polygons:
        n = p.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for li in p.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            if ax == 0: u, v = co.y, co.z
            elif ax == 1: u, v = co.x, co.z
            else: u, v = co.x, co.y
            uv[li].uv = (u / metres, v / metres)


def export_glb(path, objs=None, anims=True, selected_only=False):
    kw = dict(filepath=path, export_format='GLB', export_apply=False, export_yup=True, export_texcoords=True,
              export_normals=True, export_materials='EXPORT', export_cameras=False, export_lights=False)
    if anims:
        kw.update(export_animations=True, export_animation_mode='ACTIONS', export_skins=True, export_force_sampling=True,
                  export_optimize_animation_size=False, export_frame_range=False)
    else:
        kw.update(export_animations=False, export_skins=False)
    if selected_only:
        kw['use_selection'] = True
    bpy.ops.export_scene.gltf(**kw)
    print('wrote', path, os.path.getsize(path), 'bytes')


def args():
    a = sys.argv
    return a[a.index('--') + 1:] if '--' in a else []


def render_sheet(path, objs, views, res=(900, 700), size=2.2, center=(0, 0, 1)):
    """A quick workbench render of several camera views of the scene for checking (not shipped)."""
    sc = bpy.context.scene
    try:
        sc.render.engine = 'BLENDER_WORKBENCH'
    except TypeError:
        pass
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'MATERIAL'
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.image_settings.file_format = 'PNG'
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    sc.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = size
    sc.camera = cam
    for i, (az, el) in enumerate(views):
        d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
        cam.location = Vector(center) + d * 8
        cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = path.replace('.png', f'_{i}.png')
        bpy.ops.render.render(write_still=True)
