# First person weapon models with gloved hands, one glb each (no animation: the game kicks, sways and drops them).
# Origin is the grip, the muzzle points +Y (glTF -Z, the direction the camera looks). Usage: blender -b --python weapons.py -- <out_dir> [sheet.png]
# The camera sits above and to the left of the gun, so what the player reads is the top and left side: the detail goes there,
# and the left hand wraps the fore end from that side with its sleeve running back out of the lower left of the view.
# Parts keep box-projected uvs in gun metres; the game lays its worn paint, steel, wood and cloth maps over them by material name.
import bpy, bmesh, math, sys, os
from mathutils import Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

H = math.pi / 2

def mats():
    return dict(
        dark=material('vm_dark', (0.09, 0.095, 0.1), 0.5, 0.75),
        steel=material('vm_steel', (0.5, 0.52, 0.55), 0.32, 1.0),
        body=material('vm_body', (0.3, 0.33, 0.35), 0.55, 0.5),
        wood=material('vm_wood', (0.42, 0.25, 0.13), 0.6, 0.0),
        rubber=material('vm_rubber', (0.035, 0.035, 0.04), 0.82, 0.0),
        orange=material('vm_orange', (0.75, 0.3, 0.05), 0.5, 0.2),
        brass=material('vm_brass', (0.72, 0.5, 0.2), 0.32, 1.0),
        glove=material('vm_glove', (0.075, 0.07, 0.065), 0.72, 0.0),
        knuckle=material('vm_knuckle', (0.03, 0.03, 0.033), 0.55, 0.0),
        sleeve=material('vm_sleeve', (0.07, 0.08, 0.062), 0.92, 0.0),
        glow_o=material('vm_glow_orange', (0.2, 0.08, 0.02), 0.2, 0.0, emit=(1.0, 0.45, 0.1), emit_strength=5.0),
        glow_c=material('vm_glow_cyan', (0.05, 0.15, 0.2), 0.2, 0.0, emit=(0.3, 0.85, 1.0), emit_strength=6.0),
        glow_v=material('vm_glow_violet', (0.1, 0.05, 0.2), 0.2, 0.0, emit=(0.6, 0.4, 1.0), emit_strength=7.0),
        lens=material('vm_lens', (0.0, 0.0, 0.0), 0.05, 0.0, emit=(1.0, 0.1, 0.05), emit_strength=3.0),
    )

def tube(name, r, a, b, mat, verts=16, r2=None):
    a, b = Vector(a), Vector(b)
    d = b - a
    ob = cyl(name, r, d.length, tuple((a + b) / 2), verts=verts, mat=mat, r2=r2)
    ob.rotation_mode = 'QUATERNION'
    ob.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    return ob

def tube_y(name, r, y0, y1, loc_xz, mat, verts=20, r2=None):
    return cyl(name, r, y1 - y0, (loc_xz[0], (y0 + y1) / 2, loc_xz[1]), (H, 0, 0), verts, mat, 0.003, r2)

def finger(M, base, along, curl, r=0.0115, n=3, seg=0.03):
    """A finger as tapered segments: each one turns further by curl radians around x, with a knuckle pad on the first."""
    o, p = [], Vector(base)
    d = Vector(along).normalized()
    side = Vector((1, 0, 0))
    for i in range(n):
        q = p + d * seg * (1 - i * 0.12)
        o.append(tube('fseg', r * (1 - i * 0.14), p, q, M['glove'], 10, r * (1 - (i + 1) * 0.14)))
        o.append(sphere('fjoint', r * (1 - i * 0.14), tuple(q), mat=M['glove'], seg=10, rings=6))
        p = q
        d = (Matrix.Rotation(curl, 3, side) @ d).normalized()
    return o

