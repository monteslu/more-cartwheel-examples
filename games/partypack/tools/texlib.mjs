// Procedural texture toolkit: tileable noise, cells, height to normal map, PNG writer.
// Every pixel the game shows is made by our own code (no downloaded art). No dependencies.
import { deflateSync } from 'node:zlib';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

export const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
export const hex = (h) => [((h >> 16) & 255) / 255, ((h >> 8) & 255) / 255, (h & 255) / 255];

/** deterministic integer hash to [0,1) */
export function hash2(x, y, s) {
  let h = Math.imul(x | 0, 0x27d4eb2d) ^ Math.imul(y | 0, 0x165667b1) ^ Math.imul(s | 0, 0x9e3779b1);
  h = Math.imul(h ^ (h >>> 15), 0x85ebca6b); h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35); h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}
/** value noise that tiles with the given period (in lattice cells) */
export function vnoise(x, y, per, s) {
  const xi = Math.floor(x), yi = Math.floor(y), fx = x - xi, fy = y - yi;
  const u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
  const w = (i) => ((i % per) + per) % per;
  const a = hash2(w(xi), w(yi), s), b = hash2(w(xi + 1), w(yi), s), c = hash2(w(xi), w(yi + 1), s), d = hash2(w(xi + 1), w(yi + 1), s);
  return lerp(lerp(a, b, u), lerp(c, d, u), v);
}
/** fractal noise over a unit square that tiles: p in [0,1)^2, base frequency f cells */
export function fbm(px, py, f, oct, s) {
  let amp = 0.5, sum = 0, norm = 0, fr = f;
  for (let o = 0; o < oct; o++) { sum += amp * vnoise(px * fr, py * fr, fr, s + o * 17); norm += amp; amp *= 0.5; fr *= 2; }
  return sum / norm;
}
/** tileable cells: returns [F1, F2, id] distances in cell units for n cells across */
export function cells(px, py, n, s, jitter = 1) {
  const x = px * n, y = py * n, xi = Math.floor(x), yi = Math.floor(y);
  let f1 = 9, f2 = 9, id = 0;
  for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
    const cx = xi + i, cy = yi + j, wx = ((cx % n) + n) % n, wy = ((cy % n) + n) % n;
    const ox = cx + 0.5 + (hash2(wx, wy, s) - 0.5) * jitter, oy = cy + 0.5 + (hash2(wx, wy, s + 5) - 0.5) * jitter;
    const d = Math.hypot(x - ox, y - oy);
    if (d < f1) { f2 = f1; f1 = d; id = hash2(wx, wy, s + 9); } else if (d < f2) f2 = d;
  }
  return [f1, f2, id];
}

/** a set of float planes for one material */
export class Tex {
  constructor(w, h = w) {
    this.w = w; this.h = h; const n = w * h;
    this.a = [new Float32Array(n), new Float32Array(n), new Float32Array(n)]; // albedo, sRGB 0..1
    this.height = new Float32Array(n);
    this.rough = new Float32Array(n).fill(0.8);
    this.ao = new Float32Array(n).fill(1);
    this.metal = new Float32Array(n);
    this.e = null; // emissive rgb, optional
    this.alpha = null;
  }
  emissive() { if (!this.e) this.e = [new Float32Array(this.w * this.h), new Float32Array(this.w * this.h), new Float32Array(this.w * this.h)]; return this.e; }
  /** per pixel callback fn(u,v,x,y,set) where set(prop,value) sets albedo rgb/height/etc */
  each(fn) {
    const { w, h } = this;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) fn(x / w, y / h, y * w + x, x, y);
  }
  /** a normal map (OpenGL convention, Y up in texture space with v down) from the height plane */
  normals(strength) {
    const { w, h, height } = this, out = new Uint8Array(w * h * 3);
    const H = (x, y) => height[((y + h) % h) * w + ((x + w) % w)];
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const dx = (H(x + 1, y) - H(x - 1, y)) * strength, dy = (H(x, y + 1) - H(x, y - 1)) * strength;
      // image v runs down; glTF +Y green points up the surface of the image
      let nx = -dx, ny = dy, nz = 1; const l = Math.hypot(nx, ny, nz); nx /= l; ny /= l; nz /= l;
      const o = (y * w + x) * 3;
      out[o] = Math.round((nx * 0.5 + 0.5) * 255); out[o + 1] = Math.round((ny * 0.5 + 0.5) * 255); out[o + 2] = Math.round((nz * 0.5 + 0.5) * 255);
    }
    return out;
  }
  /** blur the albedo, height or roughness by a few box passes (cheap softening) */
  blur(plane, r = 1, passes = 1) {
    const { w, h } = this;
    for (let p = 0; p < passes; p++) {
      const tmp = new Float32Array(w * h);
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { let s = 0; for (let k = -r; k <= r; k++) s += plane[y * w + ((x + k + w) % w)]; tmp[y * w + x] = s / (2 * r + 1); }
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { let s = 0; for (let k = -r; k <= r; k++) s += tmp[((y + k + h) % h) * w + x]; plane[y * w + x] = s / (2 * r + 1); }
    }
  }
}

const crcTable = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
function crc32(buf) { let c = 0xffffffff; for (let i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0); out.write(type, 4, 'latin1'); Buffer.from(data).copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}
/** encode 8 bit RGB or RGBA (channels 3 or 4) with the Sub filter */
export function png(w, h, ch, bytes) {
  const stride = w * ch, raw = Buffer.alloc((stride + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (stride + 1)] = 1;
    for (let i = 0; i < stride; i++) raw[y * (stride + 1) + 1 + i] = (bytes[y * stride + i] - (i >= ch ? bytes[y * stride + i - ch] : 0)) & 255;
  }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = ch === 4 ? 6 : 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}
export function save(path, w, h, ch, bytes) { mkdirSync(dirname(path), { recursive: true }); writeFileSync(path, png(w, h, ch, bytes)); }
const b8 = (v) => Math.round(clamp(v) * 255);

/** write <dir>/<name>_c.png (albedo), _n.png (normal), _orm.png (occlusion, roughness, metal), _e.png (emissive) */
export function writeSet(dir, name, t, normalStrength = 2) {
  const { w, h } = t, n = w * h;
  const c = new Uint8Array(n * (t.alpha ? 4 : 3));
  for (let i = 0; i < n; i++) {
    if (t.alpha) { c[i * 4] = b8(t.a[0][i]); c[i * 4 + 1] = b8(t.a[1][i]); c[i * 4 + 2] = b8(t.a[2][i]); c[i * 4 + 3] = b8(t.alpha[i]); }
    else { c[i * 3] = b8(t.a[0][i]); c[i * 3 + 1] = b8(t.a[1][i]); c[i * 3 + 2] = b8(t.a[2][i]); }
  }
  save(`${dir}/${name}_c.png`, w, h, t.alpha ? 4 : 3, c);
  save(`${dir}/${name}_n.png`, w, h, 3, t.normals(normalStrength));
  const o = new Uint8Array(n * 3);
  for (let i = 0; i < n; i++) { o[i * 3] = b8(t.ao[i]); o[i * 3 + 1] = b8(t.rough[i]); o[i * 3 + 2] = b8(t.metal[i]); }
  save(`${dir}/${name}_orm.png`, w, h, 3, o);
  if (t.e) {
    const e = new Uint8Array(n * 3);
    for (let i = 0; i < n; i++) { e[i * 3] = b8(t.e[0][i]); e[i * 3 + 1] = b8(t.e[1][i]); e[i * 3 + 2] = b8(t.e[2][i]); }
    save(`${dir}/${name}_e.png`, w, h, 3, e);
  }
}
