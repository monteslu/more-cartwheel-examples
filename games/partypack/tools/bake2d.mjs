// Bakes the 2D art as lit sprites: every sprite is a small height field (a bevelled shape plus surface detail)
// shaded with a key light, a cool fill, a rim and a specular lobe, then written three ways:
//   <name>.png    albedo with the light baked in (for unlit drawing: menus, icons, HUD)
//   <name>_a.png  albedo with only the ambient occlusion baked in (for the lit 2D layers)
//   <name>_n.png  tangent-space normal map of the same height field (OpenGL convention), same size
// Run: node tools/bake2d.mjs   (writes assets/art/). Every pixel is made by this code.
import { fbm, cells, hash2, clamp, lerp, smooth, save } from './texlib.mjs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'art');
const lin = (c) => c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
const srgb = (c) => { c = clamp(c); return c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055; };
const norm3 = (a) => { const l = Math.hypot(...a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
const mix = (a, b, t) => [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
const KEY = norm3([-0.5, 0.62, 0.6]), FILL = norm3([0.7, -0.35, 0.5]);

// signed distance helpers (negative inside), in pixels
export const sdCircle = (x, y, cx, cy, r) => Math.hypot(x - cx, y - cy) - r;
export const sdEllipse = (x, y, cx, cy, rx, ry) => { const k = Math.hypot((x - cx) / rx, (y - cy) / ry); return (k - 1) * Math.min(rx, ry); };
export const sdRound = (x, y, cx, cy, hw, hh, r) => {
  const qx = Math.abs(x - cx) - hw + r, qy = Math.abs(y - cy) - hh + r;
  return Math.hypot(Math.max(qx, 0), Math.max(qy, 0)) + Math.min(Math.max(qx, qy), 0) - r;
};
export const sdCapsule = (x, y, ax, ay, bx, by, r) => {
  const pax = x - ax, pay = y - ay, bax = bx - ax, bay = by - ay;
  const h = clamp((pax * bax + pay * bay) / (bax * bax + bay * bay));
  return Math.hypot(pax - bax * h, pay - bay * h) - r;
};
export const sdPoly = (x, y, pts) => {
  let d = Infinity, s = 1;
  for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
    const [ax, ay] = pts[j], [bx, by] = pts[i];
    const ex = bx - ax, ey = by - ay, wx = x - ax, wy = y - ay;
    const t = clamp((wx * ex + wy * ey) / (ex * ex + ey * ey));
    d = Math.min(d, Math.hypot(wx - ex * t, wy - ey * t));
    const c1 = y >= ay, c2 = y < by, c3 = ex * wy > ey * wx;
    if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
  }
  return s * d;
};

/**
 * shape(x, y) returns null (empty) or {d, R, alb, rough, ao, hd, em, depth}:
 *   d signed distance, R bevel radius in px (rounded profile), alb sRGB 0..1, rough 0..1,
 *   ao 0..1, hd extra height in px, em emissive rgb (linear), depth scales the profile height (default 1).
 */
export function bake(name, W, H, shape, o = {}) {
  const n = W * H, hf = new Float32Array(n), cov = new Float32Array(n), S = new Array(n);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = y * W + x, s = shape(x + 0.5, y + 0.5);
    S[i] = s;
    if (!s) continue;
    cov[i] = clamp(0.5 - s.d);
    const t = clamp(-s.d / s.R), prof = Math.sqrt(Math.max(0, 1 - (1 - t) * (1 - t))) * s.R * (s.depth ?? 1);
    hf[i] = prof + (s.hd || 0);
  }
  // a touch of blur so facets and noise do not alias in the normal map
  const hb = new Float32Array(n);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    let a = 0, w = 0;
    for (let k = -1; k <= 1; k++) for (let j = -1; j <= 1; j++) {
      const xx = Math.min(W - 1, Math.max(0, x + j)), yy = Math.min(H - 1, Math.max(0, y + k)), q = (j === 0 && k === 0) ? 2 : 1;
      a += hf[yy * W + xx] * q; w += q;
    }
    hb[y * W + x] = a / w;
  }
  const H_ = (x, y) => hb[Math.min(H - 1, Math.max(0, y)) * W + Math.min(W - 1, Math.max(0, x))];
  const lit = new Uint8Array(n * 4), alb = new Uint8Array(n * 4), nor = new Uint8Array(n * 4);
  const strength = o.strength ?? 1;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = y * W + x, s = S[i], a8 = Math.round(cov[i] * 255);
    if (!s || a8 === 0) { nor[i * 4] = 128; nor[i * 4 + 1] = 128; nor[i * 4 + 2] = 255; nor[i * 4 + 3] = 255; continue; }
    const gx = (H_(x + 1, y) - H_(x - 1, y)) * 0.5 * strength, gy = (H_(x, y + 1) - H_(x, y - 1)) * 0.5 * strength;
    const N = norm3([-gx, gy, 1]); // y up: a hill's lower half faces down
    const ao = s.ao ?? 1, rough = s.rough ?? 0.5, em = s.em || [0, 0, 0];
    const al = [lin(s.alb[0]), lin(s.alb[1]), lin(s.alb[2])];
    const dk = Math.max(0, N[0] * KEY[0] + N[1] * KEY[1] + N[2] * KEY[2]);
    const df = Math.max(0, N[0] * FILL[0] + N[1] * FILL[1] + N[2] * FILL[2]);
    const Hh = norm3([KEY[0], KEY[1], KEY[2] + 1]);
    const nh = Math.max(0, N[0] * Hh[0] + N[1] * Hh[1] + N[2] * Hh[2]);
    const shin = lerp(220, 14, rough), spec = Math.pow(nh, shin) * lerp(1.2, 0.08, rough) * (o.spec ?? 1);
    const rim = Math.pow(1 - N[2], 2.2) * (o.rim ?? 0.5);
    const hemi = 0.3 + 0.2 * N[1];
    const amb = hemi * ao;
    const col = [0, 0, 0];
    for (let c = 0; c < 3; c++) {
      const key = [1.0, 0.93, 0.8][c] * 1.05, fil = [0.45, 0.6, 1.0][c] * 0.32;
      col[c] = al[c] * (amb * [0.8, 0.9, 1.1][c] + dk * key * ao + df * fil) + spec * ao * [1, 0.97, 0.9][c] + rim * (al[c] * 0.6 + 0.12) * ao + em[c];
    }
    const tone = (v) => srgb(v / (1 + v * 0.18));
    lit[i * 4] = Math.round(tone(col[0]) * 255); lit[i * 4 + 1] = Math.round(tone(col[1]) * 255); lit[i * 4 + 2] = Math.round(tone(col[2]) * 255); lit[i * 4 + 3] = a8;
    alb[i * 4] = Math.round(clamp(s.alb[0] * (0.55 + 0.45 * ao)) * 255); alb[i * 4 + 1] = Math.round(clamp(s.alb[1] * (0.55 + 0.45 * ao)) * 255); alb[i * 4 + 2] = Math.round(clamp(s.alb[2] * (0.55 + 0.45 * ao)) * 255); alb[i * 4 + 3] = a8;
    if (s.em) { for (let c = 0; c < 3; c++) alb[i * 4 + c] = Math.round(srgb(lin(alb[i * 4 + c] / 255) + em[c]) * 255); }
    nor[i * 4] = Math.round((N[0] * 0.5 + 0.5) * 255); nor[i * 4 + 1] = Math.round((N[1] * 0.5 + 0.5) * 255); nor[i * 4 + 2] = Math.round((N[2] * 0.5 + 0.5) * 255); nor[i * 4 + 3] = 255;
  }
  save(join(OUT, name + '.png'), W, H, 4, lit);
  save(join(OUT, name + '_a.png'), W, H, 4, alb);
  save(join(OUT, name + '_n.png'), W, H, 4, nor);
}