def left_hand(M, y, top, half_w, sleeve_to=(-0.2, -0.26, -0.5)):
    """The left hand under and round the fore end, seen from above left: the palm on the left side, the fingers come under and
    up the far side, the thumb lies along the top edge. y: along the gun; top: the height of the fore end's top; half_w: half its width."""
    o = []
    x = -half_w - 0.012
    z = top - 0.055
    o.append(bevel_box('palm', (0.03, 0.095, 0.085), (x, y, z), 0.013, 3, M['glove'], (0, 0.15, 0)))
    o.append(bevel_box('heel', (0.028, 0.06, 0.04), (x - 0.006, y - 0.052, z - 0.03), 0.012, 3, M['glove'], (0.3, 0.15, 0)))
    for i in range(4):
        fy = y + 0.036 - i * 0.024
        fz = z - 0.046
        o += finger(M, (x + 0.006, fy, fz), (0.9, 0.03, -0.42), 0, n=1, seg=half_w * 2 + 0.008)  # under the fore end
        o.append(sphere('kn', 0.0125, (x + 0.002, fy, fz + 0.004), mat=M['knuckle'], seg=10, rings=6))
    # the thumb on the top edge, pointing up the gun
    o += finger(M, (x + 0.002, y - 0.028, top - 0.025), (0.24, 0.96, 0.22), 0.0, r=0.0135, n=3, seg=0.033)
    # the wrist and the sleeve back out of the view
    wrist = Vector((x - 0.012, y - 0.08, z - 0.04))
    end = Vector((wrist.x + sleeve_to[0], wrist.y + sleeve_to[1], wrist.z + sleeve_to[2]))
    o.append(tube('wrist', 0.033, (x - 0.004, y - 0.05, z - 0.022), wrist, M['glove'], 14))
    o.append(tube('cuff', 0.041, wrist, wrist + (end - wrist).normalized() * 0.035, M['knuckle'], 16))
    o.append(tube('sleeve', 0.044, wrist + (end - wrist).normalized() * 0.03, end, M['sleeve'], 16, 0.058))
    for k in (0.16, 0.26, 0.4):  # folds in the cloth
        c = wrist + (end - wrist) * k
        o.append(tube('fold', 0.0475 + k * 0.014, c, c + (end - wrist).normalized() * 0.018, M['sleeve'], 16))
    return o

def right_hand(M, y, z):
    """The firing hand on the pistol grip, the sleeve going out of the bottom of the view."""
    o = []
    o.append(bevel_box('rpalm', (0.08, 0.075, 0.1), (0, y - 0.02, z), 0.022, 3, M['glove'], (0.25, 0, 0)))
    for i in range(3):
        o += finger(M, (-0.042, y + 0.01, z + 0.025 - i * 0.026), (0.2, 0.98, 0), 0.8, r=0.0105, n=2, seg=0.03)
    o += finger(M, (-0.04, y + 0.005, z + 0.058), (0.1, 0.99, 0.1), 0.35, r=0.0105, n=2, seg=0.034)  # along the receiver, off the trigger
    o.append(tube('rsleeve', 0.05, (0.02, y - 0.07, z - 0.05), (0.15, y - 0.4, z - 0.3), M['sleeve'], 16, 0.06))
    return o

def rail_top(M, y0, y1, z, w=0.034):
    """A slotted accessory rail along the top."""
    o = [bevel_box('railbase', (w, y1 - y0, 0.008), (0, (y0 + y1) / 2, z), 0.002, 1, M['dark'])]
    n = int((y1 - y0) / 0.024)
    for i in range(n):
        o.append(bevel_box('railt', (w + 0.006, 0.012, 0.009), (0, y0 + 0.012 + i * 0.024, z + 0.0075), 0.0015, 1, M['dark']))
    return o

def screws(M, pts, r=0.0055):
    o = []
    for p in pts:
        o.append(cyl('screw', r, 0.004, p, (0, H, 0), 8, M['steel']))
        o.append(bevel_box('slot', (0.0015, r * 1.6, 0.0012), (p[0] - 0.0022, p[1], p[2]), 0.0002, 1, M['knuckle']))
    return o

def seam(M, size, loc):
    """A dark panel line (a thin strip proud of the surface: reads as a groove from here)."""
    return bevel_box('seam', size, loc, 0.0004, 1, M['knuckle'])

