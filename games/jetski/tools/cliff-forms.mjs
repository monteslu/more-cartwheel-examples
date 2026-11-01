// What makes a tall slope of a course's height map read as a rock face rather than a smooth hill: spurs and gullies that run down the fall line,
// beds of harder and softer rock that stand out as ledges, and water that has cut the result. All of it works on the finished heights, above the
// waterline only, so the course, its buoys and its ramps stay where they are. It also says what a painter wants to know of each sample.
import { clamp, smooth, hash, fbm } from './land-math.mjs';

// box blur, three passes: the slope's general lie, without the small forms on it
function blur(H, N, r) {
  let a = H.slice(), b = new Float32Array(N * N);
  for (let pass = 0; pass < 3; pass++) {
    for (let j = 0; j < N; j++) { let s = 0; for (let i = -r; i <= r; i++) s += a[j * N + clamp(i, 0, N - 1)]; for (let i = 0; i < N; i++) { b[j * N + i] = s / (2 * r + 1); s += a[j * N + clamp(i + r + 1, 0, N - 1)] - a[j * N + clamp(i - r, 0, N - 1)]; } }
    for (let i = 0; i < N; i++) { let s = 0; for (let j = -r; j <= r; j++) s += b[clamp(j, 0, N - 1) * N + i]; for (let j = 0; j < N; j++) { a[j * N + i] = s / (2 * r + 1); s += b[clamp(j + r + 1, 0, N - 1) * N + i] - b[clamp(j - r, 0, N - 1) * N + i]; } }
  }
  return a;
}

// The rock's beds: an uneven stack, thin and thick, hard and soft, from a seed, so no two walls and no two heights agree and nothing comes round again.
export function bedsOf(seed, thin = 5, thick = 34) {
  const tops = [], hard = [];
  let e = -160;
  for (let k = 0; e < 520; k++) {
    const u = hash(k, 3, seed);
    tops.push(e += thin + (thick - thin) * u * u);
    hard.push(hash(k, 7, seed));
  }
  return (y) => {
    let lo = 0, hi = tops.length - 1;
    while (lo < hi) { const m = (lo + hi) >> 1; if (tops[m] < y) lo = m + 1; else hi = m; }
    const bottom = lo ? tops[lo - 1] : -160;
    return { k: lo, t: (y - bottom) / (tops[lo] - bottom), size: tops[lo] - bottom, hard: hard[lo] };
  };
}

// How far the beds rise and fall across the land: they lie tilted and folded, so a bed is not a contour of the height map.
const dipOf = (c) => (x, z) => x * c.dip[0] + z * c.dip[1] + (fbm(x * 0.0036 + 4, z * 0.0036 + 9, 3, c.seed + 5) - 0.5) * c.fold;

/** Rock can stand only so steep: what stands steeper than talus (metres per metre) slides to the sample below it. Takes the needles off, and leaves a skirt of scree at a wall's foot. */
function relax(H, N, spacing, c, passes) {
  const limit = c.talus * spacing, keep = c.keep;
  for (let pass = 0; pass < passes; pass++) {
    const O = H.slice();
    for (let j = 1; j < N - 1; j++) for (let i = 1; i < N - 1; i++) {
      const k = j * N + i;
      if (O[k] < keep) continue;
      for (const q of [k - 1, k + 1, k - N, k + N]) {
        const d = O[k] - O[q];
        if (d > limit && O[q] >= keep - 3) { const m = (d - limit) * 0.2; H[k] -= m; H[q] += m; }
      }
    }
  }
}

