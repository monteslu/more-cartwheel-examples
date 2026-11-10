#!/usr/bin/env node
// Paint the jet. From the source model (assets/source-models/f15-eagle-source.glb, the CC0 F-15 with
// its own camouflage maps) this writes assets/models/f15-eagle.glb with new surfaces, and the
// bandits' paint as assets/textures/f15-hostile.jpg:
//   - the airframe's base colour: a two-tone scheme (an upper and a lower colour split along the
//     fuselage, a dark radome, a matte anti-glare panel ahead of the canopy, a fin flash, a tail
//     number and wingtip bands), exhaust soot over the aft fuselage, and the source model's panel
//     lines (the lines are lifted from its camouflage map, the camouflage itself dropped)
//   - its roughness map (matte paint, the lines a touch rougher) and a normal map that sinks the
//     panel lines a little
//   - the nozzles, which the source leaves without texture coordinates: a cylindrical unwrap per
//     nozzle and a dark steel map with heat discolouration toward the lip (the bronze, violet and
//     blue bands of a hot exhaust), glossier than the paint
//   - the canopy a tinted glass (its factors; it has no map)
// The wreck model (assets/models/f15-wreck.glb, cut from the source by build-wreck-parts.py and
// rebound to these materials at runtime) gets the same nozzle coordinates. The textures are
// painted in object space from the mesh (every texel rasterised from the model's own UV layout),
// so the layout of the source model is kept and nothing is hand-painted.
// Needs ImageMagick (convert) for the JPEG/PNG codec and the tail number's glyphs (Russo One, the
// game's own font). Deterministic: the same input writes the same files.
//   node tools/build-jet-livery.mjs [--size 2048] [--debug]   (--debug: side and top views of the
//   paint as out/skyfighter-livery/debug-*.png for a look without a build)
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const GAME = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const WORK = join(GAME, '../../out/skyfighter-livery');
mkdirSync(WORK, { recursive: true });
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const S = +arg('--size', 2048), DEBUG = process.argv.includes('--debug');
const FONT = join(GAME, 'assets/fonts/RussoOne-Regular.ttf');