def pulse(M):
    o = []
    # receiver: upper and lower halves with a parting line and a step
    o.append(bevel_box('upper', (0.072, 0.4, 0.06), (0, 0.06, 0.06), 0.012, 3, M['body']))
    o.append(bevel_box('lower', (0.066, 0.3, 0.06), (0, 0.0, 0.0), 0.012, 3, M['dark']))
    o.append(seam(M, (0.0735, 0.4, 0.0028), (0, 0.06, 0.03)))
    # the ejection port on the left, with the bolt showing
    o.append(bevel_box('port', (0.004, 0.095, 0.032), (-0.0365, 0.1, 0.066), 0.001, 1, M['knuckle']))
    o.append(bevel_box('bolt', (0.005, 0.07, 0.016), (-0.0375, 0.1, 0.066), 0.001, 1, M['steel']))
    o.append(tube_y('chargeh', 0.008, -0.14, -0.08, (-0.024, 0.097), M['steel'], 8))
    o += screws(M, [(-0.0375, -0.05, 0.08), (-0.0375, 0.22, 0.08), (-0.0345, -0.1, 0.0), (-0.0345, 0.1, 0.0)])
    # fore end: a heat shield with vents along its top and left, the hand round it
    o.append(bevel_box('handguard', (0.08, 0.34, 0.075), (0, 0.42, 0.04), 0.02, 3, M['dark']))
    for i in range(6):
        o.append(bevel_box('vent', (0.0035, 0.032, 0.022), (-0.0405, 0.32 + i * 0.042, 0.052), 0.001, 1, M['steel']))
    o += rail_top(M, -0.1, 0.56, 0.095)
    # barrel, gas tube, a ported muzzle brake
    o.append(tube_y('barrel', 0.015, 0.56, 0.76, (0, 0.05), M['steel'], 14))
    o.append(tube_y('gas', 0.008, 0.56, 0.68, (0, 0.078), M['dark'], 10))
    o.append(tube_y('muzzle', 0.024, 0.74, 0.84, (0, 0.05), M['dark'], 14))
    for i in range(3):
        o.append(bevel_box('mport', (0.052, 0.009, 0.007), (0, 0.76 + i * 0.024, 0.05), 0.001, 1, M['knuckle']))
    # the energy cell: a glowing strip under a guard, the optic with its lens
    o.append(bevel_box('cellb', (0.032, 0.2, 0.012), (0, 0.17, 0.112), 0.003, 1, M['dark']))
    o.append(bevel_box('cell', (0.016, 0.17, 0.008), (0, 0.17, 0.121), 0.002, 1, M['glow_o']))
    o.append(bevel_box('sight', (0.034, 0.075, 0.048), (0, -0.03, 0.135), 0.008, 2, M['dark']))
    o.append(bevel_box('dot', (0.02, 0.004, 0.02), (0, 0.009, 0.139), 0.002, 1, M['lens']))
    o.append(tube('sightb', 0.006, (-0.018, -0.03, 0.105), (-0.018, -0.03, 0.125), M['steel'], 8))
    # magazine, trigger guard, grip, stock
    o.append(bevel_box('magwell', (0.06, 0.09, 0.05), (0, 0.11, -0.04), 0.008, 2, M['dark']))
    o.append(bevel_box('mag', (0.046, 0.075, 0.17), (0, 0.125, -0.13), 0.01, 2, M['body'], (-0.12, 0, 0)))
    o.append(bevel_box('guard', (0.008, 0.075, 0.006), (0, -0.02, -0.055), 0.001, 1, M['dark']))
    o.append(bevel_box('stock', (0.052, 0.18, 0.09), (0, -0.24, 0.05), 0.016, 3, M['body']))
    o.append(bevel_box('butt', (0.056, 0.024, 0.14), (0, -0.335, 0.03), 0.01, 2, M['rubber']))
    o.append(bevel_box('grip', (0.046, 0.065, 0.13), (0, -0.075, -0.09), 0.014, 3, M['rubber'], (0.25, 0, 0)))
    o += left_hand(M, 0.4, 0.078, 0.04)
    o += right_hand(M, -0.07, -0.07)
    return o, 0.84

