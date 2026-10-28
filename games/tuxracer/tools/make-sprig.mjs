// A snow-dusted spruce branch sprig as an alpha card (assets/textures/sprig.png):
// the branch runs from the base at the left to the tip at the right, side twigs
// with needle strokes in a few greens, snow settled on top of each twig.
// Deterministic (its own RNG). The card is used by src/tree_mesh.c.
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { writePng } from '../../../deps/cartwheel-engine/tools/lib/png.mjs';
const root = resolve(dirname(new URL(import.meta.url).pathname), '..');
const N = 512;
const img = new Float32Array(N * N * 4);
let seed = 12345;
const rnd = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
const blend = (x, y, r, g, b, a) => {
  if (x < 0 || y < 0 || x >= N || y >= N) return;
  const i = (x + y * N) * 4, da = img[i + 3];
  const na = a + da * (1 - a);
  if (na <= 0) return;
  img[i] = (r * a + img[i] * da * (1 - a)) / na;
  img[i + 1] = (g * a + img[i + 1] * da * (1 - a)) / na;
  img[i + 2] = (b * a + img[i + 2] * da * (1 - a)) / na;
  img[i + 3] = na;
};
const stroke = (x0, y0, x1, y1, w, r, g, b, a) => {
  const len = Math.hypot(x1 - x0, y1 - y0), steps = Math.ceil(len * 2) + 1;
  for (let s = 0; s <= steps; s++) {
    const t = s / steps, cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t;
    const rr = Math.ceil(w);
    for (let dy = -rr; dy <= rr; dy++) for (let dx = -rr; dx <= rr; dx++) {
      const d = Math.hypot(dx, dy) / w;
      if (d > 1) continue;
      blend(Math.round(cx + dx), Math.round(cy + dy), r, g, b, a * (1 - d * d));
    }
  }
};
const greens = [[0.10, 0.22, 0.09], [0.14, 0.30, 0.12], [0.18, 0.36, 0.16], [0.08, 0.18, 0.08], [0.22, 0.40, 0.19]];
// the stem: base at x 20, tip at x 500, a slight droop
const stem = (t) => [20 + t * 480, 256 + Math.sin(t * 1.6) * 30 * t];
for (let t = 0; t < 1; t += 0.004) { const [x, y] = stem(t), [x2, y2] = stem(t + 0.004); stroke(x, y, x2, y2, 4 - 2.5 * t, 0.3, 0.2, 0.12, 1); }
// side twigs with needles, the twigs shorter toward the tip
const twigs = 26;
for (let i = 0; i < twigs; i++) {
  const t = 0.04 + (i / twigs) * 0.94;
  const [sx, sy] = stem(t);
  for (const side of [-1, 1]) {
    const len = (95 + rnd() * 45) * (1 - t * 0.7);
    const ang = side * (0.55 + rnd() * 0.35) + 0.35; // swept toward the tip
    const ex = sx + Math.cos(ang) * len, ey = sy + Math.sin(ang) * len * side * (side < 0 ? -1 : 1) * (side < 0 ? 1 : 1);
    const tx = sx + Math.cos(0.35 + rnd() * 0.2) * len, ty = sy + side * Math.sin(0.9 + rnd() * 0.3) * len;
    stroke(sx, sy, tx, ty, 1.6, 0.28, 0.2, 0.12, 1);
    // needles along the twig, both sides, in pairs
    const nn = Math.floor(len / 2.2);
    for (let k = 0; k < nn; k++) {
      const u = k / nn, px = sx + (tx - sx) * u, py = sy + (ty - sy) * u;
      const nl = 13 + rnd() * 12 * (1 - u * 0.4);
      const g = greens[Math.floor(rnd() * greens.length)];
      for (const ns of [-1, 1]) {
        const a = Math.atan2(ty - sy, tx - sx) + ns * (0.75 + rnd() * 0.6);
        stroke(px, py, px + Math.cos(a) * nl, py + Math.sin(a) * nl, 1.7, g[0], g[1], g[2], 0.95);
      }
    }
    // snow lying along the upper side of the twig: a soft streak, broken here and there
    if (side < 0) {
      const pieces = 2 + Math.floor(rnd() * 3);
      for (let k = 0; k < pieces; k++) {
        const u0 = rnd() * 0.7, u1 = Math.min(0.98, u0 + 0.15 + rnd() * 0.3);
        const w = 5 + rnd() * 5;
        stroke(sx + (tx - sx) * u0, sy + (ty - sy) * u0 - w * 0.8, sx + (tx - sx) * u1, sy + (ty - sy) * u1 - w * 0.8, w, 0.96, 0.975, 1.0, 0.92);
      }
    }
  }
}
// snow along the stem top
for (let t = 0.05; t < 0.95; t += 0.02 + rnd() * 0.03) { const [x, y] = stem(t); const rr = 4 + rnd() * 5; for (let dy = -rr; dy <= rr; dy++) for (let dx = -rr; dx <= rr; dx++) { const d = Math.hypot(dx, dy * 1.5) / rr; if (d <= 1) blend(Math.round(x + dx), Math.round(y - 3 + dy), 0.95, 0.97, 1.0, 0.9 * (1 - d * d * d)); } }
const out = new Uint8Array(N * N * 4);
for (let i = 0; i < N * N; i++) {
  const a = img[i * 4 + 3];
  const g = (v) => Math.round(Math.min(1, Math.max(0, v)) ** (1 / 2.2) * 255);
  out[i * 4] = g(img[i * 4]), out[i * 4 + 1] = g(img[i * 4 + 1]), out[i * 4 + 2] = g(img[i * 4 + 2]), out[i * 4 + 3] = Math.round(Math.min(1, a) * 255);
}
mkdirSync(join(root, 'assets/textures'), { recursive: true });
writePng(join(root, 'assets/textures/sprig.png'), N, N, out);
console.log('sprig.png');
