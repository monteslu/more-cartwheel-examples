// The descent's floor textures (LEVELS.md): one tiling 1024 x 1024 albedo per level, its relief (joints,
// cracks, ridges) painted into the colour as the yard's floor is, because the floor draws baked and unlit.
// All generated here from integer hashes (periodic, so the sheet tiles seamlessly).
//   node tools/gen-level-textures.mjs [out dir (default assets/textures)] [level ...]
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { deflateSync } from 'node:zlib';

const N = 1024;
const args = process.argv.slice(2);
const out = resolve(args[0] && !/^[a-z]+$/.test(args[0]) ? args.shift() : 'assets/textures');
const only = args;

// ── periodic noise ──────────────────────────────────────────────────
function hash(x, y, s) {
  let h = (Math.imul(x | 0, 374761393) + Math.imul(y | 0, 668265263) + Math.imul(s | 0, 2246822519)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
const mod = (a, p) => ((a % p) + p) % p;
function vnoise(x, y, p, s) { // period p cells
  const ix = Math.floor(x), iy = Math.floor(y);
  let fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  const h = (a, b) => hash(mod(a, p), mod(b, p), s);
  const a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
  return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
function fbm(u, v, base, oct, s) { // u, v in 0..1 over the tile
  let t = 0, a = 0.5, f = base, n = 0;
  for (let o = 0; o < oct; o++) { t += a * vnoise(u * f, v * f, f, s + o * 31); n += a; a *= 0.5; f *= 2; }
  return t / n;
}
// periodic Voronoi: distance to the nearest and second nearest seed, the nearest seed's id
function voronoi(u, v, cells, s, jitter = 0.85) {
  const x = u * cells, y = v * cells, ix = Math.floor(x), iy = Math.floor(y);
  let d1 = 9, d2 = 9, id = 0;
  for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
    const cx = ix + i, cy = iy + j;
    const px = cx + 0.5 + (hash(mod(cx, cells), mod(cy, cells), s) - 0.5) * jitter;
    const py = cy + 0.5 + (hash(mod(cx, cells), mod(cy, cells), s + 7) - 0.5) * jitter;
    const d = Math.hypot(x - px, y - py);
    if (d < d1) { d2 = d1; d1 = d; id = mod(cx, cells) * 977 + mod(cy, cells); } else if (d < d2) d2 = d;
  }
  return { d1, d2, edge: d2 - d1, id };
}
const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const mix = (a, b, t) => a + (b - a) * t;
const mix3 = (a, b, t) => [mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)];
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };

