// Decal sprites as PNGs (our own maths + node:zlib): a bullet hole and a scorch mark, RGBA, alpha = coverage.
// Usage: node tools/make-decals.mjs   (writes textures/hole.png and textures/scorch.png)
import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const out = join(dirname(fileURLToPath(import.meta.url)), '..', 'textures');
mkdirSync(out, { recursive: true });

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
let seed = 12345;
const rnd = () => ((seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296);
const hash = (x, y) => { let h = Math.imul(x, 374761393) + Math.imul(y, 668265263); h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };
function noise(x, y, p) { // value noise, period p
  const xi = Math.floor(x), yi = Math.floor(y), fx = x - xi, fy = y - yi;
  const s = (t) => t * t * (3 - 2 * t);
  const a = hash(xi % p, yi % p), b = hash((xi + 1) % p, yi % p), c = hash(xi % p, (yi + 1) % p), d = hash((xi + 1) % p, (yi + 1) % p);
  return a + (b - a) * s(fx) + (c - a) * s(fy) + (a - b - c + d) * s(fx) * s(fy);
}
function fbm(x, y) { return 0.5 * noise(x * 4, y * 4, 4) + 0.3 * noise(x * 8, y * 8, 8) + 0.2 * noise(x * 16, y * 16, 16); }

function hole(N) {
  const px = Buffer.alloc(N * N * 4);
  const crackAngles = Array.from({ length: 7 }, () => rnd() * Math.PI * 2);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = (x + 0.5) / N * 2 - 1, v = (y + 0.5) / N * 2 - 1;
    const r = Math.hypot(u, v), a = Math.atan2(v, u);
    const rr = r * (1 + (fbm(u * 0.5 + 0.5, v * 0.5 + 0.5) - 0.5) * 0.5);
    let col = 0.0, al = 0;
    if (rr < 0.16) { col = 0.0; al = 1; }                                // the hole
    else if (rr < 0.3) { col = 0.55; al = 1 - (rr - 0.16) / 0.14 * 0.0; al *= 0.95; } // chipped, pale rim
    else if (rr < 0.95) { col = 0.08; al = Math.max(0, 1 - (rr - 0.3) / 0.65) * 0.7; } // soot halo
    for (const ca of crackAngles) { // hairline cracks
      let d = Math.abs(Math.atan2(Math.sin(a - ca), Math.cos(a - ca)));
      if (d < 0.05 && r > 0.2 && r < 0.9 - ca % 0.3) { col = Math.min(col, 0.03); al = Math.max(al, 0.9 * (1 - r)); }
    }
    const i = (y * N + x) * 4;
    const c = Math.round(Math.min(1, Math.max(0, col)) * 255);
    px[i] = c; px[i + 1] = Math.round(c * 0.96); px[i + 2] = Math.round(c * 0.92); px[i + 3] = Math.round(Math.min(1, al) * 255);
  }
  return png(N, N, px);
}
function scorch(N) {
  const px = Buffer.alloc(N * N * 4);
  const rays = Array.from({ length: 18 }, () => [rnd() * Math.PI * 2, 0.5 + rnd() * 0.45, 0.05 + rnd() * 0.12]);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = (x + 0.5) / N * 2 - 1, v = (y + 0.5) / N * 2 - 1;
    const r = Math.hypot(u, v), a = Math.atan2(v, u);
    const n = fbm(u * 0.5 + 0.5, v * 0.5 + 0.5);
    let al = Math.max(0, 1 - r / (0.55 + 0.3 * n)); al = al * al * (3 - 2 * al);
    for (const [ra, len, w] of rays) {
      const d = Math.abs(Math.atan2(Math.sin(a - ra), Math.cos(a - ra)));
      if (d < w && r < len) al = Math.max(al, (1 - d / w) * (1 - r / len) * 0.85 * (0.5 + n));
    }
    const core = Math.max(0, 1 - r / 0.28);
    const i = (y * N + x) * 4;
    const c = Math.round((0.03 + 0.1 * n * (1 - core)) * 255);
    px[i] = c; px[i + 1] = Math.round(c * 0.9); px[i + 2] = Math.round(c * 0.8); px[i + 3] = Math.round(Math.min(1, al) * 235);
  }
  return png(N, N, px);
}
writeFileSync(join(out, 'hole.png'), hole(128));
writeFileSync(join(out, 'scorch.png'), scorch(256));
console.log('wrote textures/hole.png and textures/scorch.png');
