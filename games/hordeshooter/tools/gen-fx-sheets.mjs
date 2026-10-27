// Writes the effects' flipbook sheets (PNG, RGBA), all generated here:
//   fire_sheet.png   4x4 frames of a flame puff burning out (white = hot; tinted by the particle colour ramp)
//   smoke_sheet.png  4x4 frames of a smoke billow thinning as it grows (lit from the upper left)
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

const F = 128, G = 4;
function sheet(name, cell) {
  const W = F * G, buf = Buffer.alloc(W * W * 4);
  for (let f = 0; f < G * G; f++) {
    const t = f / (G * G - 1), ox = (f % G) * F, oy = Math.floor(f / G) * F;
    for (let y = 0; y < F; y++) for (let x = 0; x < F; x++) {
      const [r, g, b, a] = cell(x / F, y / F, t);
      const i = ((oy + y) * W + ox + x) * 4;
      buf[i] = Math.round(r * 255), buf[i + 1] = Math.round(g * 255), buf[i + 2] = Math.round(b * 255), buf[i + 3] = Math.round(a * 255);
    }
  }
  writeFileSync(resolve(outDir, name), png(W, W, buf));
  console.log('wrote', name);
}
function wx(u, v, t, s) { // warped coordinates, drifting up as the frame advances
  const w1 = fbm(u * 3 + 3, v * 3 - t * 1.2, s + 1) - 0.5, w2 = fbm(u * 3 - 7, v * 3 + 5, s + 2) - 0.5;
  return [u * 3.2 + w1 * 1.8, v * 3.2 - t * 2.0 + w2 * 1.8];
}
sheet('fire_sheet.png', (u, v, t) => {
  const dx = u - 0.5, dy = v - 0.58, r = Math.hypot(dx * 1.05, dy * 0.9) * 2;
  const [x, y] = wx(u, v, t, 31);
  const n = fbm(x, y, 40, 5);
  const body = sm(1.0, 0.05, r) * (1 - 0.55 * t);
  const d = Math.max(0, body * (0.55 + 0.9 * n) - (0.12 + 0.55 * t) * (1 - n));
  const heat = Math.min(1, d * 2.2 * (1 - 0.6 * t));
  return [heat, heat, heat, Math.min(1, d * 1.7)];
});
sheet('smoke_sheet.png', (u, v, t) => {
  const dx = u - 0.5, dy = v - 0.5, r = Math.hypot(dx, dy) * 2;
  const [x, y] = wx(u * 1.1, v * 1.1, t * 0.4, 77);
  const n = fbm(x, y, 90, 6), n2 = fbm(x * 2.3 + 5, y * 2.3, 120, 3);
  const body = sm(1.05, 0.0, r + (n - 0.5) * 0.7);          // lumpy outline
  const d = sm(0.18 + 0.4 * t, 0.62 + 0.25 * t, body * 0.9 + (n2 - 0.5) * 0.6 * body);
  const slope = (fbm(x + 0.04, y, 90, 4) - fbm(x - 0.04, y, 90, 4)) + (fbm(x, y + 0.04, 90, 4) - fbm(x, y - 0.04, 90, 4));
  const lit = Math.min(1, Math.max(0.25, 0.62 - slope * 3.2 - (dx + dy) * 0.35 + 0.15 * n2));
  return [lit, lit, lit, Math.min(1, d) * (1 - 0.5 * t)];
});
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
