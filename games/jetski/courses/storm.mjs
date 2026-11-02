// Gale Bank: a squall on the open North Sea. No shore: a bank of skerries, a drilling rig and wrecks, slate water under black cloud, driving rain and lightning.
// The hook: the swell. It starts big and keeps building lap on lap until the last lap is the biggest water in the game, with a beacon's sweep to steer home by.
export default ({ clamp, lerp, smooth, hash, fbm, ridged, trackOf, strongest }) => {
const P = [[-560, -140], [-360, -420], [-40, -520], [300, -430], [560, -200], [640, 90], [470, 340], [160, 300], [-40, 480], [-340, 500], [-610, 300]];
const { trackDist, pointAt, LEN } = trackOf(P);
const SKERRIES = [[60, -60, 70, 18], [-150, 90, 46, 12], [240, 40, 40, 14], [-300, -130, 54, 10], [760, -320, 90, 20], [-760, -420, 80, 22], [-140, 720, 90, 16], [420, 660, 60, 14]];
function height(x, z) {
  let y = -38 + 8 * fbm(x * 0.004, z * 0.004, 3, 501);
  for (const [sx, sz, r, h] of SKERRIES) {
    const d = Math.hypot(x - sx, z - sz) * (0.72 + 0.6 * fbm(x * 0.012 + sx, z * 0.012, 3, 502));
    const top = h * (0.55 + 0.85 * ridged(x * 0.026, z * 0.026, 4, 503));
    y = Math.max(y, lerp(-38, top, 1 - smooth(r * 0.78, r * 1.02, d))); // (steep sides: a rock face, not a mound)
  }
  const c = trackDist(x, z);
  if (y > -7) y = lerp(-7, y, smooth(52, 90, c));
  return y;
}
return {
  id: 'storm',
  name: 'Gale Bank',
  track: P,
  height,
  // layers 0 wet black rock, 1 weed, 2 storm-washed pale rock, 3 guano
  cliffs: { seed: 811, spurs: [[46, 9], [18, 3.5]], talus: 2.8, bench: 3.5, beds: [2, 9], dip: [0.05, 0.03], fold: 8, droplets: 120000, capacity: 1.5, cut: 3, keep: 2.5 },
  paint({ x, z, y, slope, flow, hard, wall }) {
    const n = fbm(x * 0.05, z * 0.05, 3, 511);
    if (y < 2) return [0, 1, 255 * (1 - smooth(-1, 2, y + (n - 0.5) * 2)) * (1 - smooth(0.1, 0.45, wall))]; // (weed fades out on a wall's foot: its edge there comes out as teeth)
    // dark and grey rock in masses, white guano on the tops, wet where water runs. Not by height: paint is held every 2 m across the map, and a change with height on a wall comes out as a row of teeth
    if (y > 2 && (slope > 0.6 || wall > 0.2)) return strongest([[0, 0.7 - n * 0.5], [2, 0.35 + (hard - 0.5) + n * 0.4], [3, smooth(0.55, 0.85, n) * (1 - smooth(0.5, 1.2, slope)) * (1 - flow)], [4, flow * 0.7]]);
    return [0, 2, 255 * smooth(0.3, 0.7, n) * (1 - smooth(0.7, 1.2, slope))];
  },
  // open ocean, a lee behind the skerries
  mask({ y }) { return Math.max(0.08, smooth(-2, -18, y)); },
  maps: {},
  scatter: [],
  placed_models: ['rig', 'wreck', 'tower'],
  placed({ ground, pointAt, LEN }) {
    const out = [];
    const put = (model, s, off, solid, floats, turn = 0) => { const p = pointAt(s); out.push({ model, x: p.x - p.dz * off, z: p.z + p.dx * off, yaw: Math.atan2(p.dx, p.dz) + turn, scale: 1, solid, floats }); };
    put('rig', LEN * 0.32, 92, [31, 31], true);
    put('wreck', LEN * 0.14, -74, [33, 7], true, 0.5);
    put('wreck', LEN * 0.6, 70, [33, 7], true, -0.7);
    put('tower', LEN * 0.02, 44, [4, 4], true);
    put('tower', LEN * 0.47, -46, [4, 4], true);
    put('tower', LEN * 0.8, 46, [4, 4], true);
    return out;
  },
  keep_clear: () => false,
  grades: [
    ['blackrock', 'rock_face_03', [0.62, 0.64, 0.7], 0.07, 0.2],
    ['weed', 'grass', [0.22, 0.2, 0.09], 0.15],
    ['palerock', 'rock_face', [1.25, 1.3, 1.38], 0.05, 0.25],
    ['pale2', 'marble_cliff_03', [1.5, 1.5, 1.42], 0.06, 0.2],
    ['stormwet', 'rock_face_03', [0.3, 0.32, 0.36], 0.07, 0.2],
  ],
  ramp_count: 8,
  ramp_big: 2,
  ramp_skip: [2929],
  ramp_top: 6.5,
  buoys: { first: 90, step: 104, wobble: 14, offset: 9 },
  // a long heavy swell and a confused sea on top of it
  waves: [[315, 78, 0, 3.0, 0.72], [290, 51, 0, 1.6, 0.7], [350, 19, 0.2, 0.4, 0.72], [260, 8, 0.1, 0.12, 0.55]],
  swell: [0.45, 1.05],
  tide: [0, 0],
  sky: 'res://textures/sky-storm.png',
  sun_disc: 0,
  rain: 1,
  side_detile: 0.01,
  props: [
    ['Sky', 'ground_color', 'color(0.06, 0.09, 0.11, 1)'],
    ['Sky', 'time_of_day', '15.2'], ['Sky', 'sun_energy', '0.7'], ['Sky', 'sky_energy', '1.5'], ['Sky', 'north', '300'],
    ['Fill', 'energy', '1.5'], ['Fill', 'rotation', 'vec3(-0.9, 2.6, 0)'], ['Fill', 'color', 'color(0.6, 0.7, 0.82, 1)'],
    ['Post', 'exposure', '1.05'], ['Post', 'bloom_intensity', '0.12'],
    ['Water', 'shallow_color', 'color(0.1, 0.17, 0.18, 1)'], ['Water', 'deep_color', 'color(0.022, 0.05, 0.065, 1)'],
    ['Water', 'color', 'color(0.8, 0.86, 0.9, 1)'], ['Water', 'clarity', '3'], ['Water', 'murk', '1.4'],
    ['Water', 'distortion', '0.2'], ['Water', 'normal_scale', '0.5'], ['Water', 'detail', '1'],
    ['Water', 'flow_speed', '0.07'], ['Water', 'flow_direction', 'vec2(0.7, 0.7)'],
    ['Water', 'crest_foam', '1'], ['Water', 'crest_light', '0.55'], ['Water', 'scatter', '0.3'], ['Water', 'glint', '0.25'], ['Water', 'shore_foam', '2.5'],
  ],
  hook: { kind: 4, x: 60, z: -60, dirx: 1, dirz: 0, half_w: 0, half_len: 0, lintel: 0, s_in: 0, s_out: 0, current: 0, model: 'beacon' },
};
};
