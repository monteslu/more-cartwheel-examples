// The sunny sky's far mountains as a panorama ring (assets/sky/sunny_ring.png)
// resampled from the original sky cube's side faces by direction: azimuth
// across (0 = looking down the course, toward -z), elevation -30..+30 degrees
// down the rows, alpha fading the sky out above the peaks (+4..+15 degrees)
// so the engine's ProceduralSky shows through. Plus a tileable cloud layer
// (assets/sky/clouds.png: value-noise fbm in the alpha) for a high cloud deck.
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { readSgi, toRgbaTopDown } from './lib/sgi-rgb.mjs';
import { writePng } from '../../../deps/cartwheel-engine/tools/lib/png.mjs';
const root = resolve(dirname(new URL(import.meta.url).pathname), '..');
const data = join(root, 'deps/tuxracer/tuxracer-data-0.61.orig/courses/common');
mkdirSync(join(root, 'assets/sky'), { recursive: true });
const face = (n) => { const img = readSgi(join(data, n)); return { w: img.w, h: img.h, px: toRgbaTopDown(img) }; };
const sample = (f, s, t) => { // bilinear, s t in 0..1 (t top-down)
  const x = Math.min(f.w - 1, Math.max(0, s * f.w - 0.5)), y = Math.min(f.h - 1, Math.max(0, t * f.h - 0.5));
  const x0 = Math.floor(x), y0 = Math.floor(y), x1 = Math.min(f.w - 1, x0 + 1), y1 = Math.min(f.h - 1, y0 + 1), fx = x - x0, fy = y - y0;
  const o = [0, 0, 0];
  for (let c = 0; c < 3; c++) {
    const a = f.px[(x0 + y0 * f.w) * 4 + c], b = f.px[(x1 + y0 * f.w) * 4 + c], cc = f.px[(x0 + y1 * f.w) * 4 + c], d = f.px[(x1 + y1 * f.w) * 4 + c];
    o[c] = (a * (1 - fx) + b * fx) * (1 - fy) + (cc * (1 - fx) + d * fx) * fy;
  }
  return o;
};
const W = 4096, H = 1024, E0 = -50, E1 = 30;
// the user's own peak photo (assets/source/rainier.jpg, Mount Rainier from a plane): keyed against its
// sky, cut above the wing, composited over the cube's mountains at the end of the course and, scaled
// and mirrored, around the ring. Skipped when the file is absent.
import { existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { readImage } from '../../../deps/cartwheel-engine/tools/lib/png.mjs';
function loadPeak() {
  const src = join(root, 'assets/source/rainier.jpg');
  if (!existsSync(src)) return null;
  const tmp = join(root, 'assets/source/rainier.png');
  if (!existsSync(tmp)) execFileSync('sips', ['-s', 'format', 'png', src, '--out', tmp], { stdio: 'ignore' });
  const im = readImage(tmp);
  const { w, h, rgba: px } = im;
  // the wing: a slanted line from (0, 0.675 h) to (w, 0.78 h); keep above it with a margin
  const cut = (x) => Math.floor(h * (0.675 + 0.105 * x / w)) - Math.floor(h * 0.012);
  // sky: blue-dominant, not bright; scan each column from the top to the skyline
  const alpha = new Float32Array(w * h);
  let top = h;
  for (let x = 0; x < w; x++) {
    let y = 0;
    for (; y < cut(x); y++) {
      const i = (x + y * w) * 4, r = px[i], g = px[i + 1], b = px[i + 2];
      const sky = b > g && g >= r - 6 && b - r > 22 && (r + g + b) / 3 < 225;
      if (!sky) break;
    }
    top = Math.min(top, y);
    for (let yy = y; yy < cut(x); yy++) alpha[x + yy * w] = 1;
    for (let f = 1; f <= 3; f++) if (y - f >= 0) alpha[x + (y - f) * w] = Math.max(alpha[x + (y - f) * w], 1 - f / 4); // a soft edge
  }
  const bottom = cut(0) - 1;
  return { w, h, px, alpha, top, bottom };
}
const PEAK = loadPeak();
function peakAt(out, cx, baseE, degWide, mirror, haze) { // cx: column of the centre; baseE: elevation of the cut bottom
  if (!PEAK) return;
  const { w, h, px, alpha, top, bottom } = PEAK;
  const ph = bottom - top + 1, pw = w;
  const cols = Math.round(degWide / 360 * W), rows = Math.round(cols * ph / pw);
  const rowBase = Math.round((E1 - baseE) / (E1 - E0) * H);
  for (let y = 0; y < rows; y++) {
    const oy = rowBase - rows + y;
    if (oy < 0 || oy >= H) continue;
    const sy = top + Math.floor((y + 0.5) / rows * ph);
    const fade = Math.min(1, (rows - y) / (rows * 0.22)); // the bottom fifth dissolves into the cube's slopes
    for (let x = 0; x < cols; x++) {
      const ox = ((cx - Math.floor(cols / 2) + x) % W + W) % W;
      const sx0 = Math.floor((x + 0.5) / cols * pw), sx = mirror ? pw - 1 - sx0 : sx0;
      const edge = Math.min(1, Math.min(x, cols - 1 - x) / (cols * 0.2)); // the photo frames the flanks: dissolve them
      const a = alpha[sx + sy * w] * fade * edge;
      if (a <= 0) continue;
      const si = (sx + sy * w) * 4, oi = (ox + oy * W) * 4;
      const hz = [190, 205, 232];
      for (let c = 0; c < 3; c++) {
        const v = px[si + c] * (1 - haze) + hz[c] * haze;
        out[oi + c] = Math.round(v * a + out[oi + c] * (1 - a));
      }
      out[oi + 3] = Math.max(out[oi + 3], Math.round(a * 255));
    }
  }
}
for (const cond of ['sunny']) {
  const F = { front: face(`${cond}front.rgb`), back: face(`${cond}back.rgb`), left: face(`${cond}left.rgb`), right: face(`${cond}right.rgb`) };
  const out = new Uint8Array(W * H * 4);
  for (let y = 0; y < H; y++) {
    const e = (E1 - (y + 0.5) / H * (E1 - E0)) * Math.PI / 180; // row 0 = the top
    const edeg = e * 180 / Math.PI;
    let alpha = 1 - Math.min(1, Math.max(0, (edeg - 4) / 11));
    const floorMix = Math.min(1, Math.max(0, (-4 - edeg) / 6)); // below -4 degrees the cube's beige floor gives way to a snowfield haze
    alpha *= 1 - Math.min(1, Math.max(0, (-30 - edeg) / 15)); // and the band dissolves by -45: the sky's ground colour takes over
    if (y < 2 || y >= H - 2) alpha = 0; // the map wraps: an opaque bottom row would bleed into the top edge as a line
    for (let x = 0; x < W; x++) {
      const phi = (x + 0.5) / W * 2 * Math.PI; // 0 = toward -z, increasing toward +x
      const d = [Math.sin(phi) * Math.cos(e), Math.sin(e), -Math.cos(phi) * Math.cos(e)];
      const ax = Math.abs(d[0]), az = Math.abs(d[2]);
      let c;
      if (az >= ax) {
        if (d[2] < 0) c = sample(F.front, (d[0] / az + 1) / 2, (1 - d[1] / az) / 2); // looking toward -z: +x is the right of the picture
        else c = sample(F.back, (-d[0] / az + 1) / 2, (1 - d[1] / az) / 2);
      } else {
        if (d[0] > 0) c = sample(F.right, (d[2] / ax + 1) / 2, (1 - d[1] / ax) / 2); // looking toward +x: +z is the right
        else c = sample(F.left, (-d[2] / ax + 1) / 2, (1 - d[1] / ax) / 2);
      }
      const i = (x + y * W) * 4;
      const haze = [214, 222, 236];
      for (let k = 0; k < 3; k++) c[k] = c[k] * (1 - floorMix) + haze[k] * floorMix;
      out[i] = Math.round(c[0]), out[i + 1] = Math.round(c[1]), out[i + 2] = Math.round(c[2]), out[i + 3] = Math.round(alpha * 255);
    }
  }
  if (PEAK) { // the big peak ahead, smaller ones around; the cube's own slopes stay underneath
    peakAt(out, Math.round(W * 0.0), 0.2, 42, false, 0.22);
    peakAt(out, Math.round(W * 0.17), -0.6, 24, true, 0.42);
    peakAt(out, Math.round(W * 0.83), -0.4, 22, false, 0.44);
    peakAt(out, Math.round(W * 0.42), -1.0, 18, true, 0.52);
    peakAt(out, Math.round(W * 0.6), -0.8, 20, false, 0.52);
    console.log('peak photo composited');
  }
  writePng(join(root, `assets/sky/${cond}_ring.png`), W, H, out);
  console.log(`${cond}_ring.png ${W}x${H}`);
}
// clouds: tileable fbm (periodic value noise), soft deck with gaps
{
  const N = 512, out = new Uint8Array(N * N * 4);
  const hash = (x, y, p) => { x = ((x % p) + p) % p; y = ((y % p) + p) % p; let h = (x * 374761393 + y * 668265263 + p * 97) >>> 0; h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0; return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };
  const vnoise = (x, y, p) => { const x0 = Math.floor(x), y0 = Math.floor(y), fx = x - x0, fy = y - y0, wx = fx * fx * (3 - 2 * fx), wy = fy * fy * (3 - 2 * fy);
    const a = hash(x0, y0, p), b = hash(x0 + 1, y0, p), c = hash(x0, y0 + 1, p), d = hash(x0 + 1, y0 + 1, p);
    return (a + (b - a) * wx) * (1 - wy) + (c + (d - c) * wx) * wy; };
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = x / N, v = y / N;
    let f = 0, amp = 0.5, per = 4;
    for (let o = 0; o < 5; o++) { f += vnoise(u * per, v * per, per) * amp; amp *= 0.5; per *= 2; }
    const a = Math.min(1, Math.max(0, (f - 0.42) / 0.3));
    const i = (x + y * N) * 4;
    const l = 235 + Math.round(20 * Math.min(1, a)); // brighter where thick
    out[i] = l, out[i + 1] = l, out[i + 2] = Math.min(255, l + 6), out[i + 3] = Math.round(a * a * 255);
  }
  writePng(join(root, 'assets/sky/clouds.png'), N, N, out);
  console.log('clouds.png');
}