/** Water running down the land: droplets that cut where they speed up and drop their load where they slow. Land under keep metres is left as it was. */
function erode(H, N, c) {
  const keep = c.keep, W = new Float32Array(N * N);
  for (let k = 0; k < N * N; k++) W[k] = smooth(keep + 3, keep + 16, H[k]);
  const R = 2, brush = [];
  let bs = 0;
  for (let b = -R; b <= R; b++) for (let a = -R; a <= R; a++) { const w = Math.max(0, 1 - Math.hypot(a, b) / (R + 0.5)); if (w > 0) { brush.push([a, b, w]); bs += w; } }
  for (const q of brush) q[2] /= bs;
  let seed = c.seed >>> 0 || 1;
  const rnd = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
  for (let d = 0; d < c.droplets; d++) {
    let x = 2 + rnd() * (N - 5), y = 2 + rnd() * (N - 5);
    if (H[Math.floor(y) * N + Math.floor(x)] < keep + 3) continue;
    let dx = 0, dy = 0, speed = 1, water = 1, load = 0;
    for (let life = 0; life < 64; life++) {
      const ix = Math.floor(x), iy = Math.floor(y);
      if (ix < 1 || iy < 1 || ix >= N - 2 || iy >= N - 2) break;
      const u = x - ix, v = y - iy, k = iy * N + ix, a = H[k], b = H[k + 1], e = H[k + N], f = H[k + N + 1];
      const h = (a * (1 - u) + b * u) * (1 - v) + (e * (1 - u) + f * u) * v;
      const gx = (b - a) * (1 - v) + (f - e) * v, gy = (e - a) * (1 - u) + (f - b) * u;
      dx = dx * 0.08 - gx * 0.92, dy = dy * 0.08 - gy * 0.92;
      const l = Math.hypot(dx, dy);
      if (l < 1e-5) break;
      dx /= l, dy /= l;
      const nx = x + dx, ny = y + dy, jx = Math.floor(nx), jy = Math.floor(ny);
      if (jx < 1 || jy < 1 || jx >= N - 2 || jy >= N - 2) break;
      const s = nx - jx, t = ny - jy, m = jy * N + jx;
      const h2 = (H[m] * (1 - s) + H[m + 1] * s) * (1 - t) + (H[m + N] * (1 - s) + H[m + N + 1] * s) * t;
      const dh = h2 - h, cap = Math.max(-dh, 0.02) * speed * water * c.capacity;
      if (load > cap || dh > 0) {
        const put = dh > 0 ? Math.min(dh, load) : (load - cap) * 0.25;
        load -= put;
        const w0 = W[k] * 0.2; // (most of it washes away: all of it left cones at the wall's foot)
        H[k] += put * (1 - u) * (1 - v) * w0, H[k + 1] += put * u * (1 - v) * w0, H[k + N] += put * (1 - u) * v * w0, H[k + N + 1] += put * u * v * w0;
      } else {
        const take = Math.min((cap - load) * 0.3, -dh, 0.5); // (no more than this a step: a drop in a pit it is digging deepens it faster each step)
        for (const [a2, b2, w2] of brush) { const q = (iy + b2) * N + ix + a2; if (q < 0 || q >= N * N) continue; const got = take * w2 * W[q]; H[q] -= got; load += got; }
      }
      speed = Math.sqrt(Math.max(0, speed * speed - dh * 1.2));
      water *= 0.985;
      x = nx, y = ny;
    }
  }
}

/**
 * Reshapes the height map in place and returns the painter's fields.
 * @param {Float32Array} H heights, N x N samples, spacing metres apart, world (0, 0) at the middle
 * @param {{seed:number, spurs:number[][], bench:number, dip:number[], fold:number, droplets:number, capacity:number, keep:number, beds?:number[]}} c
 *   spurs [wavelength along the wall, height] per scale; bench how far a hard bed stands out, metres; dip metres per metre across x and z; fold metres of bending;
 *   droplets how many; capacity how much a drop carries; keep metres over the sea left alone; talus how steep rock can stand; cut how deep water may cut, metres; beds [thinnest, thickest] metres; spare(x, z) 0..1 how much of a place stays as the course made it
 */
