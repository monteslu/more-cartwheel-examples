// How white and how light is the band of water under the horizon? Two frames of the same moment: crest_light off, crest_light on.
//   node games/jetski/tools/crest-measure.mjs <off.png> <on.png>   (near-white count and mean level in rows 340 to 520 of the sea)
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
for (const p of process.argv.slice(2)) {
  const { w, ch, out } = png(p);
  let white = 0, n = 0, g = 0;
  for (let y = 340; y < 520; y++) for (let x = 0; x < w; x++) {
    const i = (y * w + x) * ch, r = out[i], gg = out[i + 1], b = out[i + 2];
    if (gg < r) continue; // sky, land: the sea is green or blue over red
    n++, g += gg;
    if (r > 190 && gg > 200 && b > 200) white++;
  }
  console.log(p.split('/').slice(-2).join('/'), 'sea pixels', n, 'mean green', (g / Math.max(n, 1)).toFixed(1), 'near white', white);
}
