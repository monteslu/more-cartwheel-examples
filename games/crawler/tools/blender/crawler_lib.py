# Crawler's additions to cwchar_lib (the .cwchar format is unchanged; see hordechars.py): lathed and extruded
# shapes, bevelled stone blocks, the grounding pass (every frame of a floor-standing model put on the floor),
# and the build / bake / write loop the crawler scripts share.
import sys, os, math, random
import bpy, bmesh
from mathutils import Vector, Matrix, noise
from cwchar_lib import *

TAU = 2 * math.pi

def args():
    """-- <out dir> [name ...]"""
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        sys.exit("usage: -- <out dir> [name ...]")
    out = os.path.abspath(argv[0])
    os.makedirs(out, exist_ok=True)
    return out, argv[1:]

def _uv_planar(bm, uv):
    for f in bm.faces:
        for l in f.loops:
            co = l.vert.co
            l[uv].uv = (co.x + co.y * 0.7, co.z + co.y * 0.3)

def lathe(M, bone, prof, color, seg=12, group=0, w=None, noise_amt=0.1, c=(0, 0, 0), rot=(0, 0, 0), sx=1.0, sy=1.0, cap_top=True, cap_bot=True, emissive=False, a0=0.0, a1=None):
    """A surface of revolution about the local z axis: prof is [(radius, z), ...] from bottom to top. Caps close the
    ends when their radius is not 0. sx, sy squash it into an oval. Placed at c, rotated by rot (x, y, z euler).
    With a1, only the arc from a0 to a1 (radians, open, no caps)."""
    # wind every profile the same way (outside going up): a profile drawn top to bottom is reversed
    # (a closed loop or one ending on the axis by its turning; an open band by which end is higher)
    closed = prof[0] == prof[-1]
    if closed or prof[0][0] <= 1e-6 or prof[-1][0] <= 1e-6:
        area = sum(prof[k][0] * prof[(k + 1) % len(prof)][1] - prof[(k + 1) % len(prof)][0] * prof[k][1] for k in range(len(prof)))
        flip = area < 0
    else:
        flip = prof[-1][1] < prof[0][1]
    if flip: prof = prof[::-1]; cap_top, cap_bot = cap_bot, cap_top
    bm = bmesh.new()
    arc = a1 is not None
    if arc:
        cap_top = cap_bot = False
    npt = seg + 1 if arc else seg
    span = (a1 - a0) if arc else TAU
    rings = []
    for (r, z) in prof:
        if r <= 1e-6:
            rings.append([bm.verts.new((0, 0, z))])
        else:
            rings.append([bm.verts.new((r * sx * math.cos(a0 + span * k / seg), r * sy * math.sin(a0 + span * k / seg), z)) for k in range(npt)])
    for j in range(len(rings) - 1):
        A, B = rings[j], rings[j + 1]
        if len(A) == 1 and len(B) == 1: continue
        for k in range(seg):
            k2 = (k + 1) % npt
            if len(A) == 1:
                bm.faces.new((A[0], B[k2], B[k]))
            elif len(B) == 1:
                bm.faces.new((A[k], A[k2], B[0]))
            else:
                bm.faces.new((A[k], A[k2], B[k2], B[k]))
    if arc and closed and all(len(r_) > 1 for r_ in rings):
        # an arc of a closed profile: close its two cut ends with the profile's outline
        bm.faces.new([r_[0] for r_ in rings[:-1]])
        bm.faces.new([r_[-1] for r_ in rings[:-1]][::-1])
    if cap_bot and len(rings[0]) > 1:
        bm.faces.new(rings[0][::-1])
    if cap_top and len(rings[-1]) > 1:
        bm.faces.new(rings[-1])
    bm.normal_update()
    uv = bm.loops.layers.uv.new()
    for f in bm.faces:
        for l in f.loops:
            co = l.vert.co
            l[uv].uv = (math.atan2(co.y, co.x) / TAU * 4, co.z * 2)
    bm.verts.index_update()
    m = Matrix.Translation(c) @ (Matrix.Rotation(rot[2], 4, 'Z') @ Matrix.Rotation(rot[1], 4, 'Y') @ Matrix.Rotation(rot[0], 4, 'X'))
    M._add(bm, m, w if w is not None else {bone: 1.0}, color, group, True, (1, 1), noise_amt, emissive)
    bm.free()

