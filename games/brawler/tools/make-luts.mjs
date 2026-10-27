// Colour grading LUTs (.cube, 17 cubed) for each stage's mood: split-toned shadows and highlights, contrast, saturation.
// node tools/make-luts.mjs
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'luts');
const clamp = (x) => Math.max(0, Math.min(1, x));
const S = (x, k) => { const t = x * 2 - 1; const s = Math.sign(t) * Math.pow(Math.abs(t), 1 / (1 + k)); return clamp(s * 0.5 + 0.5); };
// shadows: tint added in the darks; highs: tint multiplied into the lights; sat: saturation; con: contrast; gam: midtone gamma
const MOODS = {
  alley: { shadows: [0.01, 0.0, 0.03], highs: [1.08, 0.98, 0.88], sat: 1.12, con: 0.25, gam: 1.0 },
  docks: { shadows: [0.0, 0.015, 0.03], highs: [0.9, 1.02, 1.1], sat: 0.85, con: 0.2, gam: 0.95 },
  arcade: { shadows: [0.03, 0.0, 0.05], highs: [1.05, 0.95, 1.1], sat: 1.3, con: 0.35, gam: 1.05 },
  factory: { shadows: [0.02, 0.005, 0.0], highs: [1.12, 0.98, 0.82], sat: 1.15, con: 0.3, gam: 1.0 },
};
const N = 17;
for (const [name, m] of Object.entries(MOODS)) {
  const lines = [`TITLE "brawler ${name}"`, `LUT_3D_SIZE ${N}`];
  for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
    let c = [r, g, b].map((x) => x / (N - 1));
    const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    c = c.map((x) => clamp(l + (x - l) * m.sat));
    c = c.map((x, i) => clamp(x + m.shadows[i] * (1 - l) * (1 - l)));
    c = c.map((x, i) => clamp(x * (1 + (m.highs[i] - 1) * l)));
    c = c.map((x) => S(Math.pow(x, m.gam), m.con));
    lines.push(c.map((x) => x.toFixed(5)).join(' '));
  }
  writeFileSync(join(OUT, name + '.cube'), lines.join('\n') + '\n');
}
console.log('luts ok');