// ── the party characters ──────────────────────────────────────────────
const PCOL = [[0.93, 0.28, 0.30], [0.25, 0.55, 0.95], [0.30, 0.80, 0.45], [0.98, 0.78, 0.22]];
const NAMES = ['ruby', 'sky', 'mint', 'sun'];
const dark = (c, k) => [c[0] * k, c[1] * k, c[2] * k];

function blobBody(p) {
  const c = PCOL[p];
  return (x, y) => {
    // a soft bean: wider at the bottom, with a cream belly
    const top = sdEllipse(x, y, 128, 118, 106, 100), bot = sdEllipse(x, y, 128, 150, 118, 82), kk = 0.5 + 0.5 * clamp((bot - top) / 40 + 0.5) * 0 , hh = clamp(0.5 + 0.5 * (bot - top) / 36), d = lerp(bot, top, hh) - 18 * hh * (1 - hh);
    if (d > 1) return null;
    const u = x / 256, v = y / 256;
    const velvet = fbm(u, v, 24, 3, 3 + p) - 0.5;
    const belly = smooth(1.0, 0.6, Math.hypot((x - 128) / 62, (y - 168) / 50));
    const edge = smooth(0, 40, -d);
    let alb = mix(dark(c, 0.92), [Math.min(1, c[0] * 1.15 + 0.08), Math.min(1, c[1] * 1.15 + 0.08), Math.min(1, c[2] * 1.15 + 0.08)], smooth(230, 60, y) * 0.5);
    alb = mix(alb, [0.99, 0.93, 0.82], belly * 0.82);
    return { d, R: 100, alb, rough: 0.32 + 0.25 * belly, ao: 0.7 + 0.3 * edge + (y > 200 ? -0.1 * smooth(200, 245, y) : 0), hd: velvet * 1.6, depth: 0.55 };
  };
}
const eyeShape = (x, y) => {
  const d = sdEllipse(x, y, 32, 32, 26, 29);
  if (d > 1) return null;
  return { d, R: 24, alb: [0.97, 0.97, 1], rough: 0.08, ao: 0.8 + 0.2 * smooth(0, 10, -d), depth: 0.6 };
};
const pupilShape = (x, y) => {
  const d = sdCircle(x, y, 32, 32, 26);
  if (d > 1) return null;
  const hl = smooth(7, 3, Math.hypot(x - 24, y - 22)), hl2 = smooth(4, 1.5, Math.hypot(x - 40, y - 40)) * 0.6;
  const w = Math.max(hl, hl2);
  return { d, R: 24, alb: mix([0.05, 0.05, 0.09], [1, 1, 1], w), rough: 0.05, ao: 1, depth: 0.7, em: [w * 0.6, w * 0.6, w * 0.6] };
};

