// Colour grading LUTs per condition (assets/luts/*.cube, Adobe .cube, 17 steps),
// for the engine's PostProcess grade pass (applied on display values).
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
const root = resolve(dirname(new URL(import.meta.url).pathname), '..');
mkdirSync(resolve(root, 'assets/luts'), { recursive: true });
const N = 17;
const clamp = (x) => Math.min(1, Math.max(0, x));
const lum = (r, g, b) => 0.2126 * r + 0.7152 * g + 0.0722 * b;
const sat = ([r, g, b], s) => { const l = lum(r, g, b); return [l + (r - l) * s, l + (g - l) * s, l + (b - l) * s]; };
const contrast = ([r, g, b], c) => [r, g, b].map((v) => 0.5 + (v - 0.5) * c);
const scurve = ([r, g, b], k) => [r, g, b].map((v) => v - k * Math.sin(2 * Math.PI * v) / (2 * Math.PI)); // k: deeper shadows, brighter highs
const tint = ([r, g, b], lo, hi) => { // shadows toward lo, highlights toward hi (small per-channel offsets)
  const l = lum(r, g, b);
  return [r + lo[0] * (1 - l) + hi[0] * l, g + lo[1] * (1 - l) + hi[1] * l, b + lo[2] * (1 - l) + hi[2] * l];
};
const GRADES = {
  // a bright winter morning: a touch of warmth in the lit snow, cool blue in the shadows, a little snap
  sunny: (c) => scurve(tint(sat(c, 1.06), [-0.01, 0.0, 0.035], [0.025, 0.012, -0.015]), 0.12),
  // overcast: flatter, desaturated, slightly cool and lifted
  cloudy: (c) => tint(contrast(sat(c, 0.86), 0.94), [0.02, 0.025, 0.04], [-0.01, 0, 0.01]),
  // night: blue, desaturated, blacks crushed a little
  night: (c) => scurve(tint(sat(c, 0.7), [-0.03, -0.015, 0.05], [-0.02, 0.0, 0.04]), 0.08),
};
for (const [name, f] of Object.entries(GRADES)) {
  const lines = [`TITLE "tuxracer ${name}"`, `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
  for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
    const o = f([r / (N - 1), g / (N - 1), b / (N - 1)]).map(clamp);
    lines.push(o.map((v) => v.toFixed(5)).join(' '));
  }
  writeFileSync(resolve(root, `assets/luts/${name}.cube`), lines.join('\n') + '\n');
  console.log(name, 'written');
}
