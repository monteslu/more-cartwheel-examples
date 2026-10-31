// Red Reach: a river at noon, deep in a sandstone canyon, round a mesa. No swell, no sea: a narrow winding channel between walls, and the river's current.
// The hook: the water runs one way down the valley. Going with it is fast; coming back against it is a slog in midstream, so the line is to hug the bank
// where the water is slack.
export default ({ clamp, lerp, smooth, hash, fbm, ridged, trackOf }) => {
const P = [[-640, -360], [-470, -470], [-260, -400], [-90, -470], [100, -410], [290, -480], [480, -390], [640, -440], [720, -250], [590, -110], [660, 90], [520, 250], [610, 430], [430, 510], [230, 420], [60, 500], [-130, 410], [-320, 490], [-520, 400], [-660, 470], [-740, 280], [-610, 130], [-690, -70], [-720, -240]];
for (const p of P) p[0] *= 0.74, p[1] *= 0.74;
const { trackDist, pointAt, LEN } = trackOf(P);
function height(x, z) {
  const d = trackDist(x, z) + 9 * Math.sin(x * 0.021 + z * 0.013) + (fbm(x * 0.02, z * 0.02, 3, 301) - 0.5) * 18;
  // a bed, sand bars at the banks, then a wall that climbs in steps: benches of harder rock
  let y = lerp(-6, 1.5, smooth(34, 52, d));
  const up = smooth(50, 70, d);
  if (up > 0) {
    const top = 62 + 58 * fbm(x * 0.004 + 9, z * 0.004, 3, 302);
    const h = Math.max(0, d - 50) * 1.9;
    // terraces: the climb flattens at each hard band
    const band = 16, t = h / band, fr = t - Math.floor(t), step = (Math.floor(t) + smooth(0.55, 1, fr)) * band;
    y = Math.max(y, Math.min(top, step) + 2 + 2.5 * (fbm(x * 0.06, z * 0.06, 3, 303) - 0.5));
  }
  // the broken crown of the mesa
  if (y > 40) y += (ridged(x * 0.012, z * 0.012, 4, 304) - 0.5) * 14 * smooth(40, 90, y);
  return y;
}
return {
  id: 'canyon',
  name: 'Red Reach',
  track: P,
  height,
  // layers 0 river sand, 1 wet silt, 2 red sandstone, 3 pale capstone
  paint({ x, z, y, slope }) {
    const n = fbm(x * 0.03, z * 0.03, 3, 311);
    if (y < 3.2) return [0, 1, 255 * (1 - smooth(-2.5, 1.5, y))];
    // sand banked up against the wall's foot, by height, not by slope (by slope its edge followed the terrain's grid in teeth)
    const foot = 1 - smooth(4 + n * 9, 8 + n * 11, y);
    if (foot > 0.01) return [2, 0, 255 * foot];
    if (slope < 0.5 && y > 40) return [3, 2, 255 * smooth(0.25, 0.5, slope + (n - 0.5) * 0.3)];
    return [2, 3, 60 * (1 - smooth(0.2, 0.6, slope))];
  },
  mask({ y }) { return Math.max(0.05, 0.5 * smooth(-0.6, -4, y)); },
  maps: {
    cactus({ x, z, y, slope }) { return smooth(2.5, 5, y) * (1 - smooth(14, 26, y)) * (1 - smooth(0.2, 0.45, slope)) * smooth(0.45, 0.62, fbm(x * 0.03, z * 0.03, 3, 321)) * smooth(30, 44, trackDist(x, z)); },
    hoodoos({ x, z, y, slope }) { return smooth(1.8, 3, y) * (1 - smooth(30, 50, y)) * (1 - smooth(0.5, 0.9, slope)) * smooth(0.52, 0.66, fbm(x * 0.022 + 4, z * 0.022, 3, 322)) * smooth(36, 48, trackDist(x, z)); },
    drift({ x, z, y }) { return smooth(0.4, 1, y) * (1 - smooth(2, 3, y)) * smooth(0.45, 0.65, fbm(x * 0.05 + 3, z * 0.05, 3, 323)) * smooth(28, 38, trackDist(x, z)); },
  },
  lying: ['drift'],
  scatter: [
    ['cactus', 'cactus', 0.02, 0.7, 1.4, 650, 277, false, 0.08],
    ['hoodoo', 'hoodoos', 0.012, 0.7, 1.7, 800, 278, false, 0.15],
    ['driftwood', 'drift', 0.03, 0.7, 1.6, 450, 279, true, 0.12],
  ],
  grades: [
    ['riversand', 'sand', [1.3, 1.05, 0.78], 0.15],
    ['silt', 'coast', [0.62, 0.46, 0.33], 0.2],
    ['sandstone', 'rock', [1.05, 0.56, 0.36], 0.05],
    ['capstone', 'rock', [1.1, 0.92, 0.72], 0.07],
  ],
  ramp_count: 9,
  ramp_routes: 1,
  ramp_top: 5,
  buoys: { first: 90, step: 84, wobble: 12, offset: 8 },
  // river chop, no swell
  waves: [[20, 17, 0.1, 0.18, 0.6], [110, 11, 0.06, 0.1, 0.6], [65, 6.5, 0.04, 0.05, 0.5], [160, 3.6, 0.015, 0.02, 0.4]],
  swell: [0.8, 1.0],
  tide: [0, 0],
  sky: 'res://textures/sky-canyon.png',
  sun_disc: 1.3,
  props: [
    ['Sky', 'ground_color', 'color(0.5, 0.26, 0.15, 1)'],
    ['Sky', 'time_of_day', '12.6'], ['Sky', 'sun_energy', '4.2'], ['Sky', 'sky_energy', '1.6'], ['Sky', 'north', '120'],
    ['Fill', 'energy', '1.2'], ['Fill', 'rotation', 'vec3(-0.4, 2.2, 0)'], ['Fill', 'color', 'color(0.95, 0.62, 0.42, 1)'],
    ['Post', 'exposure', '0.9'], ['Post', 'bloom_intensity', '0.05'],
    ['Water', 'shallow_color', 'color(0.26, 0.34, 0.16, 1)'], ['Water', 'deep_color', 'color(0.06, 0.13, 0.09, 1)'],
    ['Water', 'color', 'color(0.95, 0.92, 0.82, 1)'], ['Water', 'clarity', '2.5'], ['Water', 'murk', '1.6'],
    ['Water', 'distortion', '0.16'], ['Water', 'normal_scale', '0.42'], ['Water', 'detail', '1'],
    ['Water', 'flow_speed', '0.18'], ['Water', 'flow_direction', 'vec2(1, 0)'],
    ['Water', 'crest_foam', '0.5'], ['Water', 'crest_light', '0.2'], ['Water', 'scatter', '0.4'], ['Water', 'glint', '1.6'], ['Water', 'shore_foam', '1.2'],
  ],
  hook: { kind: 2, x: 0, z: 0, dirx: 1, dirz: 0, half_w: 34, half_len: 0, lintel: 0, s_in: 0, s_out: 0, current: 6, model: null },
};
};