export function bakeCharacters() {
  for (let p = 0; p < 4; p++) {
    const c = PCOL[p];
    bake(`blob_${NAMES[p]}`, 256, 256, blobBody(p), { rim: 0.6 });
    bake(`foot_${NAMES[p]}`, 96, 56, (x, y) => { const d = sdEllipse(x, y, 48, 30, 42, 22); return d > 1 ? null : { d, R: 20, alb: dark(c, 0.7), rough: 0.4, ao: 0.8 + 0.2 * smooth(0, 10, -d), depth: 0.8 }; });
    bake(`hand_${NAMES[p]}`, 64, 64, (x, y) => { const d = sdCircle(x, y, 32, 32, 27); return d > 1 ? null : { d, R: 27, alb: dark(c, 0.95), rough: 0.35, ao: 0.85 + 0.15 * smooth(0, 10, -d) }; });
  }
  bake('eye', 64, 64, eyeShape, { spec: 1.6 });
  bake('pupil', 64, 64, pupilShape, { spec: 1.2 });
  bake('antenna', 48, 48, (x, y) => { const d = sdCircle(x, y, 24, 24, 20); return d > 1 ? null : { d, R: 20, alb: [0.55, 1, 0.65], rough: 0.2, em: [0.25, 0.9, 0.35] }; });
  bake('cheek', 64, 40, (x, y) => { const d = sdEllipse(x, y, 32, 20, 28, 15); return d > 1 ? null : { d, R: 1, alb: [1, 0.5, 0.55], rough: 1, depth: 0.01 }; }, { spec: 0, rim: 0 });
}