def prism(M, bone, pts, z0, z1, color, group=0, w=None, noise_amt=0.1, c=(0, 0, 0), rot=(0, 0, 0), bevel=0.0, taper=1.0, smooth=False):
    """A prism from a 2D outline (counter-clockwise, in the local xy plane) from z0 to z1, its top scaled by
    taper; bevel rounds its edges. Placed at c, rotated by rot."""
    area = sum(pts[k][0] * pts[(k + 1) % len(pts)][1] - pts[(k + 1) % len(pts)][0] * pts[k][1] for k in range(len(pts)))
    if (area < 0) != (z1 < z0): pts = pts[::-1]          # counter-clockwise seen from the top end, whatever was passed
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in pts]
    hi = [bm.verts.new((x * taper, y * taper, z1)) for x, y in pts]
    bm.faces.new(hi)
    bm.faces.new(lo[::-1])
    n = len(pts)
    for k in range(n):
        bm.faces.new((lo[k], lo[(k + 1) % n], hi[(k + 1) % n], hi[k]))
    bm.normal_update()
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=1, affect='EDGES', clamp_overlap=True)
    # the writer fans every polygon from its first corner: cut concave outlines into triangles first
    big = [f for f in bm.faces if len(f.verts) > 4]
    if big: bmesh.ops.triangulate(bm, faces=big, quad_method='BEAUTY', ngon_method='EAR_CLIP')
    uv = bm.loops.layers.uv.new()
    _uv_planar(bm, uv)
    bm.verts.index_update()
    m = Matrix.Translation(c) @ (Matrix.Rotation(rot[2], 4, 'Z') @ Matrix.Rotation(rot[1], 4, 'Y') @ Matrix.Rotation(rot[0], 4, 'X'))
    M._add(bm, m, w if w is not None else {bone: 1.0}, color, group, smooth, (1, 1), noise_amt)
    bm.free()

def mesh(M, bone, verts, faces, color, group=0, w=None, noise_amt=0.1, smooth=False, emissive=False):
    """Raw polygons (Blender axes)."""
    bm = bmesh.new()
    vs = [bm.verts.new(v) for v in verts]
    for f in faces:
        bm.faces.new([vs[k] for k in f])
    big = [f for f in bm.faces if len(f.verts) > 4]
    if big: bmesh.ops.triangulate(bm, faces=big, quad_method='BEAUTY', ngon_method='EAR_CLIP')
    bm.normal_update()
    uv = bm.loops.layers.uv.new()
    _uv_planar(bm, uv)
    bm.verts.index_update()
    M._add(bm, Matrix.Identity(4), w if w is not None else {bone: 1.0}, color, group, smooth, (1, 1), noise_amt, emissive)
    bm.free()

def colvar(c, k):
    return (c[0] * k, c[1] * k, c[2] * k)

def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))

# ───────────────────────── bake + write ─────────────────────────────

def ground(clip, eps=0.0):
    """Moves every frame so its lowest vertex sits on the floor (z = eps)."""
    for (pos, nrm, lv) in clip:
        mz = min(p.z for p in pos)
        for p in pos:
            p.z -= mz - eps

def finish(name, M, poses, OUT, grounded=True, ao_samples=40):
    """Builds the rig, bakes ambient occlusion and every clip, grounds the floor-standing ones, writes the file."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ob, ao = build_object(M, name)
    pose_frame(ao, {})
    ao_col = bake_ao(ob, M, ao_samples)
    clips = [bake_clip(ob, M, ao, c) for c in poses]
    if grounded:
        for c in clips:
            ground(c)
    size = write_cwchar(os.path.join(OUT, name + ".cwchar"), M, ao_col, clips, ob)
    tris = [0] * (max(M.grp) + 1)
    for p in ob.data.polygons:
        tris[p.material_index] += len(p.vertices) - 2
    print("CRAWLER %s: %d verts, tris %s, clips %s, %d bytes" % (name, len(M.v), tris, [len(c) for c in clips], size))
    return tris

def rigid(name, M, frames, OUT, grounded=True, ao_samples=40):
    """A prop: frames are poses of its bones (all one clip unless frames is a list of lists)."""
    clips = frames if frames and isinstance(frames[0], list) else [frames or [{}]]
    return finish(name, M, clips, OUT, grounded, ao_samples)

def pose_frame_s(ao, pose):
    """pose_frame with scale: a value is (rx, ry, rz[, lx, ly, lz[, sx, sy, sz]]) in the bone's local axes."""
    for pb in ao.pose.bones:
        pb.rotation_euler = (0, 0, 0); pb.location = (0, 0, 0); pb.scale = (1, 1, 1)
    for bn, v in pose.items():
        pb = ao.pose.bones[bn]
        pb.rotation_euler = (v[0], v[1], v[2])
        if len(v) > 3: pb.location = (v[3], v[4], v[5])
        if len(v) > 6: pb.scale = (v[6], v[7], v[8])
    bpy.context.view_layer.update()