// ── the floors (each returns sRGB 0..1 for u, v in the tile) ─────────
const FLOORS = {
  // concrete pavers in a drain junction: 1 m slabs with silted joints, wet dark patches, algae, rust stains
  sluice(u, v) {
    const tiles = 8; // the tile is 8 m: 1 m slabs
    const x = u * tiles, y = v * tiles;
    const row = Math.floor(y), off = row % 2 ? 0.5 : 0;
    const sx = Math.floor(x + off), fx = x + off - sx, fy = y - row;
    const sid = hash(mod(sx, tiles), row, 3);
    const joint = Math.min(fx, 1 - fx, fy, 1 - fy);
    const groove = 1 - smooth(0.0, 0.03, joint);
    const base = 0.42 + 0.08 * (sid - 0.5) + 0.12 * (fbm(u, v, 16, 4, 11) - 0.5);
    let c = [base * 0.93, base * 0.96, base * 0.92];
    const wet = smooth(0.5, 0.68, fbm(u, v, 4, 4, 21));
    c = mix3(c, [c[0] * 0.72, c[1] * 0.76, c[2] * 0.76], wet);
    const algae = smooth(0.58, 0.75, fbm(u, v, 6, 5, 33)) * (0.5 + groove * 0.5);
    c = mix3(c, [0.16, 0.22, 0.12], algae * 0.75);
    const rust = smooth(0.7, 0.85, fbm(u, v, 10, 3, 41));
    c = mix3(c, [0.3, 0.21, 0.15], rust * 0.3);
    // the relief, lit from the north-west (the grating's light): joints dark, slab edges catching
    const lit = fx < 0.04 || fy < 0.04 ? 1.12 : fx > 0.96 || fy > 0.96 ? 0.8 : 1;
    c = c.map((k) => k * (1 - 0.65 * groove) * lit);
    const crack = smooth(0.012, 0.0, voronoi(u, v, 18, 51, 1).edge) * smooth(0.55, 0.7, fbm(u, v, 8, 3, 55));
    return c.map((k) => k * (1 - 0.55 * crack));
  },
  // irregular flagstones under bone dust: warm grey and ochre stones, dark mortar, cracks, scattered bone
  // chips, soot near nothing in particular
  ossuary(u, v) {
    const vo = voronoi(u, v, 9, 61, 0.9);
    const sid = (vo.id * 2654435761 >>> 0) / 4294967296;
    const mortar = 1 - smooth(0.0, 0.06, vo.edge);
    const tone = 0.36 + 0.1 * (sid - 0.5) + 0.14 * (fbm(u, v, 12, 5, 71) - 0.5);
    let c = mix3([tone * 1.02, tone * 0.95, tone * 0.84], [tone * 1.1, tone * 0.92, tone * 0.66], hash(vo.id, 1, 73) * 0.6);
    const worn = smooth(0.35, 0.05, vo.d1) * 0.12; // stone centres worn pale by feet
    c = c.map((k) => k * (1 + worn));
    const dust = smooth(0.55, 0.72, fbm(u, v, 5, 5, 81));
    c = mix3(c, [0.62, 0.57, 0.46], dust * 0.55);
    const chips = hash(Math.floor(u * 700), Math.floor(v * 700), 85) > 0.985 && dust > 0.2 ? 1 : 0;
    c = mix3(c, [0.8, 0.75, 0.62], chips * 0.7);
    const soot = smooth(0.62, 0.8, fbm(u, v, 3, 4, 91));
    c = mix3(c, [0.08, 0.07, 0.06], soot * 0.5);
    const crack = smooth(0.01, 0.0, voronoi(u, v, 30, 95, 1).edge) * smooth(0.5, 0.65, fbm(u, v, 7, 3, 97));
    // the relief: mortar sunk and dark, each stone's north-west edge lit by the candles' general direction
    c = c.map((k) => k * (1 - 0.7 * mortar) * (1 - 0.5 * crack));
    return c;
  },
  // a forge floor: big riveted iron plates, black cinder in the seams, soot and rust, worn bright where
  // things are dragged, scorched rings
  foundry(u, v) {
    const tiles = 4; // 2 m plates on the 8 m tile
    const x = u * tiles, y = v * tiles;
    const px = Math.floor(x), py = Math.floor(y), fx = x - px, fy = y - py;
    const pid = hash(mod(px, tiles), mod(py, tiles), 5);
    const seam = 1 - smooth(0.0, 0.025, Math.min(fx, 1 - fx, fy, 1 - fy));
    const tone = 0.33 + 0.06 * (pid - 0.5) + 0.08 * (fbm(u, v, 16, 4, 13) - 0.5);
    let c = [tone * 1.0, tone * 0.97, tone * 0.95];
    const rust = smooth(0.55, 0.72, fbm(u, v, 6, 5, 17));
    c = mix3(c, [0.3, 0.16, 0.09], rust * 0.6);
    const worn = smooth(0.62, 0.78, fbm(u, v, 3, 4, 19));
    c = mix3(c, [0.38, 0.37, 0.36], worn * 0.5);
    const soot = smooth(0.5, 0.75, fbm(u, v, 5, 4, 23));
    c = mix3(c, [0.1, 0.09, 0.08], soot * 0.35);
    // rivets along each plate's edge
    const rx = Math.min(fx, 1 - fx), ry = Math.min(fy, 1 - fy);
    const along = (t) => Math.abs((t * 8) % 1 - 0.5) < 0.12;
    const rivet = (rx > 0.04 && rx < 0.07 && along(fy)) || (ry > 0.04 && ry < 0.07 && along(fx)) ? 1 : 0;
    c = c.map((k) => k * (1 - 0.75 * seam) * (1 + 0.5 * rivet));
    const scorch = smooth(0.6, 0.8, fbm(u + 0.37, v + 0.11, 4, 6, 29)) * (0.6 + 0.4 * fbm(u, v, 24, 3, 31));
    return c.map((k) => k * (1 - 0.3 * scorch));
  },
  // a cave floor: dark violet-black loam, moss mats, pale root threads, small bright specks of fungus
  grotto(u, v) {
    const n = fbm(u, v, 6, 6, 101);
    let c = [0.24 + 0.08 * n, 0.2 + 0.06 * n, 0.27 + 0.09 * n];
    const moss = smooth(0.52, 0.68, fbm(u, v, 5, 5, 103));
    c = mix3(c, [0.16, 0.26, 0.18], moss * 0.8);
    const stones = voronoi(u, v, 40, 107, 1); // small pebbles, each its own shade, sharp edged
    const stone = (stones.d1 < 0.22 ? 1 : 0) * smooth(0.7, 0.8, hash(stones.id, 5, 109));
    c = mix3(c, [0.2 + 0.08 * hash(stones.id, 6, 110), 0.18, 0.22], stone * 0.85);
    const roots = smooth(0.012, 0.0, voronoi(u, v, 10, 113, 1).edge) * smooth(0.45, 0.6, fbm(u, v, 8, 3, 115));
    c = mix3(c, [0.42, 0.38, 0.34], roots * 0.7);
    const speck = hash(Math.floor(u * 512), Math.floor(v * 512), 117) > 0.993 && moss > 0.3 ? 1 : 0;
    c = mix3(c, [0.3, 0.75, 0.68], speck * 0.8);
    return c;
  },
  // a cathedral floor: large black and grey marble slabs laid on the diagonal, veined, worn and cracked, a
  // crimson cast in the dust
  cathedral(u, v) {
    const n = 6; // 8 m tile: slabs about 1.9 m on the diagonal
    const x = (u + v) * n / 2, y = (u - v) * n / 2;
    const px = Math.floor(x), py = Math.floor(y), fx = x - px, fy = y - py;
    const dark = mod(px + py, 2) === 0;
    const joint = 1 - smooth(0.0, 0.02, Math.min(fx, 1 - fx, fy, 1 - fy));
    const vein = Math.abs(Math.sin((fbm(u, v, 8, 5, 201) * 12 + u * 9) * 3.1));
    let c = dark ? [0.25, 0.235, 0.24] : [0.34, 0.325, 0.33];
    c = mix3(c, dark ? [0.32, 0.3, 0.31] : [0.26, 0.25, 0.26], smooth(0.04, 0.0, vein) * 0.8);
    c = c.map((k) => k * (0.88 + 0.24 * fbm(u, v, 12, 4, 203)));
    const dust = smooth(0.55, 0.72, fbm(u, v, 4, 5, 205));
    c = mix3(c, [0.28, 0.18, 0.17], dust * 0.4);
    const crack = smooth(0.01, 0.0, voronoi(u, v, 22, 207, 1).edge) * smooth(0.55, 0.68, fbm(u, v, 6, 3, 209));
    return c.map((k) => k * (1 - 0.7 * joint) * (1 - 0.5 * crack));
  },
  // the rift: polished volcanic glass, near black with violet depth, fractured in big plates, ash drifted
  // into the fractures
  rift(u, v) {
    const vo = voronoi(u, v, 6, 301, 0.95);
    const depth = 0.18 + 0.07 * fbm(u, v, 6, 5, 303) + 0.05 * hash(vo.id, 1, 305);
    let c = [depth * 1.0, depth * 0.97, depth * 1.3];
    const sheen = smooth(0.6, 0.8, fbm(u * 2, v, 3, 3, 307));
    c = mix3(c, [0.16, 0.14, 0.24], sheen * 0.35);
    const frac = 1 - smooth(0.0, 0.03, vo.edge);
    c = mix3(c, [0.5, 0.5, 0.52], frac * 0.55);                 // ash in the fractures
    const ash = smooth(0.62, 0.8, fbm(u, v, 5, 5, 309));
    return mix3(c, [0.42, 0.42, 0.44], ash * 0.35);
  },
  // the core: black basalt crust over a glow, cracks bright orange (the floor is unlit: the glow is painted)
  core(u, v) {
    const vo = voronoi(u, v, 10, 401, 0.9);
    const n = fbm(u, v, 8, 5, 403);
    let c = [0.16 + 0.06 * n, 0.11 + 0.04 * n, 0.09 + 0.03 * n];
    const crack = 1 - smooth(0.0, 0.035, vo.edge);
    const hot = smooth(0.45, 0.65, fbm(u, v, 3, 4, 405));
    c = mix3(c, [0.95, 0.38, 0.08], crack * (0.25 + 0.75 * hot));
    const sulfur = smooth(0.7, 0.85, fbm(u, v, 7, 4, 407));
    c = mix3(c, [0.42, 0.36, 0.12], sulfur * 0.35);
    return c;
  },
  // molten metal and lava (the hazard strips): a hot melt, brightest in its flowing seams, with plates of cooling
  // crust drifting on it (written as lava_floor.png; the strips multiply it by their colour)
  lava(u, v) {
    const vo = voronoi(u + 0.15 * fbm(u, v, 4, 3, 501), v, 7, 503, 0.9);
    const crust = smooth(0.06, 0.2, vo.edge) * smooth(0.35, 0.6, hash(vo.id, 2, 505) + 0.3 * fbm(u, v, 10, 3, 507));
    const heat = 0.55 + 0.45 * fbm(u, v, 5, 5, 509);
    const melt = [1.0 * heat, 0.55 * heat * heat, 0.18 * heat * heat * heat];
    return mix3(melt, [0.06, 0.03, 0.02], crust * 0.92);
  },
};

// ── PNG (RGB, 8 bit) ────────────────────────────────────────────────
const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
function crc32(buf) { let c = 0xffffffff; for (const b of buf) c = CRC[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'latin1'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function png(w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

mkdirSync(out, { recursive: true });
for (const [name, fn] of Object.entries(FLOORS)) {
  if (only.length && !only.includes(name)) continue;
  const px = Buffer.alloc(N * N * 3);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const c = fn((x + 0.5) / N, (y + 0.5) / N);
    for (let k = 0; k < 3; k++) px[(y * N + x) * 3 + k] = Math.round(clamp(c[k]) * 255);
  }
  const file = resolve(out, `${name}_floor.png`);
  writeFileSync(file, png(N, N, px));
  console.log('wrote', file);
}
