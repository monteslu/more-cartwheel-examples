// Colour grades as 17^3 .cube LUTs (our own maths): contrast around mid grey, saturation, and a tint
// for the shadows and the highlights. Usage: node tools/make-luts.mjs   (writes luts/*.cube)
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const out = join(dirname(fileURLToPath(import.meta.url)), '..', 'luts');
mkdirSync(out, { recursive: true });
const GRADES = {
  foundry: { contrast: 1.18, sat: 1.12, lift: [0.0, 0.012, 0.02], shadow: [0.85, 1.0, 1.12], high: [1.12, 1.0, 0.86], gamma: 1.04 },
  courtyard: { contrast: 1.12, sat: 1.15, lift: [0.01, 0.006, 0], shadow: [0.95, 1.0, 1.08], high: [1.1, 1.02, 0.9], gamma: 1.02 },
  terraces: { contrast: 1.22, sat: 1.1, lift: [0.01, 0.0, 0.025], shadow: [0.9, 0.95, 1.18], high: [1.14, 0.98, 0.86], gamma: 1.05 },
};
const N = 17;
const clamp = (x) => Math.min(1, Math.max(0, x));
for (const [name, g] of Object.entries(GRADES)) {
  const lines = [`TITLE "arenafps ${name}"`, `LUT_3D_SIZE ${N}`];
  for (let b = 0; b < N; b++) for (let gg = 0; gg < N; gg++) for (let r = 0; r < N; r++) {
    let c = [r, gg, b].map((v) => v / (N - 1));
    const lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    c = c.map((v) => Math.pow(clamp(v), 1 / g.gamma));
    const l2 = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    c = c.map((v) => l2 + (v - l2) * g.sat);
    c = c.map((v) => 0.5 + (v - 0.5) * g.contrast);
    const w = clamp(lum); // shadows tint by (1-w), highlights by w
    c = c.map((v, i) => v * (g.shadow[i] * (1 - w) + g.high[i] * w) + g.lift[i] * (1 - w));
    lines.push(c.map((v) => clamp(v).toFixed(5)).join(' '));
  }
  writeFileSync(join(out, name + '.cube'), lines.join('\n') + '\n');
}
console.log('wrote', Object.keys(GRADES).join(', '));
