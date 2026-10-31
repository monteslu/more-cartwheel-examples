# The pickups, one glb each: a first aid case, a chest plate, three guns and an ammunition crate, each with a part that glows in
# its kind's colour so it reads across a hall. Origin at the middle: the game spins and bobs them over a lit disc on the floor.
# Usage: blender -b --python pickups.py -- <out_dir> [sheet.png]
import bpy, bmesh, math, sys, os
from mathutils import Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

H = math.pi / 2

def base_mats():
    return dict(
        dark=material('pk_dark', (0.07, 0.075, 0.08), 0.5, 0.7),
        steel=material('pk_steel', (0.5, 0.52, 0.55), 0.35, 1.0),
        wood=material('pk_wood', (0.4, 0.23, 0.11), 0.65, 0.0),
        white=material('pk_white', (0.72, 0.72, 0.68), 0.5, 0.0),
        olive=material('pk_olive', (0.14, 0.17, 0.09), 0.75, 0.0),
        blue=material('pk_blue', (0.08, 0.16, 0.3), 0.4, 0.4),
        orange=material('pk_orange', (0.7, 0.28, 0.04), 0.5, 0.2),
    )

def glow(rgb, strength=4.0):
    return material('pk_glow', (rgb[0] * 0.3, rgb[1] * 0.3, rgb[2] * 0.3), 0.3, 0.0, emit=rgb, emit_strength=strength)

def tube_x(name, r, x0, x1, y, z, mat, verts=16, r2=None):
    return cyl(name, r, x1 - x0, ((x0 + x1) / 2, y, z), (0, H, 0), verts, mat, 0.0, r2)

def health(M):
    g = glow((1.0, 0.12, 0.08), 3.5)
    o = [bevel_box('case', (0.56, 0.2, 0.4), (0, 0, 0), 0.035, 3, M['white'])]
    o.append(bevel_box('seam', (0.565, 0.205, 0.008), (0, 0, 0.06), 0.001, 1, M['dark']))
    for sy in (-1, 1):  # a red cross on both faces
        o.append(bevel_box('crossh', (0.24, 0.012, 0.075), (0, sy * 0.1, -0.03), 0.004, 1, g))
        o.append(bevel_box('crossv', (0.075, 0.012, 0.24), (0, sy * 0.1, -0.03), 0.004, 1, g))
    o.append(bevel_box('handle', (0.2, 0.04, 0.03), (0, 0, 0.255), 0.012, 2, M['dark']))
    for sx in (-1, 1):
        o.append(bevel_box('post', (0.03, 0.04, 0.06), (sx * 0.1, 0, 0.225), 0.008, 1, M['dark']))
        o.append(bevel_box('latch', (0.05, 0.016, 0.04), (sx * 0.16, -0.105, 0.09), 0.004, 1, M['steel']))
        o.append(bevel_box('latch', (0.05, 0.016, 0.04), (sx * 0.16, 0.105, 0.09), 0.004, 1, M['steel']))
    return o

def armor(M):
    g = glow((0.35, 0.65, 1.0), 4.5)
    o = [bevel_box('chest', (0.48, 0.14, 0.34), (0, 0, 0.1), 0.05, 4, M['blue'])]
    o.append(bevel_box('belly', (0.4, 0.13, 0.22), (0, 0.005, -0.17), 0.045, 4, M['blue']))
    o.append(bevel_box('abs', (0.34, 0.15, 0.015), (0, 0.003, -0.06), 0.003, 1, M['dark']))
    for sx in (-1, 1):
        o.append(bevel_box('shoulder', (0.17, 0.2, 0.09), (sx * 0.3, 0, 0.27), 0.04, 3, M['steel'], (0, sx * -0.35, 0)))
        o.append(bevel_box('strap', (0.06, 0.16, 0.14), (sx * 0.14, 0, 0.31), 0.012, 2, M['dark']))
    o.append(bevel_box('core', (0.16, 0.02, 0.06), (0, -0.075, 0.13), 0.006, 1, g))
    o.append(bevel_box('core2', (0.16, 0.02, 0.06), (0, 0.075, 0.13), 0.006, 1, g))
    return o

def scatter(M):
    g = glow((1.0, 0.75, 0.25), 4.5)
    o = []
    for sz in (-0.022, 0.022):
        o.append(tube_x('barrel', 0.022, -0.08, 0.5, 0, 0.05 + sz, M['steel'], 14))
    o.append(bevel_box('rib', (0.5, 0.02, 0.006), (0.22, 0, 0.1), 0.002, 1, M['dark']))
    o.append(bevel_box('recv', (0.24, 0.07, 0.11), (-0.17, 0, 0.04), 0.014, 2, M['dark']))
    o.append(bevel_box('pump', (0.2, 0.08, 0.06), (0.16, 0, -0.012), 0.02, 3, M['wood']))
    o.append(bevel_box('stock', (0.3, 0.055, 0.1), (-0.42, 0, -0.01), 0.018, 2, M['wood'], (0, -0.15, 0)))
    o.append(bevel_box('grip', (0.05, 0.05, 0.1), (-0.25, 0, -0.06), 0.012, 2, M['wood'], (0, -0.4, 0)))
    o.append(bevel_box('stripe', (0.1, 0.074, 0.012), (-0.17, 0, 0.098), 0.002, 1, g))
    for i in range(3):
        o.append(tube_x('shell', 0.012, -0.235 + i * 0.05, -0.19 + i * 0.05, -0.04, 0.03, g, 8))
    return o

