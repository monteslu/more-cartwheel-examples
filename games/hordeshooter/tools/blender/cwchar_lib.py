# Shared Blender helpers for the horde models: a primitive-built, armature-skinned model, baked ambient occlusion,
# per-frame evaluation and the .cwchar writer (format described in hordechars.py).
import bpy, bmesh, math, struct, sys, os
from mathutils import Vector, Matrix, noise
from mathutils.bvhtree import BVHTree
def srgb(c):
    return tuple(c)

# ───────────────────────── model builder ──────────────────────────────

class Model:
    def __init__(self):
        self.v = []; self.w = []; self.col = []; self.faces = []; self.uv = []; self.smooth = []; self.grp = []
        self.bones = []  # (name, parent, head, tail)

    def bone(self, name, parent, head, tail):
        self.bones.append((name, parent, Vector(head), Vector(tail)))

    def _add(self, bm, mat, weights, color, group, smooth, uvs, noise_amt, emissive=None):
        uv_l = bm.loops.layers.uv.verify()
        base = len(self.v)
        idx = {}
        for vert in bm.verts:
            p = mat @ vert.co
            idx[vert.index] = len(self.v)
            self.v.append(p)
            self.w.append(weights(p) if callable(weights) else weights)
            n = noise.noise(p * 3.1) * noise_amt
            c = color
            k = 1.0 + n
            if emissive is not None and emissive:
                self.col.append((c[0], c[1], c[2]))
            else:
                self.col.append((c[0] * k, c[1] * k, c[2] * k))
        for f in bm.faces:
            vs = [idx[l.vert.index] for l in f.loops]
            self.faces.append(vs)
            self.uv.append([(l[uv_l].uv[0] * uvs[0], l[uv_l].uv[1] * uvs[1]) for l in f.loops])
            self.smooth.append(smooth)
            self.grp.append(group)

    def ell(self, bone, c, r, color, rot=(0, 0, 0), group=0, seg=(10, 7), w=None, smooth=True, noise_amt=0.12, emissive=False):
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=seg[0], v_segments=seg[1], radius=1.0, calc_uvs=True)
        for vv in bm.verts:
            vv.co = Vector((vv.co.x * r[0], vv.co.y * r[1], vv.co.z * r[2]))
        m = Matrix.Translation(c) @ Matrix(Matrix.Rotation(rot[2], 4, 'Z') @ Matrix.Rotation(rot[1], 4, 'Y') @ Matrix.Rotation(rot[0], 4, 'X'))
        uvs = (2 * math.pi * (r[0] + r[1]) * 0.5, math.pi * r[2])
        self._add(bm, m, w if w is not None else {bone: 1.0}, color, group, smooth, uvs, noise_amt, emissive)
        bm.free()

    def box(self, bone, c, s, color, rot=(0, 0, 0), group=0, bevel=0.02, w=None, noise_amt=0.1):
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0, calc_uvs=True)
        for vv in bm.verts:
            vv.co = Vector((vv.co.x * s[0], vv.co.y * s[1], vv.co.z * s[2]))
        if bevel > 0:
            bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=1, affect='EDGES')
        m = Matrix.Translation(c) @ Matrix(Matrix.Rotation(rot[2], 4, 'Z') @ Matrix.Rotation(rot[1], 4, 'Y') @ Matrix.Rotation(rot[0], 4, 'X'))
        self._add(bm, m, w if w is not None else {bone: 1.0}, color, group, False, (max(s[0], s[1]) * 1.0, s[2] * 1.0), noise_amt)
        bm.free()

    def limb(self, bone, p0, p1, r0, r1, color, group=0, seg=7, w=None, cap=True, noise_amt=0.12):
        p0 = Vector(p0); p1 = Vector(p1)
        d = p1 - p0
        L = d.length
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=seg, radius1=r0, radius2=r1, depth=L, calc_uvs=True)
        q = Vector((0, 0, 1)).rotation_difference(d.normalized())
        m = Matrix.Translation((p0 + p1) * 0.5) @ q.to_matrix().to_4x4()
        self._add(bm, m, w if w is not None else {bone: 1.0}, color, group, True, (2 * math.pi * (r0 + r1) * 0.5, L), noise_amt)
        bm.free()

    def spike(self, bone, base, tip, r, color, group=0, seg=5, w=None):
        self.limb(bone, base, tip, r, r * 0.04, color, group, seg, w, noise_amt=0.05)