// ── glb in and out ──────────────────────────────────────────────────────────────────────────────
const readGlb = (path) => {
  const b = readFileSync(path);
  if (b.readUInt32LE(0) !== 0x46546c67) throw new Error(`${path}: not a glb`);
  const jl = b.readUInt32LE(12), json = JSON.parse(b.subarray(20, 20 + jl).toString());
  const bl = b.readUInt32LE(20 + jl), bin = b.subarray(28 + jl, 28 + jl + bl);
  return { json, bin };
};
const COMP = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 }, BYTES = { 5126: 4, 5125: 4, 5123: 2, 5121: 1 };
const readAccessor = (g, i) => {
  const a = g.json.accessors[i], bv = g.json.bufferViews[a.bufferView], n = COMP[a.type], unit = BYTES[a.componentType];
  const stride = bv.byteStride || unit * n, base = (bv.byteOffset || 0) + (a.byteOffset || 0);
  const out = a.componentType === 5126 ? new Float32Array(a.count * n) : new Uint32Array(a.count * n);
  for (let k = 0; k < a.count; k++) for (let c = 0; c < n; c++) {
    const o = base + k * stride + c * unit;
    out[k * n + c] = a.componentType === 5126 ? g.bin.readFloatLE(o) : a.componentType === 5125 ? g.bin.readUInt32LE(o) : a.componentType === 5123 ? g.bin.readUInt16LE(o) : g.bin.readUInt8(o);
  }
  return out;
};
// a fresh buffer: the buffer views still referenced, then the new data; images are replaced by name
class Bin {
  constructor() { this.parts = []; this.length = 0; this.views = []; }
  add(buf, extra = {}) {
    while (this.length % 4) { this.parts.push(Buffer.alloc(1)); this.length++; }
    const v = { buffer: 0, byteOffset: this.length, byteLength: buf.byteLength, ...extra };
    this.parts.push(Buffer.from(buf.buffer, buf.byteOffset, buf.byteLength)); this.length += buf.byteLength;
    this.views.push(v); return this.views.length - 1;
  }
  bytes() { const b = Buffer.concat(this.parts); return Buffer.concat([b, Buffer.alloc((4 - b.length % 4) % 4)]); }
}
const writeGlb = (path, json, bin) => {
  json.buffers = [{ byteLength: bin.length }];
  let js = Buffer.from(JSON.stringify(json)); js = Buffer.concat([js, Buffer.alloc((4 - js.length % 4) % 4, 0x20)]);
  const head = Buffer.alloc(12); head.writeUInt32LE(0x46546c67, 0); head.writeUInt32LE(2, 4); head.writeUInt32LE(28 + js.length + bin.length, 8);
  const jh = Buffer.alloc(8); jh.writeUInt32LE(js.length, 0); jh.writeUInt32LE(0x4e4f534a, 4);
  const bh = Buffer.alloc(8); bh.writeUInt32LE(bin.length, 0); bh.writeUInt32LE(0x004e4942, 4);
  writeFileSync(path, Buffer.concat([head, jh, js, bh, bin]));
};
// rebuild a glb's buffer, keeping the views the accessors use (images are rebuilt by the caller)
const rebuild = (g, keepImages) => {
  const bin = new Bin(), map = new Map();
  const used = new Set(g.json.accessors.map((a) => a.bufferView));
  if (keepImages) for (const im of g.json.images || []) used.add(im.bufferView);
  for (const [i, v] of g.json.bufferViews.entries()) if (used.has(i)) {
    const extra = {}; if (v.byteStride) extra.byteStride = v.byteStride; if (v.target) extra.target = v.target;
    map.set(i, bin.add(g.bin.subarray(v.byteOffset || 0, (v.byteOffset || 0) + v.byteLength), extra));
  }
  for (const a of g.json.accessors) a.bufferView = map.get(a.bufferView);
  if (keepImages) for (const im of g.json.images || []) im.bufferView = map.get(im.bufferView);
  g.json.bufferViews = bin.views; return bin;
};
const addAccessor = (g, bin, arr, type, minmax) => {
  const bv = bin.add(arr, { target: 34962 }), n = COMP[type];
  const a = { bufferView: bv, componentType: 5126, count: arr.length / n, type };
  if (minmax) { a.min = Array(n).fill(Infinity); a.max = Array(n).fill(-Infinity); for (let k = 0; k < arr.length; k++) { a.min[k % n] = Math.min(a.min[k % n], arr[k]); a.max[k % n] = Math.max(a.max[k % n], arr[k]); } }
  g.json.accessors.push(a); g.json.bufferViews = bin.views; return g.json.accessors.length - 1;
};

// ── images through ImageMagick ──────────────────────────────────────────────────────────────────
const magick = (args) => execFileSync('convert', args, { stdio: ['ignore', 'pipe', 'inherit'], maxBuffer: 1 << 30 });
const toGray = (file, w, h) => magick([file, '-resize', `${w}x${h}!`, '-colorspace', 'Gray', '-depth', '8', 'gray:-']);
const encode = (rgb, w, h, file, quality) => { const raw = join(WORK, 'raw.rgb'); writeFileSync(raw, rgb); magick(['-size', `${w}x${h}`, '-depth', '8', `rgb:${raw}`, ...(quality ? ['-quality', String(quality)] : []), file]); };
const label = (text, w, h) => { // white glyphs on black, w x h, the game's font
  const f = join(WORK, `label-${text}.png`);
  magick(['-background', 'black', '-fill', 'white', '-font', FONT, '-size', `${w}x${h}`, '-gravity', 'center', `label:${text}`, f]);
  return { px: toGray(f, w, h), w, h, at(u, v) { if (u < 0 || v < 0 || u >= 1 || v >= 1) return 0; return this.px[Math.floor(v * h) * w + Math.floor(u * w)] / 255; } };
};

// ── the source ──────────────────────────────────────────────────────────────────────────────────
const src = readGlb(join(GAME, 'assets/source-models/f15-eagle-source.glb'));
const airframe = src.json.meshes.find((m) => m.name === 'F15_Airframe').primitives[0];
const POS = readAccessor(src, airframe.attributes.POSITION), NRM = readAccessor(src, airframe.attributes.NORMAL), UV = readAccessor(src, airframe.attributes.TEXCOORD_0), IDX = readAccessor(src, airframe.indices);
// the source's camouflage map, as a grey image the size of the bake: its panel lines are kept
const srcColor = src.json.images.find((im) => im.name === 'F15_basecolor'), bvc = src.json.bufferViews[srcColor.bufferView];
writeFileSync(join(WORK, 'source-basecolor.jpg'), src.bin.subarray(bvc.byteOffset || 0, (bvc.byteOffset || 0) + bvc.byteLength));
const srcGray = toGray(join(WORK, 'source-basecolor.jpg'), S, S);