// ── props ─────────────────────────────────────────────────────────────
/** a rounded plate with a bevel; coloured by fn(x, y) -> sRGB, with optional wear */
function plate(name, W, H, r, bevel, colorFn, o = {}) {
  bake(name, W, H, (x, y) => {
    const d = sdRound(x, y, W / 2, H / 2, W / 2 - 1, H / 2 - 1, r);
    if (d > 1) return null;
    const u = x / W, v = y / H;
    const wear = fbm(u, v, 6, 4, o.seed ?? 5);
    const base = colorFn(x, y, u, v, d, wear);
    return { d, R: bevel, alb: base.alb ?? base, rough: o.rough ?? 0.45, ao: 0.78 + 0.22 * smooth(0, bevel * 1.2, -d), hd: (wear - 0.5) * (o.grit ?? 2) + (base.hd ?? 0), em: base.em, depth: o.depth ?? 0.7 };
  }, o);
}

export function bakeProps() {
  // hurdle rocks: faceted boulders from cells
  for (let k = 0; k < 2; k++) {
    const W = k ? 128 : 112, H = k ? 192 : 112;
    bake(k ? 'rock_tall' : 'rock', W, H, (x, y) => {
      const pts = k ? [[18, H - 6], [10, 70], [30, 18], [62, 4], [96, 22], [112, 74], [108, H - 6]] : [[6, H - 4], [10, 60], [34, 14], [66, 8], [96, 38], [108, H - 4]];
      const d = sdPoly(x, y, pts) - 6;
      if (d > 1) return null;
      const [f1, f2, id] = cells(x / W, y / H, 5, 11 + k, 1);
      const crack = smooth(0.0, 0.06, f2 - f1);
      const moss = smooth(0.62, 0.8, fbm(x / W, y / H, 3, 3, 7)) * smooth(60, 0, y - (H - 70));
      const g = 0.5 + 0.2 * id + 0.12 * fbm(x / W, y / H, 12, 3, 9);
      let alb = mix([0.34 * g + 0.25, 0.3 * g + 0.24, 0.36 * g + 0.26], [0.35, 0.55, 0.22], moss * 0.6);
      return { d, R: 18, alb, rough: 0.9, ao: (0.55 + 0.45 * crack) * (0.8 + 0.2 * smooth(0, 16, -d)), hd: (id - 0.5) * 7 + (crack - 1) * 3, depth: 0.55 };
    }, { rim: 0.35, strength: 1.2 });
  }
  // hanging pad (the bar to duck) and its post
  plate('bar', 160, 56, 22, 18, (x, y, u, v) => {
    const stripe = (Math.floor((x + y) / 22) & 1) ? 1 : 0;
    return stripe ? [0.97, 0.78, 0.18] : [0.14, 0.13, 0.17];
  }, { rough: 0.35, seed: 3 });
  bake('post', 24, 160, (x, y) => { const d = sdRound(x, y, 12, 80, 8, 80, 6); return d > 1 ? null : { d, R: 8, alb: [0.55, 0.57, 0.66], rough: 0.3, ao: 0.9, depth: 1 }; });
  // glossy gems for Beat Box notes and big dome pads for Echo, one per player colour
  for (let p = 0; p < 4; p++) {
    const c = PCOL[p];
    bake(`gem_${NAMES[p]}`, 96, 96, (x, y) => {
      const d = sdCircle(x, y, 48, 48, 44);
      if (d > 1) return null;
      const ring = smooth(30, 38, Math.hypot(x - 48, y - 48));
      return { d, R: 36, alb: mix(c, [1, 1, 1], 0.12 + 0.35 * smooth(34, 0, Math.hypot(x - 40, y - 38))), rough: 0.12, ao: 0.8 + 0.2 * smooth(0, 14, -d), hd: ring * 4, depth: 0.9, em: [lin(c[0]) * 0.12, lin(c[1]) * 0.12, lin(c[2]) * 0.12] };
    }, { spec: 1.4 });
    for (const on of [0, 1]) {
      bake(`pad_${NAMES[p]}${on ? '_on' : ''}`, 192, 192, (x, y) => {
        const d = sdRound(x, y, 96, 96, 90, 90, 30);
        if (d > 1) return null;
        const inner = sdRound(x, y, 96, 96, 70, 70, 20);
        const bez = inner > 0 ? 1 : 0;
        const k = on ? 1 : 0.55;
        const alb = bez ? [0.17, 0.16, 0.22] : mix(dark(c, k), [1, 1, 1], on ? 0.35 : 0.04);
        return { d, R: bez ? 14 : 26, alb, rough: bez ? 0.5 : 0.14, ao: 0.85 + 0.15 * smooth(0, 14, -d), hd: bez ? 0 : 7 + 4 * smooth(0, 20, -inner), depth: 0.8, em: on && !bez ? [lin(c[0]) * 0.9, lin(c[1]) * 0.9, lin(c[2]) * 0.9] : null };
      }, { spec: 1.2 });
    }
  }
  // building blocks for Stack Attack: a candy block that stretches (the bevel is small so stretching reads fine)
  for (let p = 0; p < 6; p++) {
    const c = p < 4 ? PCOL[p] : p === 4 ? [0.62, 0.45, 0.9] : [0.95, 0.55, 0.25];
    plate(`block_${p}`, 128, 128, 22, 14, (x, y, u, v, d) => {
      const stripe = smooth(0.46, 0.5, Math.abs(((u * 4 + v * 0) % 1) - 0.5)) * 0.0;
      const inner = sdRound(x, y, 64, 64, 46, 46, 12);
      const panel = inner < 0 ? 1.0 : 0.82;
      return { alb: mix(dark(c, 0.8 * panel), [1, 1, 1], 0.1 * smooth(80, 0, y)), hd: inner < 0 ? 3 : 0 };
    }, { rough: 0.38, seed: 40 + p, grit: 1.2 });
  }
  // quiz tiles and cards (a nine-slice friendly plate)
  plate('tile', 128, 96, 24, 12, (x, y, u, v) => mix([0.95, 0.96, 1.0], [0.8, 0.84, 0.95], smooth(0.2, 1, v)), { rough: 0.55, seed: 21, grit: 0.8 });
  plate('panel', 128, 128, 26, 10, (x, y, u, v) => mix([0.2, 0.2, 0.34], [0.12, 0.12, 0.22], v), { rough: 0.6, seed: 22, grit: 0.5 });
  // a coin / star for score pops
  bake('star', 96, 96, (x, y) => {
    const pts = []; for (let i = 0; i < 10; i++) { const a = -Math.PI / 2 + i * Math.PI / 5, r = i % 2 ? 22 : 46; pts.push([48 + Math.cos(a) * r, 52 + Math.sin(a) * r]); }
    const d = sdPoly(x, y, pts) - 4;
    return d > 1 ? null : { d, R: 12, alb: [1, 0.82, 0.2], rough: 0.2, ao: 0.9, depth: 1, em: [0.18, 0.12, 0.0] };
  }, { spec: 1.4 });
  // soft round shadow (a dark blob for contact shadows) and a light cone for spotlights
  bake('shadow', 128, 64, (x, y) => { const d = sdEllipse(x, y, 64, 32, 62, 30); const a = smooth(0, 1, 1 - Math.hypot((x - 64) / 62, (y - 32) / 30)); return d > 1 ? null : { d: 0.5 - a * 3, R: 1, alb: [0, 0, 0], rough: 1, depth: 0.001 }; }, { spec: 0, rim: 0 });
}