export function shapeCliffs(H, N, spacing, c) {
  const toW = (i) => (i - (N - 1) / 2) * spacing, at = (i, j) => H[clamp(j, 0, N - 1) * N + clamp(i, 0, N - 1)];
  const original = c.spare ? H.slice() : null; // (what a course needs left as it is keeps its shape)
  const L = blur(H, N, 7), dip = dipOf(c), beds = bedsOf(c.seed, ...(c.beds || []));
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    const k = j * N + i, y = H[k];
    if (y < c.keep) continue;
    const x = toW(i), z = toW(j);
    const gi = clamp(i, 1, N - 2), gj = clamp(j, 1, N - 2);
    const gx = (L[gj * N + gi + 1] - L[gj * N + gi - 1]) / (2 * spacing), gz = (L[(gj + 1) * N + gi] - L[(gj - 1) * N + gi]) / (2 * spacing), s = Math.hypot(gx, gz);
    const wall = smooth(0.35, 0.9, s), up = smooth(c.keep + 6, c.keep + 46, y);
    let v = y;
    if (wall > 0) {
      // follow the slope down to its foot: every point of one fall line reads the same noise, so these forms run straight down the wall
      const run = Math.min(L[k], 220) / Math.max(s, 0.35), px = x + gx / s * run, pz = z + gz / s * run;
      for (let q = 0; q < c.spurs.length; q++) {
        const [len, amp] = c.spurs[q], f = 1 / len;
        v += wall * up * amp * (fbm(px * f + q * 13.1, pz * f, 2, c.seed + 20 + q) - 0.5) * 2;
      }
    }
    // the beds: a hard one stands out as a ledge with a sheer face under it, a soft one is cut back. Each comes and goes along the wall
    const b = beds(v + dip(x, z)), comes = smooth(0.32, 0.62, fbm(x * 0.011 + b.k * 3.7, z * 0.011, 2, c.seed + 40));
    const hard = smooth(0.45, 0.8, b.hard) * comes * wall * up;
    v += hard * c.bench * Math.min(1, b.size / 14) * (Math.pow(b.t, 3) * 1.6 - 0.45);
    H[k] = v;
  }
  relax(H, N, spacing, c, 24);
  const before = H.slice();
  erode(H, N, c);
  for (let k = 0; k < N * N; k++) if (H[k] < before[k] - c.cut) H[k] = before[k] - c.cut; // (water cuts into a wall, it does not saw it to pieces)
  relax(H, N, spacing, { ...c, talus: c.talus * 0.72 }, 14); // (a cut's walls lie back: a face turned away from the eye catches the sky along its whole length)
  // and a wall's foot lies back as a skirt of fallen rock, with no fins standing in the water
  { const soft = blur(H, N, 4); for (let j = 1; j < N - 1; j++) for (let i = 1; i < N - 1; i++) { const k = j * N + i; if (H[k] <= 0.2) continue; const lie = Math.hypot(L[k + 1] - L[k - 1], L[k + N] - L[k - N]) / (2 * spacing), t = 1 - smooth(0.35, 0.9, lie) * (1 - smooth(c.keep + 2, c.keep + 20, H[k])); H[k] = Math.max(0.2, soft[k] + (H[k] - soft[k]) * t); } }
  if (c.spare) for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) { const k = j * N + i, a = c.spare(toW(i), toW(j)); if (a > 0) H[k] += (original[k] - H[k]) * a; }
  // where water goes: each sample, highest first, hands what it has to its lowest neighbour. Streaks run down from the ledges and gather in the gullies
  const order = new Uint32Array(N * N);
  for (let k = 0; k < N * N; k++) order[k] = k;
  order.sort((p, q) => H[q] - H[p]);
  const F = new Float32Array(N * N).fill(1);
  for (const k of order) {
    const i = k % N, j = (k - i) / N;
    if (i < 1 || j < 1 || i >= N - 1 || j >= N - 1) continue;
    let best = -1, drop = 0;
    for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) { if (!a && !b) continue; const d = (H[k] - H[(j + b) * N + i + a]) / Math.hypot(a, b); if (d > drop) drop = d, best = (j + b) * N + i + a; }
    if (best >= 0) F[best] += F[k];
  }
  const Sl = blur(H, N, 5);
  return {
    /** what the painter gets at a sample: flow 0..1 (a streak to a gully), hard 0..1 (the bed here), bed its number, wall 0..1 (how much the land's general lie is a wall) */
    at(i, j, x, z, y) {
      const k = j * N + i, gi = clamp(i, 1, N - 2), gj = clamp(j, 1, N - 2);
      const s = Math.hypot(Sl[gj * N + gi + 1] - Sl[gj * N + gi - 1], Sl[(gj + 1) * N + gi] - Sl[(gj - 1) * N + gi]) / (2 * spacing);
      const b = beds(y + dip(x, z));
      return { flow: smooth(0.6, 3.6, Math.log10(F[k])), hard: b.hard, bed: b.k, t: b.t, wall: smooth(0.35, 0.9, s) };
    },
    at_h: at,
  };
}

/** The two strongest of a list of [layer, weight], as a control word's base, over and blend. */
export function strongest(list) {
  let a = [0, -1], b = [0, -1];
  for (const q of list) { if (q[1] > a[1]) { b = a; a = q; } else if (q[1] > b[1]) b = q; }
  const sum = Math.max(1e-4, a[1] + Math.max(0, b[1]));
  return [a[0], b[0], clamp(Math.max(0, b[1]) / sum) * 255];
}