def build_object(M, name):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in M.v], [], M.faces)
    me.update()
    uvl = me.uv_layers.new(name="uv")
    li = 0
    for fi, poly in enumerate(me.polygons):
        for k in range(poly.loop_total if hasattr(poly, "loop_total") else len(poly.vertices)):
            uvl.data[poly.loop_start + k].uv = M.uv[fi][k]
        poly.use_smooth = M.smooth[fi]
        poly.material_index = M.grp[fi]
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    # vertex groups
    names = [b[0] for b in M.bones]
    vgs = {n: ob.vertex_groups.new(name=n) for n in names}
    for vi, wd in enumerate(M.w):
        tot = sum(wd.values())
        for bn, wt in wd.items():
            vgs[bn].add([vi], wt / tot, 'REPLACE')
    # armature
    arm = bpy.data.armatures.new(name + "_rig")
    ao = bpy.data.objects.new(name + "_rig", arm)
    bpy.context.scene.collection.objects.link(ao)
    bpy.context.view_layer.objects.active = ao
    ao.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for (bn, parent, head, tail) in M.bones:
        eb = arm.edit_bones.new(bn)
        eb.head = head; eb.tail = tail
        if parent:
            eb.parent = arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    mod = ob.modifiers.new("rig", 'ARMATURE')
    mod.object = ao
    for pb in ao.pose.bones:
        pb.rotation_mode = 'XYZ'
    # colours (kept as a vertex array, not a Blender attribute)
    return ob, ao

def bake_ao(ob, M, samples=40):
    """Per vertex ambient occlusion from the rest pose: darkens armpits, crotches, under hoods."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    tree = BVHTree.FromBMesh(bm)
    vn = [Vector((0, 0, 0)) for _ in M.v]
    for f in bm.faces:
        pass
    ev.to_mesh_clear()
    import random
    rng = random.Random(7)
    ao = []
    me2 = ob.data
    me2.calc_tangents if False else None
    vnorm = [v.normal.copy() for v in me2.vertices]
    for vi, p in enumerate(M.v):
        n = vnorm[vi]
        if n.length < 1e-6:
            ao.append(1.0); continue
        hit = 0
        for _ in range(samples):
            d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1)))
            if d.length < 1e-6: continue
            d.normalize()
            if d.dot(n) < 0: d = -d
            d = (d + n).normalized()
            loc, nor, idx, dist = tree.ray_cast(p + n * 0.01, d, 0.5)
            if loc is not None:
                hit += 1 - dist / 0.5 * 0.5
        ao.append(1.0 - 0.75 * hit / samples)
    bm.free()
    return ao

def pose_frame(ao, pose):
    for pb in ao.pose.bones:
        pb.rotation_euler = (0, 0, 0)
        pb.location = (0, 0, 0)
    for bn, v in pose.items():
        pb = ao.pose.bones[bn]
        pb.rotation_euler = (v[0], v[1], v[2])
        if len(v) > 3:
            pb.location = (v[3], v[4], v[5])
    bpy.context.view_layer.update()

def bake_clip(ob, M, ao, poses):
    """Per frame, per group: positions and loop normals, deduplicated by (vertex, uv)."""
    frames = []
    dg = bpy.context.evaluated_depsgraph_get()
    for pose in poses:
        pose_frame(ao, pose)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        frames.append(([v.co.copy() for v in me.vertices], [tuple(n.vector) for n in me.corner_normals], [l.vertex_index for l in me.loops]))
        ev.to_mesh_clear()
    return frames

def write_cwchar(path, M, ao_col, clips, ob):
    """clips: list of lists of frame data (positions, loop normals)."""
    me = ob.data
    ngroups = max(M.grp) + 1
    out = bytearray()
    out += b"CWCH" + struct.pack("<III", 1, len(clips), ngroups)
    for c in clips:
        out += struct.pack("<I", len(c))
    # triangulated loops, deduplicated per group by (vertex, uv rounded)
    for g in range(ngroups):
        key = {}
        verts = []  # (vertex index, loop index)
        tris = []
        for fi, poly in enumerate(me.polygons):
            if poly.material_index != g: continue
            ls = list(range(poly.loop_start, poly.loop_start + poly.loop_total))
            ids = []
            for l in ls:
                vi = me.loops[l].vertex_index
                uv = me.uv_layers[0].data[l].uv
                k = (vi, round(uv[0], 4), round(uv[1], 4), l if poly.use_smooth is False else -1)
                if k not in key:
                    key[k] = len(verts); verts.append((vi, l, uv[0], uv[1]))
                ids.append(key[k])
            for t in range(1, len(ids) - 1):
                tris.append((ids[0], ids[t], ids[t + 1]))
        nv = len(verts)
        assert nv < 65535
        out += struct.pack("<II", nv, len(tris))
        for (_, _, u, v) in verts: out += struct.pack("<ff", u, v)
        for (vi, _, _, _) in verts:
            c = M.col[vi]; a = ao_col[vi]
            out += struct.pack("<fff", c[0] * a, c[1] * a, c[2] * a)
        for t in tris: out += struct.pack("<HHH", *t)
        if (len(tris) * 6) % 4: out += b"\0\0"
        for clip in clips:
            for (pos, nrm, lv) in clip:
                for (vi, l, _, _) in verts:
                    p = pos[vi]
                    out += struct.pack("<fff", p.x, p.z, -p.y)
                for (vi, l, _, _) in verts:
                    n = nrm[l]
                    out += struct.pack("<fff", n[0], n[2], -n[1])
    with open(path, "wb") as f:
        f.write(out)
    return len(out)