// ── rasterise the airframe into texture space: object position and normal per texel ─────────────
const N = S * S, cover = new Uint8Array(N), pos = new Float32Array(N * 3), nrm = new Float32Array(N * 3);
for (let t = 0; t < IDX.length; t += 3) {
  const i0 = IDX[t], i1 = IDX[t + 1], i2 = IDX[t + 2];
  const x0 = UV[i0 * 2] * S, y0 = UV[i0 * 2 + 1] * S, x1 = UV[i1 * 2] * S, y1 = UV[i1 * 2 + 1] * S, x2 = UV[i2 * 2] * S, y2 = UV[i2 * 2 + 1] * S;
  const det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0); if (Math.abs(det) < 1e-9) continue;
  const minx = Math.max(0, Math.floor(Math.min(x0, x1, x2)) - 1), maxx = Math.min(S - 1, Math.ceil(Math.max(x0, x1, x2)) + 1);
  const miny = Math.max(0, Math.floor(Math.min(y0, y1, y2)) - 1), maxy = Math.min(S - 1, Math.ceil(Math.max(y0, y1, y2)) + 1);
  for (let y = miny; y <= maxy; y++) for (let x = minx; x <= maxx; x++) {
    const px = x + .5, py = y + .5;
    let b1 = ((px - x0) * (y2 - y0) - (x2 - x0) * (py - y0)) / det, b2 = ((x1 - x0) * (py - y0) - (px - x0) * (y1 - y0)) / det, b0 = 1 - b1 - b2;
    const eps = -0.9 / Math.sqrt(Math.abs(det)); // a texel's width of slack past the edge, so edges are covered
    if (b0 < eps || b1 < eps || b2 < eps) continue;
    const k = y * S + x; if (cover[k] === 2) continue; // an inside texel is never overwritten by an edge
    const inside = b0 >= 0 && b1 >= 0 && b2 >= 0; if (cover[k] && !inside) continue;
    b0 = Math.max(0, b0); b1 = Math.max(0, b1); b2 = Math.max(0, b2); const s = b0 + b1 + b2; b0 /= s; b1 /= s; b2 /= s;
    for (let c = 0; c < 3; c++) { pos[k * 3 + c] = b0 * POS[i0 * 3 + c] + b1 * POS[i1 * 3 + c] + b2 * POS[i2 * 3 + c]; nrm[k * 3 + c] = b0 * NRM[i0 * 3 + c] + b1 * NRM[i1 * 3 + c] + b2 * NRM[i2 * 3 + c]; }
    cover[k] = inside ? 2 : 1;
  }
}
// dilate: an uncovered texel takes its nearest painted neighbour's surface, a few texels out, so the mip chain and bilinear
// filtering never pull in the background
for (let pass = 0; pass < 12; pass++) {
  const next = Uint8Array.from(cover);
  for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) {
    const k = y * S + x; if (cover[k]) continue;
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= S || yy >= S) continue;
      const kk = yy * S + xx; if (!cover[kk]) continue;
      for (let c = 0; c < 3; c++) { pos[k * 3 + c] = pos[kk * 3 + c]; nrm[k * 3 + c] = nrm[kk * 3 + c]; }
      next[k] = 1; break;
    }
  }
  cover.set(next);
}
let covered = 0; for (let k = 0; k < N; k++) covered += cover[k] > 0; console.log(`airframe: ${IDX.length / 3} triangles, ${(100 * covered / N).toFixed(1)} % of the ${S} x ${S} map painted`);