// ── backdrops: tileable parallax layers (alpha) and a ground strip ─────
function band(name, W, H, fn) {
  const b = new Uint8Array(W * H * 4);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const c = fn(x, y, x / W, y / H); const i = (y * W + x) * 4; b[i] = Math.round(clamp(c[0]) * 255); b[i + 1] = Math.round(clamp(c[1]) * 255); b[i + 2] = Math.round(clamp(c[2]) * 255); b[i + 3] = Math.round(clamp(c[3]) * 255); }
  save(join(OUT, name + '.png'), W, H, 4, b);
  save(join(OUT, name + '_a.png'), W, H, 4, b); // the lit layers load the albedo with a flat normal map
  const nn = new Uint8Array(W * H * 4); for (let i = 0; i < W * H; i++) { nn[i * 4] = 128; nn[i * 4 + 1] = 128; nn[i * 4 + 2] = 255; nn[i * 4 + 3] = 255; }
  save(join(OUT, name + '_n.png'), W, H, 4, nn);
}
export function bakeBackdrops() {
  // a ridge line that tiles: height from 1D tiling noise; colour fades toward the horizon
  const ridge = (name, f, seed, top, bot, snow) => band(name, 1024, 256, (x, y, u, v) => {
    const h = 0.25 + 0.6 * fbm(u, 0.37, f, 4, seed) ;
    const yy = 1 - v;
    if (yy > h) return [0, 0, 0, 0];
    const edge = smooth(h - 0.012, h, yy);
    const k = yy / h;
    const c = mix(bot, top, k);
    const tex = 0.92 + 0.16 * fbm(u * 2, v * 2, 24, 3, seed + 5);
    const sn = snow ? smooth(0.82, 0.9, k) : 0;
    return [...mix(c.map((q) => q * tex), [0.96, 0.97, 1], sn).map((q) => q + edge * 0.08), 1];
  });
  ridge('ridge_far', 3, 71, [0.52, 0.62, 0.86], [0.7, 0.78, 0.94], true);
  ridge('ridge_mid', 5, 33, [0.32, 0.5, 0.6], [0.46, 0.65, 0.66], false);
  ridge('ridge_near', 7, 12, [0.18, 0.36, 0.34], [0.26, 0.5, 0.36], false);
  // soft cloud sprites
  band('cloud', 512, 160, (x, y, u, v) => {
    let a = 0;
    for (const [cx, cy, r] of [[0.22, 0.62, 0.17], [0.4, 0.5, 0.24], [0.6, 0.55, 0.22], [0.78, 0.65, 0.16], [0.5, 0.75, 0.2]]) a = Math.max(a, smooth(1, 0.35, Math.hypot((u - cx) / r, (v - cy) / (r * 1.6))));
    const sh = smooth(0.35, 0.95, v);
    const n = fbm(u, v, 5, 3, 2);
    return [lerp(1, 0.78, sh) , lerp(1, 0.82, sh), lerp(1, 0.94, sh), a * (0.55 + 0.45 * n)];
  });
  // a ground strip (track surface) that tiles horizontally: a worn rubber running track
  band('track', 256, 128, (x, y, u, v) => {
    const g = fbm(u, v, 8, 4, 8), grain = hash2(x, y, 3);
    const lane = smooth(0.0, 0.03, v) * smooth(1, 0.97, v);
    const k = 0.72 + 0.34 * g + 0.1 * (grain - 0.5);
    const seam = smooth(0.012, 0.0, Math.abs(((u * 4) % 1) - 0.0)) + smooth(0.012, 0.0, Math.abs(((u * 4) % 1) - 1));
    const c = [0.78 * k, 0.4 * k, 0.32 * k];
    return [...c.map((q) => q * (1 - 0.35 * seam) * (0.8 + 0.2 * lane)), 1];
  });
  // stars and sparkles for night scenes
  band('sparkle', 64, 64, (x, y, u, v) => { const d = Math.hypot(x - 32, y - 32) / 32; const a = Math.pow(Math.max(0, 1 - d), 3) + Math.pow(Math.max(0, 1 - Math.hypot((x - 32) / 3, (y - 32) / 30)), 2) * 0.8 + Math.pow(Math.max(0, 1 - Math.hypot((x - 32) / 30, (y - 32) / 3)), 2) * 0.8; return [1, 1, 1, clamp(a)]; });
  // a vignette-free radial glow for light halos
  band('glow', 128, 128, (x, y) => { const d = Math.hypot(x - 64, y - 64) / 64; return [1, 1, 1, Math.pow(Math.max(0, 1 - d), 2)]; });
}