def bake_clip_s(ob, ao, poses):
    frames = []
    for pose in poses:
        pose_frame_s(ao, pose)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        frames.append(([v.co.copy() for v in me.vertices], [tuple(n.vector) for n in me.corner_normals], [l.vertex_index for l in me.loops]))
        ev.to_mesh_clear()
    return frames

def finish_s(name, M, poses, OUT, grounded=True, ao_samples=40, deform=None, ao_scale=1.0, ao_deep=None):
    """finish with scalable bones; deform(frame_index_in_clip, clip_index, positions) may move vertices after
    the rig (positions are Blender Vectors, edited in place; normals are recomputed). ao_deep: (strength, boost, floor)
    for a deeper occlusion bake (bake_ao); None keeps the original."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ob, ao = build_object(M, name)
    pose_frame_s(ao, {})
    ao_col = [1 - (1 - a) * ao_scale for a in bake_ao(ob, M, ao_samples, *(ao_deep or ()))]
    clips = []
    for ci, c in enumerate(poses):
        fr = bake_clip_s(ob, ao, c)
        if deform:
            ob.modifiers.clear()
            me = ob.data
            rest = [v.co.copy() for v in me.vertices]
            out = []
            for fi, (pos, nrm, lv) in enumerate(fr):
                deform(fi, ci, pos)
                for k, v in enumerate(me.vertices): v.co = pos[k]
                me.update()
                out.append(([v.co.copy() for v in me.vertices], [tuple(n.vector) for n in me.corner_normals], lv))
            for k, v in enumerate(me.vertices): v.co = rest[k]
            me.update()
            mod = ob.modifiers.new("rig", 'ARMATURE'); mod.object = ao
            fr = out
        clips.append(fr)
    if grounded:
        for c in clips:
            ground(c)
    size = write_cwchar(os.path.join(OUT, name + ".cwchar"), M, ao_col, clips, ob)
    tris = [0] * (max(M.grp) + 1)
    for p in ob.data.polygons:
        tris[p.material_index] += len(p.vertices) - 2
    print("CRAWLER %s: %d verts, tris %s, clips %s, %d bytes" % (name, len(M.v), tris, [len(c) for c in clips], size))
    return tris

# ───────────────────────── kit pieces ─────────────────────────────────

def pillow(M, bone, c, u, v, n, hu, hv, d, ch, color, group=0, w=None, noise_amt=0.1, jit=None, tri_top=0.0, keep="brtl"):
    """A chamfered stone face standing out of a plane: centred at c on the plane, spanning +-hu along u and
    +-hv along v, its front d out along n, the front inset by ch (the chamfer). Five quads, no back. jit is an
    optional list of 4 offsets (along n) for the front corners (a tilted, uneven face). tri_top != 0 splits the
    front into four triangles round a centre pushed out by tri_top (a domed or dished face)."""
    c = Vector(c); u = Vector(u); v = Vector(v); n = Vector(n)
    jit = jit or (0, 0, 0, 0)
    back = [c + u * a * hu + v * b * hv for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    front = [c + n * (d + jit[k]) + u * a * (hu - ch) + v * b * (hv - ch) for k, (a, b) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1)))]
    verts = back + front
    if u.cross(v).dot(n) > 0:
        faces = [(4, 5, 6, 7)] if not tri_top else []
        sides = [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    else:
        faces = [(7, 6, 5, 4)] if not tri_top else []
        sides = [(4, 5, 1, 0), (5, 6, 2, 1), (6, 7, 3, 2), (7, 4, 0, 3)]
    # keep: which chamfers to build (b, r, t, l = the -v, +u, +v, -u sides): a brick seen from above needs only its top
    sides = [sd for sd, ch_ in zip(sides, "brtl") if ch_ in keep]
    if tri_top:
        verts.append(c + n * (d + sum(jit) / 4 + tri_top))
        if u.cross(v).dot(n) > 0:
            faces = [(4, 5, 8), (5, 6, 8), (6, 7, 8), (7, 4, 8)]
        else:
            faces = [(5, 4, 8), (6, 5, 8), (7, 6, 8), (4, 7, 8)]
    mesh(M, bone, [tuple(p) for p in verts], faces + sides, color, group=group, w=w, noise_amt=noise_amt)

def cuboid(M, bone, lo, hi, color, group=0, w=None, noise_amt=0.1, faces="xXyYzZ"):
    """An axis-aligned box from lo to hi with only the named faces (x = -x side, X = +x side, ...)."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    V = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    F = {"z": (3, 2, 1, 0), "Z": (4, 5, 6, 7), "y": (0, 1, 5, 4), "Y": (2, 3, 7, 6), "x": (3, 0, 4, 7), "X": (1, 2, 6, 5)}
    mesh(M, bone, V, [F[k] for k in faces], color, group=group, w=w, noise_amt=noise_amt)