// ── the panel lines: the source's map less its camouflage (a high pass), the source's own labels erased ─
const blur = (img, r) => { // box blur, separable, r texels
  const tmp = new Float32Array(N), out = new Float32Array(N), w = 2 * r + 1;
  for (let y = 0; y < S; y++) { let acc = 0; for (let x = -r; x <= r; x++) acc += img[y * S + Math.min(S - 1, Math.max(0, x))]; for (let x = 0; x < S; x++) { tmp[y * S + x] = acc / w; acc += img[y * S + Math.min(S - 1, x + r + 1)] - img[y * S + Math.max(0, x - r)]; } }
  for (let x = 0; x < S; x++) { let acc = 0; for (let y = -r; y <= r; y++) acc += tmp[Math.min(S - 1, Math.max(0, y)) * S + x]; for (let y = 0; y < S; y++) { out[y * S + x] = acc / w; acc += tmp[Math.min(S - 1, y + r + 1) * S + x] - tmp[Math.max(0, y - r) * S + x]; } }
  return out;
};
const gray = new Float32Array(N); for (let k = 0; k < N; k++) gray[k] = srcGray[k] / 255;
const low = blur(gray, Math.round(S / 256));
const lines = new Float32Array(N); // 0 = plain paint, 1 = the darkest line
for (let k = 0; k < N; k++) lines[k] = Math.min(1, Math.max(0, (low[k] - gray[k]) * 9 - 0.04));
// the source model's own "IS 26 015" labels on its fins (map coordinates in units of the map's size)
for (const [cx, cy] of [[.058, .372], [.185, .372], [.332, .372], [.800, .420]]) {
  for (let y = Math.floor((cy - .03) * S); y < (cy + .03) * S; y++) for (let x = Math.floor((cx - .04) * S); x < (cx + .04) * S; x++) lines[y * S + x] = 0;
}

