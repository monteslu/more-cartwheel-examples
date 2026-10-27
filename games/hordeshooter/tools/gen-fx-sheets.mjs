// Writes the heat-haze sheet (PNG, RGBA), generated here (the fire and smoke flipbooks with their motion
// vectors and scorch.png come from the engine's tools/fx/bake-flipbooks.mjs):
//   haze.png         one swirl: red and green are the bend direction (0.5 = none), alpha the weight (for DISTORT)
//   node tools/gen-fx-sheets.mjs assets/fx
import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { resolve } from 'node:path';

const outDir = resolve(process.argv[2] || 'assets/fx');
mkdirSync(outDir, { recursive: true });

function crc32(buf) {
  let c, crc = 0xffffffff;
  for (let n = 0; n < buf.length; n++) {
    c = (crc ^ buf[n]) & 0xff;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    crc = (crc >>> 8) ^ c;
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function png(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}
// value noise on an integer hash, fbm and a domain warp
function h(x, y, s) { let a = (x * 374761393 + y * 668265263 + s * 2147483647) | 0; a = Math.imul(a ^ (a >>> 13), 1274126177); return ((a ^ (a >>> 16)) >>> 0) / 4294967296; }
function vn(x, y, s) {
  const x0 = Math.floor(x), y0 = Math.floor(y), tx = x - x0, ty = y - y0;
  const sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
  const a = h(x0, y0, s), b = h(x0 + 1, y0, s), c = h(x0, y0 + 1, s), d = h(x0 + 1, y0 + 1, s);
  return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}
function fbm(x, y, s, oct = 5) { let n = 0, a = 0.5, nn = 0; for (let o = 0; o < oct; o++) { n += a * vn(x, y, s + o * 17); nn += a; a *= 0.5; x *= 2; y *= 2; } return n / nn; }
const sm = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };

{
  const N = 128, buf = Buffer.alloc(N * N * 4);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = x / N, v = y / N, e = 0.01;
    const f = (a, b) => fbm(a * 4, b * 4, 5, 4);
    const gx = (f(u + e, v) - f(u - e, v)) / (2 * e), gy = (f(u, v + e) - f(u, v - e)) / (2 * e);
    const k = Math.max(-1, Math.min(1, 0.06));
    const r = Math.hypot(u - 0.5, v - 0.5) * 2, fall = sm(1, 0.25, r);
    const i = (y * N + x) * 4;
    buf[i] = Math.round((0.5 + Math.max(-0.5, Math.min(0.5, gx * 0.05))) * 255);
    buf[i + 1] = Math.round((0.5 + Math.max(-0.5, Math.min(0.5, gy * 0.05))) * 255);
    buf[i + 2] = 128; buf[i + 3] = Math.round(fall * 255 * (k > 0 ? 1 : 1));
  }
  writeFileSync(resolve(outDir, 'haze.png'), png(N, N, buf));
  console.log('wrote haze.png');
}
