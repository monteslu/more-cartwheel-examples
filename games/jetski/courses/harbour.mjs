// Iron Quay: a working port at night. Black water, quays of concrete and steel, cranes and warehouses with lit windows, moored ships to weave between. Man-made
// from end to end. The hook: a tanker under way crosses the course, and its wake is a moving line of big waves to jump or be thrown by.
export default ({ clamp, lerp, smooth, hash, fbm, trackOf }) => {
const P = [[-520, 300], [-300, 380], [-60, 330], [150, 400], [380, 330], [560, 180], [600, -40], [470, -200], [250, -160], [90, -260], [-110, -380], [-340, -340], [-530, -200], [-620, 40]];
const { trackDist, pointAt, LEN } = trackOf(P);
// the land: everything flat at the quay's top; the basin's edge a straight-sided quay wall, in blocks
const quay = (x, z) => { const q = 40; return [Math.round(x / q) * q, Math.round(z / q) * q]; };
// the tanker's way: straight across the course from one dock to the other
const A = [150, 60], B = [-40, 690], ALEN = Math.hypot(B[0] - A[0], B[1] - A[1]), AU = [(B[0] - A[0]) / ALEN, (B[1] - A[1]) / ALEN];
const onWay = (x, z) => { const t = clamp(((x - A[0]) * AU[0] + (z - A[1]) * AU[1]) / ALEN) * ALEN; return Math.hypot(x - A[0] - AU[0] * t, z - A[1] - AU[1] * t); };
function height(x, z) {
  const d = trackDist(x, z);
  if (onWay(x, z) < 26) return -13;
  // a basin with straight edges: the distance is stepped in blocks of quay, so the shore is corners and piers, not a curve
  const [qx, qz] = quay(x, z), dq = trackDist(qx, qz);
  const open = dq < 96 || d < 62;
  return open ? -13 : 4.4;
}
return {
  id: 'harbour',
  name: 'Iron Quay',
  track: P,
  height,
  // layers 0 concrete, 1 wet quay foot, 2 dark steel-grey stone, 3 tarmac
  paint({ x, z, y }) {
    if (y < 1) return [1, 2, 200];
    const n = fbm(x * 0.02, z * 0.02, 3, 411);
    return [0, 3, 255 * smooth(0.4, 0.6, n)];
  },
  placed_models: ['ship', 'warehouse', 'block', 'block2', 'block3', 'containers', 'crane', 'lamp'],
  keep_clear: (x, z) => onWay(x, z) < 34,
  // what stands on a grid along the quays. Tall things in rows, not scattered; ships at the wall, solid
  placed({ ground, pointAt, LEN }) {
    const out = [];
    // every point under a footprint is on the quay's top: the basin's edge is corners and piers, and a thing placed by its centre hung over them
    const onQuay = (x, z, yaw, x0, x1, z0, z1) => { const c = Math.cos(yaw), n = Math.sin(yaw); for (let a = x0; a <= x1; a += 1) for (let b = z0; b <= z1; b += 1) if (ground(x + a * c + b * n, z - a * n + b * c) < 4.3) return false; return true; };
    for (let s = 40; s < LEN; s += 118) {
      const p = pointAt(s), side = Math.floor(s / 118) % 2 ? 1 : -1, off = 74;
      const x = p.x - p.dz * side * off, z = p.z + p.dx * side * off, yaw = Math.atan2(p.dx, p.dz);
      if (onWay(x, z) < 70 || ground(x, z) > -4 || ground(x + p.dx * 40, z + p.dz * 40) > -4 || ground(x - p.dx * 40, z - p.dz * 40) > -4) continue;
      out.push({ model: 'ship', x, z, yaw, scale: 1, solid: [40, 8], floats: true });
    }
    for (let z = -860; z <= 860; z += 44) for (let x = -860; x <= 860; x += 44) {
      const d = trackDist(x, z);
      if (onWay(x, z) < 50 || ground(x, z) < 3) continue;
      let lo = 1e9; for (const [a, b] of [[-24, -14], [24, -14], [-24, 14], [24, 14]]) lo = Math.min(lo, ground(x + a, z + b));
      const r = hash(Math.round(x / 44), Math.round(z / 44), 431);
      if (d > 132 && lo > 3 && r < 0.62) out.push({ model: r < 0.4 ? 'warehouse' : ['block', 'block2', 'block3'][Math.floor(hash(x, z, 433) * 3)], x, z, yaw: Math.PI / 2 * Math.floor(hash(x, z, 434) * 4), scale: 0.85 + 0.3 * hash(x, z, 432) });
      else if (d > 96 && d <= 132 && lo > 3 && r < 0.7) { const yaw = Math.PI / 2 * Math.floor(r * 8); if (onQuay(x, z, yaw, -14, 14, -19, 19)) out.push({ model: 'containers', x, z, yaw, scale: 1 }); }
    }
    // cranes and lamps along the quay's edge
    for (let s = 20; s < LEN; s += 34) for (const side of [-1, 1]) {
      const p = pointAt(s);
      for (let off = 58; off < 130; off += 6) {
        const x = p.x - p.dz * side * off, z = p.z + p.dx * side * off;
        if (ground(x, z) > 3 && ground(x + p.dz * side * 6, z - p.dx * side * 6) < 0) {
          // back from the edge until the whole foot stands on the quay; a pier too narrow for it gets none
          const crane = Math.floor(s / 34) % 5 === 0, yaw = Math.atan2(p.dx, p.dz) + (side > 0 ? Math.PI : 0);
          if (onWay(x, z) > 40) for (let back = 9; back <= 36; back += 3) {
            const px = x - p.dz * side * back, pz = z + p.dx * side * back;
            if ((crane ? onQuay(px, pz, yaw, -11, 11, -11, 11) : onQuay(px, pz, yaw, -1, 1, -1, 6))) { out.push({ model: crane ? 'crane' : 'lamp', x: px, z: pz, yaw, scale: 1 }); break; }
          }
          break;
        }
      }
    }
    return out;
  },
  mask({ y }) { return Math.max(0.06, 0.34 * smooth(-1, -6, y)); },
  maps: {},
  scatter: [],
  grades: [
    ['concrete', 'coast', [0.5, 0.5, 0.52], 0.06],
    ['quayfoot', 'rock', [0.12, 0.13, 0.13], 0.12],
    ['stone', 'rock', [0.26, 0.27, 0.3], 0.09],
    ['tarmac', 'coast', [0.14, 0.14, 0.16], 0.1],
  ],
  ramp_count: 10,
  ramp_top: 5,
  buoys: { first: 80, step: 90, wobble: 12, offset: 8.5 },
  // a harbour's slop: short, steep and small
  waves: [[30, 24, 0.14, 0.2, 0.6], [120, 15, 0.08, 0.14, 0.6], [75, 7, 0.05, 0.06, 0.5], [200, 3.8, 0.02, 0.02, 0.4]],
  swell: [0.8, 1.0],
  tide: [0, 0],
  sky: 'res://textures/sky-harbour.png',
  sun_disc: 0.05,
  side_detile: 1,
  props: [
    ['Sky', 'ground_color', 'color(0.02, 0.025, 0.04, 1)'],
    ['Sky', 'time_of_day', '22.5'], ['Sky', 'sun_energy', '0.4'], ['Sky', 'sky_energy', '0.5'], ['Sky', 'north', '30'],
    ['Fill', 'energy', '2.2'], ['Fill', 'rotation', 'vec3(-0.7, 0.9, 0)'], ['Fill', 'color', 'color(0.86, 0.68, 0.46, 1)'],
    ['Post', 'exposure', '1.25'], ['Post', 'bloom_intensity', '0.3'],
    ['Water', 'shallow_color', 'color(0.03, 0.06, 0.08, 1)'], ['Water', 'deep_color', 'color(0.006, 0.012, 0.02, 1)'],
    ['Water', 'color', 'color(0.95, 0.95, 1, 1)'], ['Water', 'clarity', '2'], ['Water', 'murk', '1'],
    ['Water', 'distortion', '0.1'], ['Water', 'normal_scale', '0.3'], ['Water', 'detail', '0.8'],
    ['Water', 'flow_speed', '0.03'], ['Water', 'flow_direction', 'vec2(1, 0)'],
    ['Water', 'crest_foam', '0.25'], ['Water', 'crest_light', '0'], ['Water', 'scatter', '0'], ['Water', 'glint', '0.5'], ['Water', 'shore_foam', '0.6'],
  ],
  hook: { kind: 3, x: A[0], z: A[1], dirx: AU[0], dirz: AU[1], half_w: 11, half_len: ALEN, lintel: 62, s_in: 7, s_out: 0, current: 0, model: 'tanker' },
};
};
