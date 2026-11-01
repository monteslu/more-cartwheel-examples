// Does a crop of cliff repeat? Not by correlating the whole crop with itself: on a wall in perspective that barely told a plaid wall from a plain one.
//   node games/jetski/tools/cliff-measure.mjs <png> <x> <y> <w> <h>
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
/** A tile shows as the same rock again and again. @returns {{share:number, patches:number}} of small patches across the crop, the share that have a near copy of themselves elsewhere in it */
export function repeatShare(path, X, Y, W, H, P = 48, NCC = 0.62) {
  const { w, ch, out } = png(path), g = new Float32Array(W * H);
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) { const k = ((j + Y) * w + i + X) * ch; g[j * W + i] = out[k] * 0.3 + out[k + 1] * 0.6 + out[k + 2] * 0.1; }
  const stat = (x, y) => { let m = 0; for (let j = 0; j < P; j += 2) for (let i = 0; i < P; i += 2) m += g[(y + j) * W + x + i]; m /= (P * P) / 4; let v = 0; for (let j = 0; j < P; j += 2) for (let i = 0; i < P; i += 2) { const d = g[(y + j) * W + x + i] - m; v += d * d; } return { m, v }; };
  let total = 0, hit = 0;
  for (let y = 0; y + P <= H; y += P) for (let x = 0; x + P <= W; x += P) {
    const A = stat(x, y);
    if (A.v / ((P * P) / 4) < 30) continue; // flat dark rock says nothing
    total++;
    let found = false;
    for (let v = 0; v + P <= H && !found; v += 4) for (let u = 0; u + P <= W && !found; u += 4) {
      if (Math.hypot(u - x, v - y) < P * 1.2) continue;
      const B = stat(u, v);
      let sab = 0;
      for (let j = 0; j < P; j += 2) for (let i = 0; i < P; i += 2) sab += (g[(y + j) * W + x + i] - A.m) * (g[(v + j) * W + u + i] - B.m);
      if (sab / Math.sqrt(A.v * B.v + 1e-6) > NCC) found = true;
    }
    if (found) hit++;
  }
  return { share: total ? hit / total : 0, patches: total };
}
if (process.argv[1]?.endsWith('cliff-measure.mjs') && process.argv[7] !== 'grid') { const [p, x, y, w, h] = process.argv.slice(2); console.log(JSON.stringify(repeatShare(p, +x, +y, +w, +h))); }
/** A wall seen from the line: a tile shows as a grid, so brightness averaged down each column and along each row beats regularly. @returns {{score:number, lag:number}} the strongest regular beat, 0 none, 1 perfect */
export function gridScore(path, X, Y, W, H) {
  const { w, ch, out } = png(path), col = new Float64Array(W), row = new Float64Array(H);
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) { const k = ((j + Y) * w + i + X) * ch, v = out[k] * 0.3 + out[k + 1] * 0.6 + out[k + 2] * 0.1; col[i] += v / H; row[j] += v / W; }
  const beat = (p) => {
    const n = p.length, R = 24, f = new Float64Array(n);
    for (let i = 0; i < n; i++) { let m = 0, c = 0; for (let k = Math.max(0, i - R); k <= Math.min(n - 1, i + R); k++) { m += p[k]; c++; } f[i] = p[i] - m / c; }
    const acf = (L) => { let a = 0, b = 0, c = 0; for (let i = 0; i + L < n; i++) { a += f[i] * f[i + L]; b += f[i] * f[i]; c += f[i + L] * f[i + L]; } return a / Math.sqrt(b * c + 1e-9); };
    let best = 0, lag = 0;
    for (let L = 18; L < n / 2.2; L++) { const r = acf(L); if (r > best && r > acf(L - 3) && r > acf(L + 3)) best = r, lag = L; }
    return { best, lag };
  };
  const a = beat(col), b = beat(row);
  return a.best > b.best ? { score: a.best, lag: a.lag } : { score: b.best, lag: b.lag };
}
if (process.argv[1]?.endsWith('cliff-measure.mjs') && process.argv[7] === 'grid') { const [p, x, y, w, h] = process.argv.slice(2); console.log(JSON.stringify(gridScore(p, +x, +y, +w, +h))); }