// the snow detail texture (assets/sky/snowdetail.png): one tileable 512^2 map the snow shader reads instead of
// evaluating noise per pixel. R, G: the gradient of a stretched ripple field (sastrugi) in x and z, centred
// on 128; B: a fine grain value; A: a cloud deck density for the cloud shadows. Periodic value noise.
{
  const N = 512, out = new Uint8Array(N * N * 4);
  const hashp = (x, y, p) => { x = ((x % p) + p) % p; y = ((y % p) + p) % p; let h = (x * 374761393 + y * 668265263 + p * 97) >>> 0; h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0; return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };
  const vn = (x, y, px, py) => { const x0 = Math.floor(x), y0 = Math.floor(y), fx = x - x0, fy = y - y0, wx = fx * fx * (3 - 2 * fx), wy = fy * fy * (3 - 2 * fy);
    const h = (i, j) => hashp(((i % px) + px) % px, ((j % py) + py) % py, px * 131 + py);
    const a = h(x0, y0), b = h(x0 + 1, y0), c = h(x0, y0 + 1), d = h(x0 + 1, y0 + 1);
    return (a + (b - a) * wx) * (1 - wy) + (c + (d - c) * wx) * wy; };
  // the tile spans 8 m of ripple field: ripples ~0.4 m apart across z (long along x)
  const ripple = (u, v) => vn(u * 3, v * 22, 3, 22) * 0.6 + vn(u * 6 + 0.3, v * 44 + 0.7, 6, 44) * 0.4;
  const grain = (u, v) => vn(u * 160, v * 160, 160, 160);
  const cloud = (u, v) => { let f = 0, amp = 0.5, per = 4; for (let o = 0; o < 5; o++) { f += vn(u * per, v * per, per, per) * amp; amp *= 0.5; per *= 2; } return f; };
  const e = 1 / N;
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = x / N, v = y / N;
    const gx = (ripple(u + e, v) - ripple(u - e, v)) / (2 * e), gz = (ripple(u, v + e) - ripple(u, v - e)) / (2 * e); // per tile unit
    const i = (x + y * N) * 4;
    out[i] = Math.round(Math.min(255, Math.max(0, 128 + gx * 6)));   // gx is up to ~+-12 per tile unit
    out[i + 1] = Math.round(Math.min(255, Math.max(0, 128 + gz * 1.2))); // gz up to ~+-80
    out[i + 2] = Math.round(grain(u, v) * 255);
    out[i + 3] = Math.round(cloud(u, v) * 255);
  }
  writePng(join(root, 'assets/sky/snowdetail.png'), N, N, out);
  console.log('snowdetail.png');
}