// ── the paint ────────────────────────────────────────────────────────────────────────────────────
const smooth = (a, b, v) => { const t = Math.min(1, Math.max(0, (v - a) / (b - a))); return t * t * (3 - 2 * t); };
const mix = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const hash = (x, y) => { let h = (Math.imul(x | 0, 374761393) + Math.imul(y | 0, 668265263)) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };
const vnoise = (x, y) => { const xi = Math.floor(x), yi = Math.floor(y), fx = x - xi, fy = y - yi, u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy); return (hash(xi, yi) * (1 - u) + hash(xi + 1, yi) * u) * (1 - v) + (hash(xi, yi + 1) * (1 - u) + hash(xi + 1, yi + 1) * u) * v; };
// the model: the nose at +z (z 9.7), the tail at -z, up +y; the wings y 2.0 .. 2.4, the fins y 3.5 .. 5.6 at |x| 1.5
const FIN_LE = (y) => -5.89 - 0.75 * (y - 3.5); // the fin's leading edge
const number = { player: label('07', 256, 192), hostile: label('31', 256, 192) };
const SCHEMES = {
  player: { top: [62, 84, 110], bottom: [158, 166, 174], radome: [74, 76, 80], glare: [44, 47, 52], accent: [232, 156, 48], ink: [236, 236, 230], noseRing: false },
  hostile: { top: [58, 48, 46], bottom: [78, 80, 82], radome: [52, 52, 54], glare: [22, 22, 24], accent: [190, 40, 36], ink: [210, 205, 200], noseRing: true },
};
const paint = (scheme, k) => {
  const c = SCHEMES[scheme], x = pos[k * 3], y = pos[k * 3 + 1], z = pos[k * 3 + 2], nx = nrm[k * 3], ny = nrm[k * 3 + 1], nz = nrm[k * 3 + 2];
  const ax = Math.abs(x);
  // two tones, split along the fuselage a little above the wing line; the wings' top and underside by their normal
  let col = mix(c.bottom, c.top, smooth(2.05, 2.3, y + 0.35 * ny));
  const fin = y > 3.45 && z < -4 && ax < 2.1 && Math.abs(ny) < 0.6;
  if (fin) {
    col = c.top;
    const s = z + 0.75 * (y - 3.5); // the fin flash runs parallel to the leading edge
    if (s > -6.95 && s < -6.55) col = c.accent;
    // the tail number, read the right way round on both faces (from the right the nose is on the left)
    const u = (x > 0 ? -(z + 7.9) : (z + 7.9)) / 1.0 + 0.5, v = (4.45 - y) / 0.75;
    col = mix(col, c.ink, number[scheme].at(u, v));
  }
  // wingtip bands
  if (ax > 5.95 && y < 3) col = c.accent;
  // the radome and the anti-glare panel ahead of the windscreen
  col = mix(col, c.radome, smooth(7.55, 7.7, z));
  if (z > 6.1 && z < 7.6 && ny > 0.7 && ax < 0.5) col = mix(col, c.glare, smooth(0.7, 0.85, ny));
  if (c.noseRing && z > 6.85 && z < 7.3) col = c.accent;
  // exhaust soot over the aft fuselage, a slight unevenness in the paint, and the panel lines
  const soot = ax < 1.7 && y < 3.4 ? smooth(-6.8, -9.2, z) * 0.45 : 0;
  const wear = 0.965 + 0.07 * (vnoise(x * 1.7 + 50, z * 1.7 + y * 3) * 0.6 + vnoise(x * 6 + 7, z * 6 + y * 5) * 0.4);
  const line = lines[k];
  const f = (1 - soot) * wear * (1 - 0.32 * line);
  return [col[0] * f, col[1] * f, col[2] * f];
};
const rgb = { player: new Uint8Array(N * 3), hostile: new Uint8Array(N * 3) }, metalRough = new Uint8Array(N * 3), normal = new Uint8Array(N * 3);
const height = new Float32Array(N);
for (let k = 0; k < N; k++) {
  if (!cover[k]) { for (const s of ['player', 'hostile']) { rgb[s][k * 3] = 60; rgb[s][k * 3 + 1] = 64; rgb[s][k * 3 + 2] = 70; } metalRough[k * 3] = 255; metalRough[k * 3 + 1] = 190; normal[k * 3] = 128; normal[k * 3 + 1] = 128; normal[k * 3 + 2] = 255; continue; }
  for (const s of ['player', 'hostile']) { const p = paint(s, k); for (let c = 0; c < 3; c++) rgb[s][k * 3 + c] = Math.round(Math.min(255, Math.max(0, p[c]))); }
  const y = pos[k * 3 + 1], z = pos[k * 3 + 2], ny = nrm[k * 3 + 1], ax = Math.abs(pos[k * 3]);
  // roughness: matte paint, the anti-glare panel flatter still, the lines a touch rougher; no metal
  let rough = 0.72 + 0.12 * lines[k];
  if (z > 6.1 && z < 7.6 && ny > 0.75 && ax < 0.5) rough = 0.92;
  if (z > 7.6) rough = 0.95;
  metalRough[k * 3] = 255; metalRough[k * 3 + 1] = Math.round(rough * 255); metalRough[k * 3 + 2] = 0;
  height[k] = -lines[k];
}
// the normal map from the lines' height (a groove), glTF's convention: +X right, +Y up the image
for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) {
  const k = y * S + x; if (!cover[k]) continue;
  const h = (xx, yy) => height[Math.min(S - 1, Math.max(0, yy)) * S + Math.min(S - 1, Math.max(0, xx))];
  const dx = (h(x + 1, y) - h(x - 1, y)) * 2.2, dy = (h(x, y + 1) - h(x, y - 1)) * 2.2;
  const len = Math.hypot(dx, dy, 1), nxv = -dx / len, nyv = dy / len, nzv = 1 / len;
  normal[k * 3] = Math.round(nxv * 127.5 + 127.5); normal[k * 3 + 1] = Math.round(nyv * 127.5 + 127.5); normal[k * 3 + 2] = Math.round(nzv * 127.5 + 127.5);
}

