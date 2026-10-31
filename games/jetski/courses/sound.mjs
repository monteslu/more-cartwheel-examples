// Cold Sound: a grey morning in a granite fjord, open to the ocean at its west end and sheltered at its head. A tide that drains
// through the race opens a drowned arch through the headland: the short way home, once there is room under it.
export default ({ clamp, lerp, smooth, hash, fbm, ridged, trackOf }) => {
const P = [[-600, 200], [-420, 300], [-200, 340], [20, 300], [180, 200], [360, 190], [520, 120], [560, -30], [470, -170], [320, -200], [190, -160], [30, -260], [-200, -340], [-460, -330], [-680, -200], [-760, 20]];
const { trackDist, pointAt, LEN } = trackOf(P);
// the cut through the headland's neck: from the south leg to the north leg
const A = [196, 186], B = [194, -158];
const segDist = (x, z) => {
  const dx = B[0] - A[0], dz = B[1] - A[1], t = clamp(((x - A[0]) * dx + (z - A[1]) * dz) / (dx * dx + dz * dz));
  return Math.hypot(x - A[0] - dx * t, z - A[1] - dz * t);
};
const near = (X, Z) => { let b = 0, bd = 1e9; for (let s = 0; s < LEN; s += 2) { const p = pointAt(s), d = Math.hypot(p.x - X, p.z - Z); if (d < bd) bd = d, b = s; } return b; };
const ocean = (x) => 1 - smooth(-640, -470, x);
function height(x, z) {
  const d = trackDist(x, z) + (fbm(x * 0.012, z * 0.012, 3, 201) - 0.5) * 50;
  const massif = 34 + 130 * Math.pow(ridged(x * 0.0035 + 3, z * 0.0035, 5, 203), 1.6);
  let y = lerp(-34, massif, smooth(52, 150, d));
  // the shore shelves at the foot of the granite, so a low tide bares ledges
  y += smooth(40, 75, d) * (1 - smooth(75, 120, d)) * 3.5 * (fbm(x * 0.03, z * 0.03, 3, 204) - 0.3);
  if (y > 2) {
    const k = smooth(2, 26, y);
    y += k * ((ridged(x * 0.022, z * 0.022, 5, 205) - 0.55) * 26 + (fbm(x * 0.09, z * 0.09, 3, 206) - 0.5) * 7);
  }
  // ledges at the waterline: the slope all but stops between the tides, so the falling tide bares rock shelves and boulders have
  // somewhere to sit
  if (y > -8 && y < 8) y -= 2.5 * Math.sin(Math.PI * y / 8);
  // to the west the land falls away to the ocean
  y = lerp(y, -40, ocean(x + (fbm(x * 0.006, z * 0.006, 2, 207) - 0.5) * 120));
  // the cut
  const c = segDist(x, z);
  y = Math.min(y, lerp(-6.5, Math.max(y, 12), smooth(11, 24, c)));
  return y;
}
return {
  id: 'sound',
  name: 'Cold Sound',
  track: P,
  height,
  // layers 0 dark shingle, 1 kelp-stained tide rock, 2 granite, 3 moss; snow is granite graded pale up high (blend of 0 and 2 is enough down low)
  paint({ x, z, y, slope }) {
    const n = fbm(x * 0.03, z * 0.03, 3, 211), steep = smooth(0.6, 1.1, slope + (n - 0.5) * 0.5);
    // the tide's stain on the granite: one base layer across the band, or the terrain's grid shows as teeth along the waterline
    if (y < 6) return [2, 1, 255 * (1 - smooth(1.5, 6, y + (n - 0.5) * 2))];
    const moss = (1 - steep) * smooth(5, 14, y + n * 8) * (1 - smooth(60, 110, y + n * 30));
    return [2, 3, 255 * moss];
  },
  mask({ x, z, y }) {
    // the swell comes in from the ocean and dies up the fjord, and on any shoal
    const open = 1 - smooth(-560, 330, x);
    return Math.max(0.05, Math.max(open, 0.16) * smooth(-0.8, -7, y));
  },
  maps: {
    pines({ x, z, y, slope }) { return smooth(5, 12, y) * (1 - smooth(55, 95, y)) * (1 - smooth(0.7, 1.05, slope)) * smooth(0.38, 0.55, fbm(x * 0.018, z * 0.018, 3, 221)); },
    boulders({ x, z, y, slope }) { return smooth(-2.5, -0.5, y) * (1 - smooth(3, 7, y)) * smooth(0.42, 0.6, fbm(x * 0.05 + 3, z * 0.05, 3, 222)) * smooth(30, 42, trackDist(x, z)) * smooth(8, 16, segDist(x, z)); },
  },
  lying: ['boulders'],
  // the fjord's shore is steep everywhere: deep-set boulders hold on ground the atoll's could not
  lying_drop: [8, 12],
  scatter: [
    ['pine', 'pines', 0.03, 0.7, 1.5, 800, 177, false, 0.1],
    ['rock0', 'boulders', 0.25, 0.7, 2.6, 700, 179, true, 0.9],
    ['rock1', 'boulders', 0.2, 0.5, 2.0, 700, 180, true, 0.9],
    ['rock2', 'boulders', 0.15, 0.8, 3.4, 700, 181, true, 0.9],
  ],
  grades: [
    ['shingle', 'coast', [0.42, 0.44, 0.48], 0.2],
    ['tiderock', 'rock', [0.24, 0.28, 0.26], 0.1],
    ['granite', 'rock', [0.7, 0.86, 1.08], 0.06],
    ['moss', 'grass', [0.42, 0.56, 0.34], 0.09],
  ],
  ramp_count: 10,
  ramp_top: 6.5,
  buoys: { first: 80, step: 92, wobble: 12, offset: 8.5 },
  // the long swell of an open ocean, on a course where it matters most at the west end
  waves: [[12, 66, 0, 1.9, 0.72], [340, 43, 0, 1.15, 0.7], [40, 15, 0.2, 0.26, 0.7], [300, 7, 0.07, 0.07, 0.5]],
  swell: [0.7, 1.0],
  tide: [2.6, -1.6],
  sky: 'res://textures/sky-sound.png',
  sun_disc: 0.12,
  props: [
    // (what the water mirrors past the horizon on a wave's back: the sky's default sandy ground lay on this sea in tan bands)
    ['Sky', 'ground_color', 'color(0.16, 0.26, 0.34, 1)'],
    ['Sky', 'time_of_day', '10.4'], ['Sky', 'sun_energy', '2.0'], ['Sky', 'sky_energy', '2.6'], ['Sky', 'north', '40'],
    ['Fill', 'energy', '2.4'], ['Fill', 'rotation', 'vec3(-0.55, -2.2, 0)'], ['Fill', 'color', 'color(0.72, 0.78, 0.92, 1)'],
    ['Post', 'exposure', '1.0'], ['Post', 'bloom_intensity', '0.05'],
    ['Water', 'shallow_color', 'color(0.1, 0.34, 0.36, 1)'], ['Water', 'deep_color', 'color(0.03, 0.08, 0.13, 1)'],
    ['Water', 'color', 'color(0.86, 0.92, 0.95, 1)'], ['Water', 'clarity', '4'], ['Water', 'murk', '1.2'],
    // (ripples strong enough to break up a mirrored skyline: at less, its edge lies on the water as a hard patch)
    ['Water', 'distortion', '0.16'], ['Water', 'normal_scale', '0.3'], ['Water', 'detail', '0.9'], ['Water', 'crest_foam', '1'], ['Water', 'crest_light', '0.3'], ['Water', 'scatter', '0.5'], ['Water', 'glint', '0.5'], ['Water', 'shore_foam', '2'],
  ],
  // an arch across the cut: nothing passes while the water stands too high under it
  hook: { kind: 1, x: (A[0] + B[0]) / 2, z: (A[1] + B[1]) / 2, dirx: (B[0] - A[0]) / Math.hypot(B[0] - A[0], B[1] - A[1]), dirz: (B[1] - A[1]) / Math.hypot(B[0] - A[0], B[1] - A[1]), half_w: 11, half_len: Math.hypot(B[0] - A[0], B[1] - A[1]) / 2, lintel: 3.6, s_in: near(A[0], A[1]), s_out: near(B[0], B[1]), model: 'arch' },
};
};