def scatter(M):
    o = []
    o.append(bevel_box('receiver', (0.082, 0.3, 0.1), (0, 0.0, 0.04), 0.016, 3, M['body']))
    o.append(seam(M, (0.0835, 0.2, 0.0028), (0, 0.02, 0.05)))
    # the loading port and a shell rack on the left side (four brass heads)
    o.append(bevel_box('port', (0.004, 0.11, 0.04), (-0.0415, 0.05, 0.03), 0.001, 1, M['knuckle']))
    o.append(bevel_box('rack', (0.012, 0.13, 0.03), (-0.047, -0.05, 0.075), 0.003, 1, M['dark']))
    for i in range(4):
        o.append(tube('shell', 0.0095, (-0.056, -0.098 + i * 0.032, 0.075), (-0.051, -0.098 + i * 0.032, 0.075), M['brass'], 10))
    o += screws(M, [(-0.0415, 0.13, 0.075), (-0.0415, -0.13, 0.015)])
    # two barrels, a ventilated rib with posts, bands, a bead
    for sx in (-0.024, 0.024):
        o.append(tube_y('barrel', 0.024, 0.14, 0.8, (sx, 0.055), M['steel'], 16))
        o.append(tube_y('muzzle', 0.029, 0.76, 0.82, (sx, 0.055), M['dark'], 16))
    o.append(bevel_box('rib', (0.02, 0.64, 0.006), (0, 0.48, 0.094), 0.002, 1, M['dark']))
    for i in range(9):
        o.append(bevel_box('post', (0.006, 0.01, 0.012), (0, 0.2 + i * 0.072, 0.086), 0.001, 1, M['steel']))
    o.append(bevel_box('bead', (0.008, 0.008, 0.016), (0, 0.8, 0.105), 0.002, 1, M['glow_o']))
    o.append(tube_y('band', 0.05, 0.64, 0.66, (0, 0.055), M['dark'], 18))
    # the pump, ribbed
    o.append(bevel_box('forend', (0.09, 0.24, 0.07), (0, 0.36, 0.0), 0.022, 3, M['wood']))
    for i in range(7):
        o.append(bevel_box('groove', (0.0925, 0.006, 0.0085), (0, 0.27 + i * 0.03, -0.024), 0.001, 1, M['knuckle']))
    # stock, grip, plate
    o.append(bevel_box('stock', (0.062, 0.3, 0.11), (0, -0.28, 0.0), 0.022, 3, M['wood'], (-0.12, 0, 0)))
    o.append(bevel_box('plate', (0.066, 0.026, 0.135), (0, -0.436, -0.03), 0.01, 2, M['rubber']))
    o.append(bevel_box('grip', (0.05, 0.065, 0.13), (0, -0.12, -0.08), 0.016, 3, M['wood'], (0.25, 0, 0)))
    o.append(bevel_box('guard', (0.008, 0.075, 0.006), (0, -0.04, -0.03), 0.001, 1, M['dark']))
    o += left_hand(M, 0.36, 0.035, 0.045)
    o += right_hand(M, -0.11, -0.06)
    return o, 0.82

def rocket(M):
    o = []
    o.append(tube_y('tube', 0.078, -0.25, 0.62, (0, 0.0), M['body'], 32))
    o.append(tube_y('mouth', 0.095, 0.58, 0.7, (0, 0.0), M['dark'], 32, 0.092))
    o.append(tube_y('inner', 0.066, 0.69, 0.705, (0, 0.0), M['glow_o'], 24))
    o.append(tube_y('rear', 0.1, -0.3, -0.18, (0, 0.0), M['dark'], 28, 0.084))
    for y in (-0.02, 0.28):
        o.append(tube_y('band', 0.084, y, y + 0.03, (0, 0.0), M['orange'], 32))
    # the top: a rail, a raised sight with a glowing ring, a warning stripe, cables down the left
    o += rail_top(M, -0.12, 0.34, 0.083)
    o.append(bevel_box('sightp', (0.03, 0.09, 0.05), (0, 0.04, 0.115), 0.008, 2, M['dark']))
    o.append(tube_y('sightring', 0.02, 0.09, 0.1, (0, 0.135), M['glow_c'], 16))
    o.append(bevel_box('stripe', (0.05, 0.05, 0.006), (0, 0.46, 0.079), 0.002, 1, M['orange']))
    for i, z in enumerate((0.02, -0.012)):
        o.append(tube('cable', 0.0055, (-0.078, -0.14, z), (-0.078, 0.38, z), M['rubber'], 8))
    for y in (-0.08, 0.14, 0.34):
        o.append(bevel_box('clamp', (0.008, 0.016, 0.05), (-0.08, y, 0.004), 0.002, 1, M['steel']))
    o += screws(M, [(-0.0795, 0.56, 0.02), (-0.0795, 0.64, 0.02)], 0.006)
    o.append(bevel_box('fgrip', (0.05, 0.06, 0.1), (0, 0.34, -0.12), 0.016, 3, M['rubber']))
    o.append(bevel_box('grip', (0.05, 0.065, 0.13), (0, 0.04, -0.15), 0.016, 3, M['rubber'], (0.2, 0, 0)))
    o += left_hand(M, 0.32, 0.0, 0.08)
    o += right_hand(M, 0.04, -0.12)
    return o, 0.71