// ── the nozzles: a cylindrical unwrap per nozzle, each corner its own vertex so the seam never spans a triangle ──
const unwrapNozzle = (g, bin, prim, lipAtMax) => {
  const P = readAccessor(g, prim.attributes.POSITION), Nn = readAccessor(g, prim.attributes.NORMAL), I = prim.indices != null ? readAccessor(g, prim.indices) : Uint32Array.from({ length: P.length / 3 }, (_, i) => i);
  let minz = Infinity, maxz = -Infinity, cx = 0, cy = 0; for (let k = 0; k < P.length / 3; k++) { minz = Math.min(minz, P[k * 3 + 2]); maxz = Math.max(maxz, P[k * 3 + 2]); cx += P[k * 3]; cy += P[k * 3 + 1]; }
  cx /= P.length / 3; cy /= P.length / 3;
  // one nozzle or two side by side: each turns about its own centre
  const centres = { left: [0, 0, 0], right: [0, 0, 0] };
  for (let k = 0; k < P.length / 3; k++) { const c = centres[P[k * 3] < cx ? 'left' : 'right']; c[0] += P[k * 3]; c[1] += P[k * 3 + 1]; c[2]++; }
  let spread = 0; for (let k = 0; k < P.length / 3; k++) spread = Math.max(spread, Math.abs(P[k * 3] - cx));
  const two = spread > 0.9 * (maxz - minz);
  const uvOf = (k) => {
    const c = two ? centres[P[k * 3] < cx ? 'left' : 'right'] : [cx * P.length / 3, cy * P.length / 3, P.length / 3];
    const u = Math.atan2(P[k * 3 + 1] - c[1] / c[2], P[k * 3] - c[0] / c[2]) / (2 * Math.PI) + 0.5;
    const v = lipAtMax ? (P[k * 3 + 2] - minz) / (maxz - minz) : (maxz - P[k * 3 + 2]) / (maxz - minz);
    return [u, v];
  };
  const n = I.length, P2 = new Float32Array(n * 3), N2 = new Float32Array(n * 3), UV2 = new Float32Array(n * 2);
  for (let t = 0; t < n; t += 3) {
    const uvs = [0, 1, 2].map((c) => uvOf(I[t + c]));
    const umax = Math.max(...uvs.map((u) => u[0])); for (const u of uvs) if (umax - u[0] > 0.5) u[0] += 1; // across the seam: carry on past 1
    for (let c = 0; c < 3; c++) { const i = I[t + c]; for (let d = 0; d < 3; d++) { P2[(t + c) * 3 + d] = P[i * 3 + d]; N2[(t + c) * 3 + d] = Nn[i * 3 + d]; } UV2[(t + c) * 2] = uvs[c][0]; UV2[(t + c) * 2 + 1] = uvs[c][1]; }
  }
  prim.attributes = { POSITION: addAccessor(g, bin, P2, 'VEC3', true), NORMAL: addAccessor(g, bin, N2, 'VEC3'), TEXCOORD_0: addAccessor(g, bin, UV2, 'VEC2') };
  delete prim.indices;
};
// the nozzle map: dark brushed steel forward, the heat bands toward the lip
const NW = 512, NH = 256, nozzle = new Uint8Array(NW * NH * 3);
for (let y = 0; y < NH; y++) for (let x = 0; x < NW; x++) {
  const u = x / NW, v = (y + .5) / NH;
  const streak = 0.9 + 0.2 * (vnoise(u * 160, v * 3) * 0.5 + vnoise(u * 400 + 9, v * 6) * 0.5); // brushed along the petals
  const wobble = (vnoise(u * 12, v * 4 + 3) - 0.5) * 0.12 + (vnoise(u * 40 + 5, v * 9) - 0.5) * 0.05;
  const t = v + wobble;
  let col = [44, 44, 47];
  col = mix(col, [118, 86, 50], smooth(0.42, 0.56, t)); // straw and bronze
  col = mix(col, [84, 60, 102], smooth(0.58, 0.72, t)); // violet
  col = mix(col, [52, 78, 104], smooth(0.74, 0.86, t)); // blue
  col = mix(col, [30, 30, 32], smooth(0.9, 1.0, t)); // the sooted lip
  const seam = 1 - 0.35 * smooth(0.985, 1, Math.abs(((u * 24) % 1) - 0.5) * 2); // the petals' edges
  for (let c = 0; c < 3; c++) nozzle[(y * NW + x) * 3 + c] = Math.round(Math.min(255, col[c] * streak * seam));
}

