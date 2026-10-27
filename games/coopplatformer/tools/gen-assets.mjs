// Generates the game's image assets that the engine reads as files (it has no in-memory asset path for these):
//   assets/decals.png      a 4 x 2 sheet of 256 px decals (dirt, scorch, moss, cracks, damp streaks, leaf litter,
//                          soot, crazed ground), white where a MULTIPLY decal leaves the picture alone
//   assets/grade/<region>.cube  one colour grade per region (a 17^3 3D LUT on display values)
// Everything is computed here from value noise and curves: no image is taken from anywhere.
//   node games/coopplatformer/tools/gen-assets.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ASSETS = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets');

// ── PNG writer (RGBA8) ──
const crcTable = new Uint32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
/** @param {Buffer} b */
const crc32 = (b) => {
  let c = 0xffffffff;
  for (const x of b) c = crcTable[(c ^ x) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
};
/** @param {string} type @param {Buffer} data */
const chunk = (type, data) => {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
};
/** @param {number} w @param {number} h @param {Uint8Array} rgba */
function png(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    Buffer.from(rgba.buffer, y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0), ihdr.writeUInt32BE(h, 4), (ihdr[8] = 8), (ihdr[9] = 6), (ihdr[10] = 0), (ihdr[11] = 0), (ihdr[12] = 0);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

// ── value noise ──
/** @param {number} x @param {number} y @param {number} s */
const hash = (x, y, s) => {
  let h = (Math.imul(x, 374761393) + Math.imul(y, 668265263) + Math.imul(s, 2147483647)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
};
const sm = (t) => t * t * (3 - 2 * t);
/** @param {number} u @param {number} v @param {number} s */
const vnoise = (u, v, s) => {
  const x = Math.floor(u), y = Math.floor(v), fx = sm(u - x), fy = sm(v - y);
  const a = hash(x, y, s), b = hash(x + 1, y, s), c = hash(x, y + 1, s), d = hash(x + 1, y + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
};
/** @param {number} u @param {number} v @param {number} oct @param {number} s */
const fbm = (u, v, oct, s) => {
  let t = 0, a = 0.5, f = 1, n = 0;
  for (let i = 0; i < oct; i++) (t += a * vnoise(u * f, v * f, s + i * 17)), (n += a), (a *= 0.5), (f *= 2);
  return t / n;
};
const sstep = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const clamp = (x) => Math.min(1, Math.max(0, x));

// ── the decal sheet ──
const CELL = 256, COLS = 4, ROWS = 2, W = CELL * COLS, H = CELL * ROWS;
const sheet = new Uint8Array(W * H * 4);
/** each cell: (u, v in 0..1, centred) -> [r, g, b, a] in 0..1; MULTIPLY decals darken with rgb below 1 */
const cells = [
  // 0 dirt splat: brown, a ragged blob with spatter
  (u, v) => {
    const d = Math.hypot(u - 0.5, v - 0.5) * 2, n = fbm(u * 6, v * 6, 5, 11);
    const blob = 1 - sstep(0.45, 0.95, d + (n - 0.5) * 0.7);
    const spat = sstep(0.82, 0.9, fbm(u * 22, v * 22, 2, 12)) * (1 - sstep(0.6, 1.0, d));
    const a = clamp(blob * (0.55 + 0.45 * fbm(u * 14, v * 14, 4, 13)) + spat * 0.6);
    return [0.62, 0.5, 0.4, a];
  },
  // 1 scorch: dark core, soot rays
  (u, v) => {
    const dx = u - 0.5, dy = v - 0.5, d = Math.hypot(dx, dy) * 2, ang = Math.atan2(dy, dx);
    const rays = 0.5 + 0.5 * Math.sin(ang * 9 + fbm(u * 4, v * 4, 3, 21) * 6);
    const a = clamp((1 - sstep(0.2, 1.0, d - rays * 0.18)) * (0.7 + 0.3 * fbm(u * 10, v * 10, 4, 22)));
    return [0.18, 0.15, 0.13, a];
  },
  // 2 moss: green patches that hang in clumps
  (u, v) => {
    const d = Math.hypot((u - 0.5) * 1.1, v - 0.4) * 2, n = fbm(u * 8, v * 8, 5, 31);
    const a = clamp(sstep(0.42, 0.62, n) * (1 - sstep(0.55, 1.0, d)) + sstep(0.7, 0.75, fbm(u * 30, v * 30, 2, 32)) * (1 - sstep(0.4, 0.9, d)) * 0.5);
    return [0.55, 0.78, 0.35, a];
  },
  // 3 cracks: thin dark branching lines
  (u, v) => {
    const n1 = Math.abs(fbm(u * 3.5, v * 3.5, 5, 41) - 0.5), n2 = Math.abs(fbm(u * 7, v * 7, 4, 42) - 0.5);
    const line = 1 - sstep(0.0, 0.02, n1), fine = (1 - sstep(0.0, 0.012, n2)) * sstep(0.45, 0.6, fbm(u * 3, v * 3, 3, 43));
    const fade = 1 - sstep(0.5, 1.0, Math.hypot(u - 0.5, v - 0.5) * 2);
    return [0.25, 0.22, 0.2, clamp((line + fine * 0.8) * fade)];
  },
  // 4 damp streaks: vertical runs from the top edge
  (u, v) => {
    const col = fbm(u * 9, 0.5, 4, 51), len = 0.35 + 0.6 * fbm(u * 5, 1.5, 3, 52);
    const run = sstep(0.45, 0.7, col) * (1 - sstep(len - 0.25, len, v));
    const edge = 1 - sstep(0.4, 0.5, Math.abs(u - 0.5));
    return [0.55, 0.58, 0.6, clamp(run * edge * (0.6 + 0.4 * fbm(u * 20, v * 6, 3, 53)))];
  },
  // 5 leaf litter: small leaf shapes, warm
  (u, v) => {
    let a = 0;
    for (let i = 0; i < 26; i++) {
      const cx = 0.15 + 0.7 * hash(i, 1, 61), cy = 0.15 + 0.7 * hash(i, 2, 61), r = 0.025 + 0.03 * hash(i, 3, 61), th = 6.28 * hash(i, 4, 61);
      const x = (u - cx) * Math.cos(th) + (v - cy) * Math.sin(th), y = -(u - cx) * Math.sin(th) + (v - cy) * Math.cos(th);
      a = Math.max(a, 1 - sstep(0.7, 1.0, Math.hypot(x / (r * 1.8), y / r)));
    }
    return [0.7, 0.55, 0.32, a * 0.85];
  },
  // 6 soot: rises from the bottom edge and spreads, for walls over lava and torches
  (u, v) => {
    const w = 0.12 + 0.35 * (1 - v), n = fbm(u * 5, v * 3, 5, 71);
    const a = clamp((1 - sstep(w * 0.4, w, Math.abs(u - 0.5) + (n - 0.5) * 0.2)) * sstep(0.0, 0.25, v) * (0.5 + 0.5 * n));
    return [0.2, 0.17, 0.16, a];
  },
  // 7 crazed ground: a dry-mud cell pattern
  (u, v) => {
    let best = 9, second = 9;
    for (let i = 0; i < 40; i++) {
      const d = Math.hypot(u - hash(i, 7, 81), v - hash(i, 8, 81));
      if (d < best) (second = best), (best = d);
      else if (d < second) second = d;
    }
    const edge = 1 - sstep(0.0, 0.012, second - best);
    const fade = 1 - sstep(0.55, 1.0, Math.hypot(u - 0.5, v - 0.5) * 2);
    return [0.4, 0.33, 0.28, clamp(edge * fade * 0.9)];
  },
];
for (let c = 0; c < COLS * ROWS; c++) {
  const ox = (c % COLS) * CELL, oy = Math.floor(c / COLS) * CELL;
  for (let y = 0; y < CELL; y++)
    for (let x = 0; x < CELL; x++) {
      const u = (x + 0.5) / CELL, v = (y + 0.5) / CELL;
      const [r, g, b, a] = cells[c](u, v);
      // MULTIPLY: the colour is lerped toward white by alpha so a soft edge fades to "no change"
      const i = ((oy + y) * W + ox + x) * 4;
      sheet[i] = Math.round(255 * (1 - a + a * r)), sheet[i + 1] = Math.round(255 * (1 - a + a * g)), sheet[i + 2] = Math.round(255 * (1 - a + a * b));
      sheet[i + 3] = Math.round(255 * a);
    }
}
writeFileSync(join(ASSETS, 'decals.png'), png(W, H, sheet));

// ── per-region grades ──
/**
 * @param {object} g contrast (1 = none), sat, black point, shadow tint and highlight tint (added), gamma
 * @returns {string} a .cube file
 */
function cube(name, g) {
  const N = 17;
  const lines = [`TITLE "${name}"`, `LUT_3D_SIZE ${N}`];
  const s = (x) => (x < 0.5 ? 0.5 * Math.pow(2 * x, g.contrast) : 1 - 0.5 * Math.pow(2 * (1 - x), g.contrast));
  for (let bi = 0; bi < N; bi++)
    for (let gi = 0; gi < N; gi++)
      for (let ri = 0; ri < N; ri++) {
        let c = [ri / (N - 1), gi / (N - 1), bi / (N - 1)];
        c = c.map((x) => Math.max(0, (x - g.black) / (1 - g.black)));
        c = c.map((x) => Math.pow(x, g.gamma));
        c = c.map(s);
        const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
        c = c.map((x) => l + (x - l) * g.sat);
        const sh = (1 - l) * (1 - l), hi = l * l;
        c = c.map((x, k) => clamp(x + g.shadow[k] * sh + g.high[k] * hi));
        lines.push(c.map((x) => x.toFixed(5)).join(' '));
      }
  return lines.join('\n') + '\n';
}
mkdirSync(join(ASSETS, 'grade'), { recursive: true });
const GRADES = {
  // warm afternoon: golden highlights, teal shade, firm contrast, richer greens
  meadow: { contrast: 1.22, sat: 1.18, black: 0.03, gamma: 1.04, shadow: [-0.02, 0.01, 0.04], high: [0.05, 0.025, -0.03] },
  // caves: cold blue shadows, warm torch highlights, deep blacks
  caves: { contrast: 1.3, sat: 1.15, black: 0.045, gamma: 1.06, shadow: [-0.01, 0.015, 0.06], high: [0.06, 0.02, -0.03] },
  // ruins: green-gold daylight, clear water greens, no milk in the shadows
  ruins: { contrast: 1.25, sat: 1.12, black: 0.05, gamma: 1.08, shadow: [-0.015, 0.025, 0.03], high: [0.04, 0.035, -0.02] },
  // volcano: hot orange highlights, red-black shadows, hard contrast
  volcano: { contrast: 1.35, sat: 1.2, black: 0.05, gamma: 1.05, shadow: [0.03, -0.01, -0.01], high: [0.07, 0.01, -0.05] },
};
for (const [k, g] of Object.entries(GRADES)) writeFileSync(join(ASSETS, 'grade', `${k}.cube`), cube(k, g));
console.log('wrote assets/decals.png and assets/grade/*.cube');