def rocket(M):
    g = glow((1.0, 0.4, 0.2), 5.0)
    o = [tube_x('tube', 0.075, -0.42, 0.42, 0, 0, M['dark'], 24)]
    o.append(tube_x('mouth', 0.09, 0.38, 0.5, 0, 0, M['steel'], 24, 0.085))
    o.append(tube_x('rear', 0.095, -0.5, -0.4, 0, 0, M['steel'], 24, 0.078))
    o.append(tube_x('inner', 0.06, 0.495, 0.505, 0, 0, g, 18))
    for x in (-0.15, 0.17):
        o.append(tube_x('band', 0.079, x, x + 0.035, 0, 0, M['orange'], 24))
    o.append(bevel_box('sight', (0.1, 0.03, 0.05), (0.05, 0, 0.1), 0.008, 1, M['dark']))
    o.append(bevel_box('grip', (0.05, 0.05, 0.12), (-0.1, 0, -0.12), 0.012, 2, M['dark'], (0, 0.2, 0)))
    o.append(bevel_box('fgrip', (0.05, 0.05, 0.1), (0.15, 0, -0.11), 0.012, 2, M['dark']))
    return o

def rail(M):
    g = glow((0.75, 0.45, 1.0), 6.0)
    o = [bevel_box('spine', (1.0, 0.05, 0.05), (0.05, 0, 0.01), 0.012, 2, M['dark'])]
    for i in range(5):
        x = -0.15 + i * 0.13
        o.append(tube_x('coil', 0.06 - i * 0.004, x, x + 0.055, 0, 0.01, M['steel'], 18))
        o.append(tube_x('glow', 0.052 - i * 0.004, x + 0.01, x + 0.045, 0, 0.01, g, 16))
    for sy in (-1, 1):
        o.append(bevel_box('prong', (0.26, 0.012, 0.04), (0.5, sy * 0.032, 0.01), 0.003, 1, M['steel']))
    o.append(bevel_box('body', (0.26, 0.075, 0.1), (-0.32, 0, 0), 0.02, 2, M['dark']))
    o.append(bevel_box('grip', (0.05, 0.05, 0.1), (-0.3, 0, -0.095), 0.012, 2, M['dark'], (0, -0.3, 0)))
    return o

def ammo(M):
    g = glow((1.0, 0.95, 0.45), 4.5)
    o = [bevel_box('crate', (0.46, 0.3, 0.22), (0, 0, -0.05), 0.02, 2, M['olive'])]
    o.append(bevel_box('lid', (0.48, 0.32, 0.03), (0, 0, 0.07), 0.008, 2, M['dark']))
    for sx in (-1, 1):
        o.append(bevel_box('corner', (0.03, 0.31, 0.225), (sx * 0.2, 0, -0.05), 0.004, 1, M['dark']))
        o.append(bevel_box('handle', (0.012, 0.11, 0.022), (sx * 0.24, 0, 0.0), 0.004, 1, M['steel']))
    o.append(bevel_box('stencil', (0.2, 0.304, 0.045), (0, 0, -0.05), 0.002, 1, M['orange']))
    for i in range(3):  # cells standing in the open lid
        o.append(cyl('cell', 0.042, 0.11, (-0.11 + i * 0.11, 0, 0.14), (0, 0, 0), 12, g))
        o.append(cyl('cap', 0.044, 0.018, (-0.11 + i * 0.11, 0, 0.2), (0, 0, 0), 12, M['steel']))
    return o

KINDS = [('health', health), ('armor', armor), ('scatter', scatter), ('rocket', rocket), ('rail', rail), ('ammo', ammo)]

def main():
    a = args(); out = a[0]; sheet = a[1] if len(a) > 1 else None
    for name, fn in KINDS:
        reset()
        M = base_mats()
        ob = join(fn(M), 'pk_' + name)
        ob.data.polygons.foreach_set('use_smooth', [True] * len(ob.data.polygons))
        try:
            ob.data.set_sharp_from_angle(angle=0.7)
        except Exception:
            pass
        box_uv(ob, 0.5)
        print('PICKUP', name, 'tris', sum(len(p.vertices) - 2 for p in ob.data.polygons))
        if sheet:
            render_sheet(sheet.replace('.png', '_%s.png' % name), [ob], [(0.5, 0.35)], res=(600, 450), size=1.3, center=(0, 0, 0))
        export_glb(os.path.join(out, 'pk_%s.glb' % name), anims=False)

main()
