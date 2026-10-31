// Crater Atoll: a volcanic atoll at the end of a warm day. A course definition: see tools/course-lib.mjs for what each part does.
export default ({ clamp, lerp, smooth, hash, fbm, ridged, trackOf }) => {
// The track: a closed loop, metres, x east, z south. Lagoon, out through the east gap into the open swell, round the
// islet's sea stacks, back in over the north pass.
const P = [[-140, 150], [-20, 195], [110, 170], [205, 95], [290, 25], [395, 5], [510, 15], [625, -40], [690, -160], [670, -300], [570, -400], [430, -440], [290, -445], [165, -395], [85, -300], [40, -200], [-40, -120], [-150, -115], [-250, -55], [-290, 45], [-245, 125]];
const { trackDist } = trackOf(P);
// sea stacks: basalt pillars off the east and north shores
const STACKS = [[560, 130, 34, 15], [600, 165, 22, 10], [760, -60, 46, 18], [785, -20, 26, 11], [770, -300, 38, 16], [545, -320, 58, 26], [490, -280, 34, 15], [600, -270, 24, 11], [430, -540, 42, 17], [230, -540, 30, 14], [400, 110, 20, 10]];

function height(x, z) {
  const r0 = Math.hypot(x, z), a = Math.atan2(z, x);
  const wob = (fbm(Math.cos(a) * 2 + 7, Math.sin(a) * 2 + 3, 4, 11) - 0.5) * 120;
  const R = 345 + wob;
  // the shelf falling to the deep
  let y = lerp(-46, -9, smooth(1000, R + 30, r0));
  // lagoon: pale sand a few metres down, with shoals
  const lag = 1 - smooth(R - 95, R - 40, r0);
  const lagFloor = -3.6 - 3.2 * fbm(x * 0.012, z * 0.012, 3, 5) + 2.4 * Math.max(0, fbm(x * 0.03 + 9, z * 0.03, 3, 6) - 0.55) * 4;
  y = lerp(y, lagFloor, lag);
  // the crater's rim: walls to the west and south-west, low cays to the east
  const wall = Math.pow(0.5 + 0.5 * Math.cos(a - Math.PI * 0.95), 1.4);
  const rimH = 5 + 26 * smooth(0.4, 0.75, fbm(Math.cos(a) * 5 + 2, Math.sin(a) * 5, 3, 22)) + 150 * wall * (0.55 + 0.75 * fbm(Math.cos(a) * 3, Math.sin(a) * 3, 3, 21));
  const w = 60 + 125 * wall;
  const g = Math.exp(-Math.pow((r0 - R - 8 * wall) / w, 2));
  let rim = (rimH + 3) * g - 3;
  if (wall > 0.05) rim += g * wall * 34 * (ridged(x * 0.018, z * 0.018, 5, 31) - 0.5) * 2;
  rim += g * 3 * (fbm(x * 0.05, z * 0.05, 4, 33) - 0.5) * 2;
  y = Math.max(y, rim);
  // the land's skin: gullies running downhill and broken ground; beaches keep smooth
  if (y > 1) {
    const k = smooth(1.5, 12, y);
    y += k * ((ridged(x * 0.03, z * 0.03, 5, 61) - 0.55) * 13 * (0.35 + 0.65 * smooth(8, 50, y)) + (fbm(x * 0.14, z * 0.14, 3, 62) - 0.5) * 3.2);
  }
  // rock strata: ledges across the slope, so a wall reads as layers, not as streaks down it
  if (y > 6) { const bands = fbm(x * 0.01 + 2, z * 0.01, 2, 63) * 22; y += smooth(6, 30, y) * 1.7 * Math.sin((y + bands) * 0.42); }
  // sea stacks
  for (const [sx, sz, h, sw] of STACKS) {
    const d = Math.hypot(x - sx, z - sz) * (0.8 + 0.7 * fbm(x * 0.07 + sx, z * 0.07, 3, 41));
    const prof = 1 - smooth(sw * 0.72, sw * 0.98, d);
    const top = h * (0.72 + 0.5 * ridged(x * 0.045, z * 0.045, 4, 42)) * (1 - 0.35 * smooth(0, sw, d));
    y = Math.max(y, lerp(-30, top, prof));
  }
  // the channel: the course stays in water
  const d = trackDist(x, z);
  const inLagoon = lag;
  const channel = lerp(-7, -3.2, inLagoon);
  if (y > channel) y = lerp(channel, y, smooth(46, 92, d));
  return y;
}

return {
  id: 'atoll',
  name: 'Crater Atoll',
  track: P,
  height,
  // layers 0 pale sand, 1 wet coast sand, 2 basalt, 3 jungle
  paint({ x, z, y, slope }) {
    const n1 = fbm(x * 0.03, z * 0.03, 4, 51), n2 = fbm(x * 0.11, z * 0.11, 3, 52);
    const steep = smooth(0.95, 1.5, slope + (n2 - 0.5) * 0.7);
    const jungle = smooth(3.5 + n1 * 5, 8 + n1 * 8, y) * (1 - smooth(95 + n1 * 50, 135 + n1 * 50, y));
    let base = 0, over = 1, blend = 0;
    if (y < -1.2) { base = 0; over = 1; blend = smooth(-3, -7, y) * 140 + 100 * smooth(0.52, 0.72, n1); }
    else if (y < 1.0) { base = 0; over = 1; blend = 255 * smooth(0.9, -0.4, y); }
    else { base = 0; over = 3; blend = 255 * jungle; }
    if (y > 2 || y < -10) { const rocky = Math.max(steep, y > 20 ? 0.1 : 0); if (rocky > 0.35) { base = jungle > 0.5 ? 3 : base; over = 2; blend = 255 * smooth(0.35, 0.8, rocky); } }
    if (y >= 0.4 && slope > 0.95) { base = 2; over = 3; blend = 255 * jungle * (1 - steep); }
    return [base, over, blend];
  },
  // how free the swell is: open sea 1; it dies in the lagoon's shelter and as the floor shoals
  mask({ x, z, y, r0 }) {
  const open = smooth(250, 400, r0 + (fbm(x * 0.004, z * 0.004, 2, 71) - 0.5) * 60);
  const shoal = smooth(-0.8, -8, y);
    return Math.max(0.05, Math.max(open, 0.26) * shoal);
  },
  maps: {
    palms({ x, z, y, slope }) {
      return smooth(1.3, 2.4, y) * (1 - smooth(55, 80, y)) * (1 - smooth(0.45, 0.7, slope)) * smooth(0.42, 0.62, fbm(x * 0.02, z * 0.02, 3, 81));
    },
    rocks({ x, z, y, slope }) {
    const band = smooth(-1.6, -0.5, y) * (1 - smooth(1.2, 4.5, y));
    const clump = smooth(0.4, 0.62, fbm(x * 0.05 + 3, z * 0.05, 3, 91));
    const away = smooth(24, 40, trackDist(x, z));
      return clamp(band * clump * (0.25 + slope) * 2.2) * away;
    },
    shrubs({ x, z, y, slope }) {
      return smooth(3, 8, y) * (1 - smooth(85, 120, y)) * (1 - smooth(0.9, 1.5, slope)) * smooth(0.3, 0.55, fbm(x * 0.04, z * 0.04, 3, 92));
    },
  },
  lying: ['rocks'],
  // model, density map, density, scale range, visible range, seed, sink, tilt
  scatter: [
    ['palm', 'palms', 0.012, 0.75, 1.45, 900, 77, false, 0.25],
    ['shrub', 'shrubs', 0.03, 0.8, 2.0, 450, 78, false, 0.35],
    ['rock0', 'rocks', 0.05, 0.6, 2.4, 700, 79, true, 0.3],
    ['rock1', 'rocks', 0.04, 0.5, 1.8, 700, 80, true, 0.25],
    ['rock2', 'rocks', 0.03, 0.7, 3.2, 700, 81, true, 0.35],
  ],
  // the CC0 scans graded into this place's palette: layer, scan, rgb, the texture's scale
  grades: [
    ['sand', 'sand', [1.28, 1.2, 1.02], 0.16],
    ['coast', 'coast', [0.72, 0.66, 0.56], 0.22],
    ['rock', 'rock', [0.52, 0.47, 0.45], 0.075],
    ['grass', 'grass', [0.42, 0.74, 0.32], 0.09],
  ],
  ramp_count: 9,
  ramp_top: 5,
  // the lighthouse: on the top of the islet in the north-east
  landmark: { model: 'lighthouse', where: (x, z) => x > 120 && x < 420 && z > -340 && z < -80 },
  // the sea: direction, wavelength, amplitude at no swell, amplitude per unit of swell, steepness
  waves: [[200, 46, 0, 1.5, 0.7], [216, 37, 0, 1.05, 0.7], [150, 14, 0.14, 0.22, 0.65], [252, 7, 0.05, 0.05, 0.5]],
  swell: [0.62, 1.0],
  tide: [0, 0.5],
  sky: 'res://textures/sky.png',
  // set on the scene's nodes when the course loads
  props: [
    ['Sky', 'ground_color', 'color(0.3, 0.27, 0.23, 1)'],
    ['Sky', 'time_of_day', '16.9'], ['Sky', 'sun_energy', '3.6'], ['Sky', 'sky_energy', '1.7'], ['Sky', 'north', '200'],
    ['Fill', 'energy', '1.7'], ['Fill', 'rotation', 'vec3(-0.6, -1.6, 0)'], ['Fill', 'color', 'color(0.55, 0.62, 0.95, 1)'],
    ['Post', 'exposure', '1.0'], ['Post', 'bloom_intensity', '0.09'],
    ['Water', 'shallow_color', 'color(0.05, 0.62, 0.58, 1)'], ['Water', 'deep_color', 'color(0.02, 0.14, 0.24, 1)'],
    ['Water', 'color', 'color(0.93, 0.99, 0.98, 1)'], ['Water', 'clarity', '9'], ['Water', 'murk', '1'],
    // (ripples strong enough to break up a mirrored skyline: at less, its edge lies on the water as a hard patch)
    ['Water', 'distortion', '0.16'], ['Water', 'normal_scale', '0.3'], ['Water', 'detail', '0.9'], ['Water', 'crest_foam', '1'], ['Water', 'crest_light', '1'], ['Water', 'scatter', '1.1'], ['Water', 'glint', '1.2'], ['Water', 'shore_foam', '1.6'],
  ],
};
};
