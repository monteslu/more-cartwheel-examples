# Arena dressing meshes, one glb: every object is "<prop>__<material>" so the game can merge each
# material's pieces into its level meshes (origin = centre of the base, front = +Y).
# Usage: blender -b --python props.py -- <out_dir> [sheet.png]
import bpy, bmesh, math, sys, os, random
from mathutils import Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

M = {}

def mat(name, c, r, m=0.0):
    M[name] = material(name, c, r, m)
    return M[name]

def part(prop, matname, ob):
    ob.name = '%s__%s' % (prop, matname)
    if not ob.data.materials:
        ob.data.materials.append(M[matname])
    return ob

def finish(objs_by_mat):
    out = []
    for key, objs in objs_by_mat.items():
        prop, m = key
        ob = join(objs, '%s__%s' % (prop, m))
        ob.data.materials.clear(); ob.data.materials.append(M[m])
        box_uv(ob, 1.0)
        out.append(ob)
    return out

def group():
    return {}

def put(g, prop, m, ob):
    g.setdefault((prop, m), []).append(ob)

def crate(g, prop, s):
    put(g, prop, 'crate', bevel_box('c', (s, s, s * 0.98), (0, 0, s * 0.49), 0.03 * s, 3))
    t = 0.09 * s
    for sx in (-1, 1):
        for sy in (-1, 1):
            put(g, prop, 'crate_frame', bevel_box('post', (t, t, s), (sx * (s / 2 - t / 2), sy * (s / 2 - t / 2), s / 2), 0.012, 2))
    for z in (t / 2, s - t / 2):
        for ax in (0, 1):
            size = (s, t, t) if ax == 0 else (t, s, t)
            for k in (-1, 1):
                loc = (0, k * (s / 2 - t / 2), z) if ax == 0 else (k * (s / 2 - t / 2), 0, z)
                put(g, prop, 'crate_frame', bevel_box('rail', size, loc, 0.01, 2))
    for k in (-1, 1):
        put(g, prop, 'crate_frame', bevel_box('mid', (s * 0.98, s * 1.012, 0.07 * s), (0, 0, s / 2 + k * 0.0), 0.008, 1))

def barrel(g):
    put(g, 'barrel', 'barrel', cyl('b', 0.3, 0.9, (0, 0, 0.45), verts=24, bevel=0.012))
    for z in (0.12, 0.45, 0.78):
        put(g, 'barrel', 'metal', cyl('ring', 0.315, 0.04, (0, 0, z), verts=24))
    put(g, 'barrel', 'metal', cyl('lid', 0.27, 0.02, (0, 0, 0.905), verts=24))
    put(g, 'barrel', 'dark', cyl('cap', 0.05, 0.03, (0.12, 0.08, 0.92), verts=12))

def lamp(g):
    put(g, 'lamp', 'dark', bevel_box('arm', (0.06, 0.4, 0.06), (0, 0.2, 0), 0.015, 2))
    put(g, 'lamp', 'dark', bevel_box('plate', (0.3, 0.03, 0.4), (0, 0.0, 0), 0.01, 2))
    put(g, 'lamp', 'metal', bevel_box('hood', (0.42, 0.3, 0.2), (0, 0.5, 0.1), 0.04, 3, rot=(0.0, 0.0, 0.0)))
    put(g, 'lamp', 'dark', bevel_box('rim', (0.44, 0.04, 0.22), (0, 0.66, 0.1), 0.01, 2))

def jersey(g):
    bm = bmesh.new()
    prof = [(-0.3, 0), (0.3, 0), (0.3, 0.12), (0.15, 0.55), (0.12, 0.8), (-0.12, 0.8), (-0.15, 0.55), (-0.3, 0.12)]
    L = 2.0
    f = []
    for y in (-L / 2, L / 2):
        f.append([bm.verts.new((x, y, z)) for x, z in prof])
    for i in range(len(prof)):
        j = (i + 1) % len(prof)
        bm.faces.new((f[0][i], f[0][j], f[1][j], f[1][i]))
    bmesh.ops.contextual_create(bm, geom=f[0]); bmesh.ops.contextual_create(bm, geom=f[1])
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    ob = new_obj('j', bm); ob.data.materials.append(M['concrete'])
    for p in ob.data.polygons: p.use_smooth = False
    put(g, 'jersey', 'concrete', ob)
    put(g, 'jersey', 'paint', bevel_box('stripe', (0.62, 0.5, 0.1), (0, 0, 0.5), 0.005, 1, rot=(0, 0, 0)))

def railing(g):
    L = 2.0
    for x in (-L / 2 + 0.04, 0, L / 2 - 0.04):
        put(g, 'railing', 'metal', bevel_box('post', (0.05, 0.05, 1.0), (x, 0, 0.5), 0.008, 2))
    for z in (0.95, 0.65, 0.35):
        put(g, 'railing', 'metal', cyl('rail', 0.022, L, (0, 0, z), (0, math.pi / 2, 0), verts=12))
    put(g, 'railing', 'dark', bevel_box('kick', (L, 0.02, 0.1), (0, 0, 0.05), 0.004, 1))