// ── decals for the 3D scenes: scorch, splat, oil, tyre skid (RGBA, projected by Decal3D) ──
export function bakeDecals() {
  band('decal_scorch', 256, 256, (x, y, u, v) => {
    const dx = u - 0.5, dy = v - 0.5, r = Math.hypot(dx, dy) * 2, an = Math.atan2(dy, dx);
    const rag = 0.62 + 0.3 * fbm(Math.cos(an) * 0.5 + 0.5, Math.sin(an) * 0.5 + 0.5, 4, 4, 7) + 0.1 * fbm(u, v, 12, 3, 4);
    const body = smooth(rag, rag - 0.22, r), core = smooth(0.5, 0.0, r);
    const streak = Math.pow(Math.max(0, Math.cos(an * 9 + fbm(u, v, 6, 2, 3) * 5)), 6) * smooth(1, 0.2, r) * 0.5;
    const k = 0.04 + 0.05 * fbm(u, v, 20, 3, 2);
    return [k + 0.0, k, k, clamp(body * (0.55 + 0.4 * core) + streak * body)];
  });
  band('decal_ember', 256, 256, (x, y, u, v) => {
    const r = Math.hypot(u - 0.5, v - 0.5) * 2, f = fbm(u, v, 10, 4, 9);
    const hot = smooth(0.35, 0.0, r) * smooth(0.35, 0.65, f);
    return [1, 0.45 + 0.3 * f, 0.08, clamp(hot * 1.2)];
  });
  band('decal_splat', 256, 256, (x, y, u, v) => {
    const dx = u - 0.5, dy = v - 0.5, r = Math.hypot(dx, dy) * 2, an = Math.atan2(dy, dx);
    const rag = 0.5 + 0.35 * Math.pow(fbm(Math.cos(an) * 0.5 + 0.5, Math.sin(an) * 0.5 + 0.5, 6, 3, 5), 2) * 2;
    const drops = smooth(0.06, 0.03, Math.min(...[[0.2, 0.25], [0.78, 0.3], [0.7, 0.8], [0.25, 0.74], [0.5, 0.1]].map(([a, b]) => Math.hypot(u - a, v - b))));
    return [1, 1, 1, clamp(smooth(rag, rag - 0.12, r) + drops)];
  });
  band('decal_ring', 256, 256, (x, y, u, v) => {
    const r = Math.hypot(u - 0.5, v - 0.5) * 2;
    const ring = smooth(0.93, 0.9, r) * smooth(0.8, 0.86, r), fill = smooth(1, 0.0, r) * 0.18;
    const dash = 0.7 + 0.3 * Math.sin(Math.atan2(v - 0.5, u - 0.5) * 24);
    return [1, 0.25, 0.1, clamp(ring * dash + fill)];
  });
  band('decal_foam', 256, 256, (x, y, u, v) => {
    const r = Math.hypot(u - 0.5, v - 0.5) * 2, f = fbm(u, v, 9, 4, 6), g = fbm(u, v, 22, 3, 9);
    const ring = smooth(0.95, 0.7, r) * smooth(0.25, 0.5, r + (f - 0.5) * 0.35);
    const lace = smooth(0.45, 0.7, g);
    return [1, 1, 1, clamp(ring * (0.35 + 0.65 * lace))];
  });
  band('decal_tyre', 128, 256, (x, y, u, v) => {
    const a = smooth(0.2, 0.3, u) * smooth(0.8, 0.7, u), f = fbm(u * 2, v * 3, 4, 3, 6);
    return [0.02, 0.02, 0.02, a * (0.25 + 0.5 * f) * smooth(0, 0.15, v) * smooth(1, 0.8, v)];
  });
}

const MAIN = process.argv[1] && import.meta.url.endsWith(process.argv[1].split('/').pop());
if (MAIN) { bakeCharacters(); bakeProps(); bakeBackdrops(); bakeDecals(); console.log('bake2d ok'); }
