# First person weapon models with gloved hands, one glb each (no animation: the game kicks, sways and drops them).
# Origin is the grip, the muzzle points +Y (glTF -Z, the direction the camera looks). Usage: blender -b --python weapons.py -- <out_dir> [sheet.png]
import bpy, bmesh, math, sys, os
from mathutils import Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

H = math.pi / 2

def mats():
    return dict(
        dark=material('vm_dark', (0.07, 0.075, 0.08), 0.42, 0.6),
        steel=material('vm_steel', (0.55, 0.57, 0.6), 0.28, 1.0),
        body=material('vm_body', (0.26, 0.29, 0.32), 0.4, 0.45),
        wood=material('vm_wood', (0.30, 0.17, 0.08), 0.55, 0.0),
        orange=material('vm_orange', (0.9, 0.35, 0.05), 0.4, 0.2),
        glove=material('vm_glove', (0.07, 0.075, 0.08), 0.8, 0.0),
        sleeve=material('vm_sleeve', (0.12, 0.14, 0.12), 0.85, 0.0),
        glow_o=material('vm_glow_orange', (0.2, 0.08, 0.02), 0.2, 0.0, emit=(1.0, 0.45, 0.1), emit_strength=5.0),
        glow_c=material('vm_glow_cyan', (0.05, 0.15, 0.2), 0.2, 0.0, emit=(0.3, 0.85, 1.0), emit_strength=6.0),
        glow_v=material('vm_glow_violet', (0.1, 0.05, 0.2), 0.2, 0.0, emit=(0.6, 0.4, 1.0), emit_strength=7.0),
        lens=material('vm_lens', (0.0, 0.0, 0.0), 0.05, 0.0, emit=(1.0, 0.1, 0.05), emit_strength=3.0),
    )

def hand(M, pos, rot=(0, 0, 0), flip=1):
    o = []
    o.append(bevel_box('palm', (0.075, 0.1, 0.07), pos, 0.025, 3, M['glove'], rot))
    for i in range(4):
        o.append(bevel_box('finger%d' % i, (0.016, 0.07, 0.016), (pos[0] + 0.037 * flip, pos[1] + 0.01 - i * 0.024 + 0.03, pos[2] - 0.02), 0.006, 2, M['glove']))
    o.append(bevel_box('thumb', (0.02, 0.06, 0.02), (pos[0] - 0.03 * flip, pos[1] + 0.03, pos[2] + 0.03), 0.007, 2, M['glove'], (0, 0, 0.5)))
    return o

def sleeve(M, start, end, r=0.045):
    a, b = Vector(start), Vector(end)
    d = b - a
    ob = cyl('sleeve', r, d.length, tuple((a + b) / 2), verts=16, mat=M['sleeve'])
    ob.rotation_mode = 'QUATERNION'
    ob.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    cuff = cyl('cuff', r * 1.12, 0.03, tuple(a + d * 0.0 + d.normalized() * 0.0), verts=16, mat=M['dark'])
    cuff.rotation_mode = 'QUATERNION'
    cuff.rotation_quaternion = ob.rotation_quaternion
    cuff.location = a
    return [ob, cuff]

def tube_y(name, r, y0, y1, loc_xz, mat, verts=20, r2=None):
    ob = cyl(name, r, y1 - y0, (loc_xz[0], (y0 + y1) / 2, loc_xz[1]), (H, 0, 0), verts, mat, 0.004, r2)
    return ob

def pulse(M):
    o = []
    o.append(bevel_box('receiver', (0.075, 0.42, 0.11), (0, 0.06, 0.03), 0.02, 3, M['body']))
    o.append(bevel_box('top', (0.05, 0.4, 0.03), (0, 0.06, 0.1), 0.008, 2, M['dark']))
    o.append(bevel_box('handguard', (0.085, 0.3, 0.09), (0, 0.36, 0.02), 0.025, 3, M['dark']))
    for i in range(5):
        o.append(bevel_box('vent%d' % i, (0.09, 0.012, 0.03), (0, 0.24 + i * 0.05, 0.03), 0.003, 1, M['steel']))
    o.append(tube_y('barrel', 0.016, 0.45, 0.72, (0, 0.035), M['steel'], 14))
    o.append(tube_y('muzzle', 0.028, 0.68, 0.74, (0, 0.035), M['dark'], 14))
    o.append(bevel_box('cell', (0.03, 0.2, 0.028), (0, 0.17, 0.087), 0.008, 2, M['glow_o']))
    o.append(bevel_box('mag', (0.05, 0.08, 0.17), (0, 0.08, -0.12), 0.018, 3, M['dark']))
    o.append(bevel_box('stock', (0.06, 0.16, 0.12), (0, -0.2, 0.0), 0.025, 3, M['body']))
    o.append(bevel_box('grip', (0.05, 0.07, 0.15), (0, -0.07, -0.1), 0.02, 3, M['dark'], (0.25, 0, 0)))
    o.append(bevel_box('sight', (0.03, 0.05, 0.04), (0, 0.0, 0.13), 0.01, 2, M['dark']))
    o.append(bevel_box('dot', (0.02, 0.004, 0.02), (0, 0.027, 0.135), 0.002, 1, M['lens']))
    o += hand(M, (0, -0.06, -0.01), (0.2, 0, 0))
    o += hand(M, (0.0, 0.34, -0.05), (0, 0, 0))
    o += sleeve(M, (0.03, -0.08, -0.06), (0.1, -0.34, -0.2))
    return o, 0.78

