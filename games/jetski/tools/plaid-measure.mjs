// Is a cliff plaid? A periodic texture correlates with itself at its period. In windows across a crop, with the local mean removed, this finds each
// window's strongest correlation at any offset past the rock's own grain, and reports their median (0: none, 1: a perfect repeat).
//   node games/jetski/tools/plaid-measure.mjs <png> <x> <y> <w> <h>
import { readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
function png(path) {
  const b = readFileSync(path), w = b.readUInt32BE(16), h = b.readUInt32BE(20), ch = b[25] === 6 ? 4 : 3, parts = [];
  for (let o = 8; o < b.length;) { const n = b.readUInt32BE(o); if (b.toString('latin1', o + 4, o + 8) === 'IDAT') parts.push(b.subarray(o + 8, o + 8 + n)); o += 12 + n; }
  const raw = inflateSync(Buffer.concat(parts)), out = Buffer.alloc(w * h * ch), s = w * ch;
  for (let y = 0; y < h; y++) {
    const f = raw[y * (s + 1)], r = y * (s + 1) + 1, d = y * s;
    for (let x = 0; x < s; x++) {
      const a = x >= ch ? out[d + x - ch] : 0, u = y ? out[d + x - s] : 0, c = y && x >= ch ? out[d + x - s - ch] : 0;
      let p = 0;
      if (f === 1) p = a; else if (f === 2) p = u; else if (f === 3) p = (a + u) >> 1;
      else if (f === 4) { const q = a + u - c, pa = Math.abs(q - a), pb = Math.abs(q - u), pc = Math.abs(q - c); p = pa <= pb && pa <= pc ? a : pb <= pc ? u : c; }
      out[d + x] = (raw[r + x] + p) & 255;
    }
  }
  return { w, h, ch, out };
}
export function plaid(path, X, Y, W, H, WIN = 220) {
  const { w, ch, out } = png(path), g = new Float32Array(W * H);
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) { const k = ((j + Y) * w + i + X) * ch; g[j * W + i] = out[k] * 0.3 + out[k + 1] * 0.6 + out[k + 2] * 0.1; }
  const R = 12, I = new Float64Array((W + 1) * (H + 1));
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) I[(j + 1) * (W + 1) + i + 1] = g[j * W + i] + I[j * (W + 1) + i + 1] + I[(j + 1) * (W + 1) + i] - I[j * (W + 1) + i];
  const f = new Float32Array(W * H);
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
    const a = Math.max(0, i - R), b = Math.min(W, i + R + 1), c = Math.max(0, j - R), d = Math.min(H, j + R + 1);
    f[j * W + i] = g[j * W + i] - (I[d * (W + 1) + b] - I[c * (W + 1) + b] - I[d * (W + 1) + a] + I[c * (W + 1) + a]) / ((b - a) * (d - c));
  }
  const peaks = [];
  for (let y0 = 0; y0 + WIN * 1.5 <= H; y0 += WIN) for (let x0 = 0; x0 + WIN * 1.5 <= W; x0 += WIN) {
    let best = 0;
    for (let dy = 0; dy < WIN / 2; dy++) for (let dx = -WIN / 2 | 0; dx < WIN / 2; dx++) {
      if (Math.hypot(dx, dy) < 24) continue;
      let sab = 0, saa = 0, sbb = 0;
      for (let j = 0; j < WIN; j += 2) for (let i = 0; i < WIN; i += 2) {
        const yy = y0 + j + dy, xx = x0 + i + dx;
        if (xx < 0 || xx >= W || yy >= H) continue;
        const A = f[(y0 + j) * W + x0 + i], B = f[yy * W + xx];
        sab += A * B, saa += A * A, sbb += B * B;
      }
      const r = sab / Math.sqrt(saa * sbb + 1e-6);
      if (r > best) best = r;
    }
    peaks.push(best);
  }
  peaks.sort((a, b) => a - b);
  return { r: peaks[peaks.length >> 1], n: peaks.length };
}
if (process.argv[1].endsWith('plaid-measure.mjs')) { const [p, x, y, w, h] = process.argv.slice(2); const m = plaid(p, +x, +y, +w, +h); console.log(`repeat ${m.r.toFixed(3)} over ${m.n} windows`); }
