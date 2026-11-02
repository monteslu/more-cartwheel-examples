// Red Reach: a river at noon, deep in a sandstone canyon, round a mesa. No swell, no sea: a narrow winding channel between walls, and the river's current.
// The hook: the water runs one way down the valley. Going with it is fast; coming back against it is a slog in midstream, so the line is to hug the bank
// where the water is slack.
export default ({ clamp, lerp, smooth, hash, fbm, ridged, trackOf, strongest }) => {
const P = [[-640, -360], [-470, -470], [-260, -400], [-90, -470], [100, -410], [290, -480], [480, -390], [640, -440], [720, -250], [590, -110], [660, 90], [520, 250], [610, 430], [430, 510], [230, 420], [60, 500], [-130, 410], [-320, 490], [-520, 400], [-660, 470], [-740, 280], [-610, 130], [-690, -70], [-720, -240]];
for (const p of P) p[0] *= 0.74, p[1] *= 0.74;
const { trackDist, pointAt, LEN } = trackOf(P);
const smin = (a, b, k) => { const t = clamp(0.5 + 0.5 * (b - a) / k); return lerp(b, a, t) - k * t * (1 - t); }; // (a rim with a soft edge: a hard one zigzags on the land's grid against the sky)
function height(x, z) {
  const d = trackDist(x, z) + 9 * Math.sin(x * 0.021 + z * 0.013) + (fbm(x * 0.02, z * 0.02, 3, 301) - 0.5) * 18;
  // a bed, a sand bar at the bank, a slope of fallen rock against the wall, then a wall that climbs in steps: benches of harder rock
  let y = lerp(-6, 1.5, smooth(34, 52, d));
  y = lerp(y, Math.max(y, lerp(1.5, 8.5, smooth(50, 80, d + (fbm(x * 0.05, z * 0.05, 3, 305) - 0.5) * 8))), smooth(42, 52, d));
  const up = smooth(66, 84, d);
  if (up > 0) {
    const top = 62 + 58 * fbm(x * 0.004 + 9, z * 0.004, 3, 302);
    const h = Math.max(0, d - 66) * 1.5 + 14 * (fbm(x * 0.009 + 3, z * 0.009, 3, 306) - 0.5);
    // (the wall starts past the sand: a level foot for what stands on the bank, whatever the noise does)
    y = Math.max(y, smin(top, Math.max(0, h - 7), 16) + 6 + 2.5 * (fbm(x * 0.06, z * 0.06, 3, 303) - 0.5));
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
  cliffs: { seed: 303, spurs: [[190, 16], [70, 7], [26, 2.5]], talus: 2.6, bench: 11, beds: [5, 30], dip: [0.006, 0.011], fold: 16, droplets: 500000, capacity: 1.6, cut: 7, keep: 9 },
  // layers 0 river sand, 1 wet silt, 2 red sandstone, 3 pale capstone, 4 deep red, 5 dark desert varnish
  paint({ x, z, y, slope, flow, hard, wall }) {
    const n = fbm(x * 0.03, z * 0.03, 3, 311);
    if (y < 3.2) return [0, 1, 255 * (1 - smooth(-2.5, 1.5, y))];
    // the wall: red sandstone in beds of its own colour, whole masses deeper red, pale hard beds, and the dark stain of water down from each ledge
    if (y > 7 && (wall > 0.25 || slope > 0.8)) {
      const mass = fbm(x * 0.0048 + 7, z * 0.0048, 3, 312);
      return strongest([[2, 0.6], [4, smooth(0.34, 0.66, mass * 0.7 + (1 - hard) * 0.45)], [3, (smooth(0.72, 0.95, hard) * smooth(0.42, 0.66, fbm(x * 0.0075, z * 0.0075, 2, 313)) * 0.75 + smooth(60, 95, y) * smooth(0.35, 0.65, n)) * 0.9], [5, flow * 0.7]]);
    }
    // sand and fallen rock against the wall's foot, by height, not by slope (by slope its edge followed the terrain's grid in teeth), and never up the wall itself
    const foot = (1 - smooth(4 + n * 3, 7 + n * 3, y)) * (1 - smooth(0.55, 0.9, slope));
    if (foot > 0.01) return [2, 0, 255 * foot];
    if (slope < 0.5 && y > 40) return [3, 2, 255 * smooth(0.25, 0.5, slope + (n - 0.5) * 0.3)];
    return [2, 3, 60 * (1 - smooth(0.2, 0.6, slope))];
  },
  mask({ y }) { return Math.max(0.05, 0.5 * smooth(-0.6, -4, y)); },
  maps: {
    cactus({ x, z, y, slope }) { return smooth(2.5, 4, y) * (1 - smooth(9, 12, y)) * (1 - smooth(0.3, 0.5, slope)) * smooth(0.4, 0.55, fbm(x * 0.03, z * 0.03, 3, 321)); },
    hoodoos({ x, z, y, slope }) { return smooth(3, 5, y) * (1 - smooth(10, 14, y)) * (1 - smooth(0.32, 0.5, slope)) * smooth(0.5, 0.62, fbm(x * 0.022 + 4, z * 0.022, 3, 322)); },
    drift({ x, z, y, slope }) { return (1 - smooth(0.16, 0.3, slope)) * smooth(0.4, 1, y) * (1 - smooth(2, 3, y)) * smooth(0.4, 0.6, fbm(x * 0.05 + 3, z * 0.05, 3, 323)); },
    boulders({ x, z, y, slope }) { return smooth(1.6, 3, y) * (1 - smooth(9, 13, y)) * (1 - smooth(0.4, 0.6, slope)) * smooth(0.36, 0.56, fbm(x * 0.04 + 7, z * 0.04, 3, 324)); },
  },
  lying: ['drift', 'boulders'],
  scatter: [
    ['cactus', 'cactus', 0.065, 0.7, 1.4, 650, 277, false, 0.08],
    ['hoodoo', 'hoodoos', 0.04, 0.7, 1.7, 800, 278, false, 0.15],
    ['driftwood', 'drift', 0.04, 0.7, 1.6, 450, 279, true, 0.12],
    ['boulder0', 'boulders', 0.09, 0.6, 2.6, 700, 280, true, 0.25],
    ['boulder1', 'boulders', 0.07, 0.5, 2.0, 700, 281, true, 0.25],
    ['boulder2', 'boulders', 0.055, 0.7, 3.4, 700, 282, true, 0.25],
  ],
  grades: [
    ['riversand', 'sand', [1.3, 1.05, 0.78], 0.15],
    ['silt', 'coast', [0.62, 0.46, 0.33], 0.2],
    ['sandstone', 'cliff_side', [1.0, 0.72, 0.56], 0.055, 1.05],
    ['capstone', 'marble_cliff_03', [1.02, 0.86, 0.68], 0.07, 0.7],
    ['deepred', 'rock_face_03', [0.78, 0.46, 0.34], 0.06, 1],
    ['varnish', 'rock_face_03', [0.3, 0.2, 0.16], 0.07, 0.6],
  ],
  ramp_count: 9,
  ramp_top: 5,
  buoys: { first: 90, step: 84, wobble: 12, offset: 8 },
  // river chop, no swell
  waves: [[20, 17, 0.1, 0.18, 0.6], [110, 11, 0.06, 0.1, 0.6], [65, 6.5, 0.04, 0.05, 0.5], [160, 3.6, 0.015, 0.02, 0.4]],
  swell: [0.8, 1.0],
  tide: [0, 0],
  sky: 'res://textures/sky-canyon.png',
  sun_disc: 1.3,
  side_detile: 0.01,
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