// ── write the jet ────────────────────────────────────────────────────────────────────────────────
const jet = readGlb(join(GAME, 'assets/source-models/f15-eagle-source.glb'));
const bin = rebuild(jet, false);
encode(rgb.player, S, S, join(WORK, 'basecolor.jpg'), 92); encode(normal, S, S, join(WORK, 'normal.png')); encode(metalRough, S, S, join(WORK, 'metal-rough.png'));
encode(nozzle, NW, NH, join(WORK, 'nozzle.jpg'), 92); encode(rgb.hostile, S, S, join(GAME, 'assets/textures/f15-hostile.jpg'), 92);
const image = (name, file, mime) => { const i = jet.json.images.findIndex((im) => im.name === name); const im = { name, mimeType: mime, bufferView: bin.add(readFileSync(file)) }; if (i >= 0) jet.json.images[i] = im; else jet.json.images.push(im); return i >= 0 ? i : jet.json.images.length - 1; };
image('F15_normal', join(WORK, 'normal.png'), 'image/png'); image('F15_basecolor', join(WORK, 'basecolor.jpg'), 'image/jpeg'); image('F15_metal_rough', join(WORK, 'metal-rough.png'), 'image/png');
const nozzleImage = image('F15_nozzle', join(WORK, 'nozzle.jpg'), 'image/jpeg');
jet.json.samplers = [{ magFilter: 9729, minFilter: 9987, wrapS: 10497, wrapT: 10497 }];
jet.json.textures = jet.json.images.map((_, i) => ({ sampler: 0, source: i }));
const mat = (name) => jet.json.materials.find((m) => m.name === name);
Object.assign(mat('F15_Airframe'), { normalTexture: { index: 0, scale: 1 }, pbrMetallicRoughness: { baseColorTexture: { index: 1 }, metallicRoughnessTexture: { index: 2 }, metallicFactor: 1, roughnessFactor: 1 } });
Object.assign(mat('F15_Canopy_Glass'), { alphaMode: 'BLEND', pbrMetallicRoughness: { baseColorFactor: [0.30, 0.22, 0.10, 0.55], metallicFactor: 0, roughnessFactor: 0.06 } });
Object.assign(mat('F15_Nozzle_Metal'), { pbrMetallicRoughness: { baseColorTexture: { index: nozzleImage }, baseColorFactor: [1, 1, 1, 1], metallicFactor: 0.55, roughnessFactor: 0.42 } });
Object.assign(mat('F15_Nozzle_Inner'), { pbrMetallicRoughness: { baseColorFactor: [0.05, 0.045, 0.04, 1], metallicFactor: 0.7, roughnessFactor: 0.55 } });
for (const m of jet.json.meshes) for (const p of m.primitives) if (jet.json.materials[p.material].name === 'F15_Nozzle_Metal') unwrapNozzle(jet, bin, p, false);
writeGlb(join(GAME, 'assets/models/f15-eagle.glb'), jet.json, bin.bytes());

// ── the wreck's nozzles get the same coordinates (it is cut from the source, the nose turned to -z) ──
const wreck = readGlb(join(GAME, 'assets/models/f15-wreck.glb'));
const wbin = rebuild(wreck, true);
for (const m of wreck.json.meshes) for (const p of m.primitives) if (wreck.json.materials[p.material].name === 'F15_Nozzle_Metal' && !p.attributes.TEXCOORD_0) unwrapNozzle(wreck, wbin, p, true);
writeGlb(join(GAME, 'assets/models/f15-wreck.glb'), wreck.json, wbin.bytes());
console.log(`wrote assets/models/f15-eagle.glb, assets/models/f15-wreck.glb, assets/textures/f15-hostile.jpg`);

// ── a look without a build: the paint projected on the side and the top ──────────────────────────
if (DEBUG) {
  const W = 1200, H = 480;
  for (const scheme of ['player', 'hostile']) for (const view of ['side', 'top']) {
    const img = new Uint8Array(W * H * 3).fill(30), depth = new Float32Array(W * H).fill(-Infinity);
    for (let k = 0; k < N; k++) {
      if (!cover[k]) continue;
      const x = pos[k * 3], y = pos[k * 3 + 1], z = pos[k * 3 + 2];
      const px = Math.round((9.9 - z) / 20 * W), py = view === 'side' ? Math.round(H - (y - 1) / 5 * H) : Math.round(H / 2 + x / 14 * H), d = view === 'side' ? x : y;
      if (px < 0 || py < 0 || px >= W || py >= H) continue;
      const q = py * W + px; if (d < depth[q]) continue; depth[q] = d;
      for (let c = 0; c < 3; c++) img[q * 3 + c] = rgb[scheme][k * 3 + c];
    }
    encode(img, W, H, join(WORK, `debug-${scheme}-${view}.png`));
  }
  console.log(`debug views in ${WORK}`);
}