def scatter(M):
    o = []
    o.append(bevel_box('receiver', (0.09, 0.3, 0.12), (0, 0.0, 0.03), 0.025, 3, M['body']))
    for sx in (-0.03, 0.03):
        o.append(tube_y('barrel', 0.034, 0.1, 0.78, (sx, 0.05), M['dark'], 18))
        o.append(tube_y('muzzle', 0.04, 0.74, 0.8, (sx, 0.05), M['steel'], 18))
    o.append(bevel_box('rib', (0.03, 0.62, 0.014), (0, 0.45, 0.1), 0.004, 1, M['steel']))
    o.append(bevel_box('forend', (0.11, 0.26, 0.09), (0, 0.3, -0.03), 0.03, 3, M['wood']))
    o.append(bevel_box('stock', (0.07, 0.3, 0.14), (0, -0.26, 0.0), 0.03, 3, M['wood'], (-0.12, 0, 0)))
    o.append(bevel_box('plate', (0.075, 0.03, 0.16), (0, -0.42, -0.03), 0.01, 2, M['dark']))
    o.append(bevel_box('grip', (0.055, 0.07, 0.15), (0, -0.1, -0.1), 0.02, 3, M['wood'], (0.25, 0, 0)))
    o.append(bevel_box('bead', (0.012, 0.012, 0.03), (0, 0.78, 0.115), 0.003, 1, M['glow_o']))
    o += hand(M, (0, -0.09, -0.03), (0.2, 0, 0))
    o += hand(M, (0, 0.3, -0.07))
    o += sleeve(M, (0.03, -0.1, -0.08), (0.1, -0.36, -0.22))
    return o, 0.8

def rocket(M):
    o = []
    o.append(tube_y('tube', 0.085, -0.25, 0.62, (0, 0.0), M['body'], 28))
    o.append(tube_y('mouth', 0.1, 0.58, 0.7, (0, 0.0), M['dark'], 28, 0.1))
    o.append(tube_y('inner', 0.07, 0.69, 0.71, (0, 0.0), M['glow_o'], 20))
    o.append(tube_y('rear', 0.11, -0.3, -0.18, (0, 0.0), M['dark'], 24, 0.09))
    for y in (0.0, 0.3):
        o.append(tube_y('band', 0.092, y, y + 0.03, (0, 0.0), M['orange'], 28))
    o.append(bevel_box('sight', (0.02, 0.08, 0.05), (0, 0.1, 0.12), 0.008, 2, M['dark']))
    o.append(bevel_box('guard', (0.04, 0.2, 0.05), (0.0, 0.4, 0.1), 0.015, 2, M['steel']))
    o.append(bevel_box('grip', (0.055, 0.07, 0.15), (0, 0.04, -0.17), 0.02, 3, M['dark'], (0.2, 0, 0)))
    o.append(bevel_box('fgrip', (0.05, 0.06, 0.12), (0, 0.38, -0.15), 0.02, 3, M['dark']))
    o.append(bevel_box('stripe', (0.1, 0.05, 0.02), (0, 0.2, 0.085), 0.005, 1, M['glow_o']))
    o += hand(M, (0, 0.04, -0.18), (0.2, 0, 0))
    o += hand(M, (0, 0.38, -0.2))
    o += sleeve(M, (0.03, -0.02, -0.2), (0.12, -0.3, -0.32))
    return o, 0.71

def rail(M):
    o = []
    o.append(bevel_box('spine', (0.06, 0.8, 0.07), (0, 0.22, 0.03), 0.02, 3, M['dark']))
    o.append(bevel_box('chan', (0.02, 0.62, 0.02), (0, 0.32, 0.07), 0.005, 2, M['glow_v']))
    for i in range(5):
        y = 0.0 + i * 0.12
        o.append(tube_y('coil%d' % i, 0.07 - i * 0.004, y, y + 0.05, (0, 0.03), M['steel'], 24))
        o.append(tube_y('coilg%d' % i, 0.062 - i * 0.004, y + 0.012, y + 0.04, (0, 0.03), M['glow_v'], 20))
    o.append(bevel_box('prong_l', (0.014, 0.28, 0.05), (-0.04, 0.68, 0.03), 0.004, 2, M['steel']))
    o.append(bevel_box('prong_r', (0.014, 0.28, 0.05), (0.04, 0.68, 0.03), 0.004, 2, M['steel']))
    o.append(bevel_box('tip', (0.02, 0.02, 0.05), (0, 0.8, 0.03), 0.004, 1, M['glow_v']))
    o.append(bevel_box('body', (0.09, 0.22, 0.12), (0, -0.18, 0.0), 0.03, 3, M['body']))
    o.append(bevel_box('grip', (0.05, 0.07, 0.15), (0, -0.1, -0.1), 0.02, 3, M['dark'], (0.25, 0, 0)))
    o.append(bevel_box('fgrip', (0.05, 0.06, 0.1), (0, 0.3, -0.07), 0.02, 3, M['dark']))
    o += hand(M, (0, -0.1, -0.03), (0.2, 0, 0))
    o += hand(M, (0, 0.3, -0.08))
    o += sleeve(M, (0.03, -0.12, -0.06), (0.1, -0.38, -0.2))
    return o, 0.82

WEAPONS = [('pulse', pulse), ('scatter', scatter), ('rocket', rocket), ('rail', rail)]

def main():
    a = args(); out = a[0]; sheet = a[1] if len(a) > 1 else None
    for name, fn in WEAPONS:
        reset()
        M = mats()
        objs, muzzle = fn(M)
        ob = join(objs, 'vm_' + name)
        for p in ob.data.polygons:
            p.use_smooth = True
        print('MUZZLE', name, muzzle)
        if sheet:
            render_sheet(sheet.replace('.png', '_%s.png' % name), [ob], [(math.pi / 2, 0.2), (0.4, 0.4)], size=1.6, center=(0, 0.25, 0))
        export_glb(os.path.join(out, 'vm_%s.glb' % name), anims=False)

main()