def pipe(g):
    L = 2.0
    put(g, 'pipe', 'metal', cyl('p', 0.11, L, (0, 0, 0), (0, math.pi / 2, 0), verts=20))
    for x in (-0.95, 0.0, 0.95):
        put(g, 'pipe', 'dark', cyl('flange', 0.15, 0.06, (x, 0, 0), (0, math.pi / 2, 0), verts=20))
    put(g, 'pipe', 'dark', bevel_box('clamp', (0.08, 0.05, 0.4), (0.5, 0, -0.12), 0.01, 2))
    put(g, 'pipe', 'dark', bevel_box('clamp2', (0.08, 0.05, 0.4), (-0.5, 0, -0.12), 0.01, 2))

def vent(g):
    put(g, 'vent', 'dark', bevel_box('frame', (1.2, 0.08, 0.8), (0, 0.04, 0.4), 0.02, 2))
    for i in range(7):
        put(g, 'vent', 'metal', bevel_box('louver', (1.05, 0.06, 0.04), (0, 0.075, 0.1 + i * 0.1), 0.006, 1, rot=(0.5, 0, 0)))

def planter(g):
    put(g, 'planter', 'concrete', bevel_box('p', (2.2, 1.0, 0.7), (0, 0, 0.35), 0.06, 3))
    random.seed(4)
    for i in range(9):
        x = -0.8 + i * 0.2
        put(g, 'planter', 'hedge', sphere('h', 0.34, (x, random.uniform(-0.1, 0.1), 0.85), (1, 1, 0.9), None, 12, 8))

def column(g):
    # a fluted 3.2 m round column on a square base with a flared capital (the game scales it with the pillar)
    put(g, 'column', 'concrete', bevel_box('base', (0.9, 0.9, 0.3), (0, 0, 0.15), 0.05, 3))
    put(g, 'column', 'concrete', cyl('shaft', 0.3, 2.6, (0, 0, 1.6), verts=20, bevel=0.0))
    put(g, 'column', 'metal', cyl('ring1', 0.33, 0.1, (0, 0, 0.4), verts=20))
    put(g, 'column', 'metal', cyl('ring2', 0.33, 0.1, (0, 0, 2.7), verts=20))
    put(g, 'column', 'concrete', cyl('cap', 0.3, 0.3, (0, 0, 3.05), verts=20, r2=0.55))
    put(g, 'column', 'concrete', bevel_box('abacus', (1.1, 1.1, 0.12), (0, 0, 3.26), 0.03, 2))

def console(g):
    put(g, 'console', 'dark', bevel_box('b', (0.9, 0.5, 1.1), (0, 0, 0.55), 0.05, 3))
    put(g, 'console', 'metal', bevel_box('top', (0.96, 0.56, 0.06), (0, 0, 1.12), 0.02, 2, rot=(0, 0, 0)))
    put(g, 'console', 'metal', bevel_box('screen', (0.7, 0.05, 0.4), (0, 0.15, 1.35), 0.02, 2, rot=(-0.45, 0, 0)))

def main():
    a = args(); out = a[0]; sheet = a[1] if len(a) > 1 else None
    reset()
    mat('crate', (0.72, 0.55, 0.3), 0.7)
    mat('crate_frame', (0.38, 0.27, 0.14), 0.65)
    mat('barrel', (0.7, 0.32, 0.12), 0.5, 0.4)
    mat('metal', (0.55, 0.56, 0.58), 0.4, 1.0)
    mat('dark', (0.1, 0.1, 0.11), 0.5, 0.7)
    mat('concrete', (0.7, 0.68, 0.64), 0.9)
    mat('paint', (0.95, 0.62, 0.08), 0.5)
    mat('hedge', (0.2, 0.5, 0.15), 0.9)
    g = group()
    crate(g, 'crate', 1.0)
    crate(g, 'crate_s', 0.6)
    barrel(g); lamp(g); jersey(g); railing(g); pipe(g); vent(g); planter(g); column(g); console(g)
    objs = finish(g)
    bpy.ops.object.select_all(action='DESELECT')
    if sheet:
        # lay the props out in a row for the contact sheet only
        names = []
        for o in objs:
            p = o.name.split('__')[0]
            if p not in names: names.append(p)
        for o in objs:
            o.location.x = names.index(o.name.split('__')[0]) * 3.0
        render_sheet(sheet, objs, [(0.5, 0.35)], res=(1800, 500), size=36, center=(15, 0, 1.0))
        for o in objs: o.location.x = 0
    export_glb(os.path.join(out, 'props.glb'), anims=False)

main()