def rail(M):
    o = []
    o.append(bevel_box('spine', (0.058, 0.8, 0.06), (0, 0.22, 0.03), 0.014, 3, M['dark']))
    o.append(bevel_box('chan', (0.016, 0.62, 0.012), (0, 0.32, 0.065), 0.003, 2, M['glow_v']))
    for i in range(5):
        y = 0.0 + i * 0.12
        o.append(tube_y('coil', 0.066 - i * 0.004, y, y + 0.05, (0, 0.03), M['steel'], 28))
        o.append(tube_y('coilg', 0.058 - i * 0.004, y + 0.012, y + 0.04, (0, 0.03), M['glow_v'], 24))
        for k in range(3):
            o.append(tube_y('wind', 0.0675 - i * 0.004, y + 0.006 + k * 0.016, y + 0.0105 + k * 0.016, (0, 0.03), M['brass'], 28))
    o.append(tube('feed', 0.0065, (-0.04, -0.16, 0.06), (-0.052, 0.5, 0.05), M['rubber'], 8))
    o.append(tube('feed2', 0.0045, (-0.03, -0.16, 0.07), (-0.044, 0.5, 0.066), M['orange'], 8))
    o.append(bevel_box('prong_l', (0.013, 0.28, 0.05), (-0.04, 0.68, 0.03), 0.003, 2, M['steel']))
    o.append(bevel_box('prong_r', (0.013, 0.28, 0.05), (0.04, 0.68, 0.03), 0.003, 2, M['steel']))
    o.append(bevel_box('tip', (0.02, 0.02, 0.05), (0, 0.8, 0.03), 0.004, 1, M['glow_v']))
    o.append(bevel_box('body', (0.085, 0.24, 0.11), (0, -0.2, 0.01), 0.022, 3, M['body']))
    o.append(seam(M, (0.0865, 0.24, 0.0028), (0, -0.2, 0.025)))
    o += screws(M, [(-0.0435, -0.28, 0.05), (-0.0435, -0.12, 0.05), (-0.0435, -0.28, -0.03), (-0.0435, -0.12, -0.03)])
    o.append(bevel_box('sight', (0.03, 0.12, 0.05), (0, -0.14, 0.09), 0.01, 2, M['dark']))
    o.append(bevel_box('dot', (0.018, 0.004, 0.018), (0, -0.078, 0.093), 0.002, 1, M['glow_c']))
    o.append(bevel_box('grip', (0.046, 0.065, 0.13), (0, -0.11, -0.09), 0.014, 3, M['rubber'], (0.25, 0, 0)))
    o.append(bevel_box('fguard', (0.075, 0.2, 0.05), (0, 0.33, -0.02), 0.014, 2, M['body']))
    o += left_hand(M, 0.33, 0.0, 0.0375)
    o += right_hand(M, -0.1, -0.08)
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
        try:
            ob.data.set_sharp_from_angle(angle=0.7)
        except Exception:
            pass
        box_uv(ob, 0.34)
        print('MUZZLE', name, muzzle, 'tris', sum(len(p.vertices) - 2 for p in ob.data.polygons))
        if sheet:
            render_sheet(sheet.replace('.png', '_%s.png' % name), [ob], [(math.pi / 2, 0.2), (0.6, 0.5), (-0.5, 0.55)], size=1.5, center=(0, 0.2, 0))
        export_glb(os.path.join(out, 'vm_%s.glb' % name), anims=False)

main()
