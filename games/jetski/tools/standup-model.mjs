// The stand-up craft: assets/models/standup.glb (and standup_lo.glb, the same craft for distant racers). Called by
// make-models.mjs. Every vertex and every texel from this file.
//
// Forward +z, in the frame the sim floats (view.c stands the node STAND_LIFT higher). A short, chunky, pointed hull,
// 2.25 m by 0.72 m, the nose swept up; mostly a closed, crowned hood over the engine, with a white race-number plate on
// top; the open foot tray only behind the rider's shins, with red sides and a textured rubber mat. Modelled seams where
// the hood meets the hull, a rubber bumper rail along the gunwale, a splash decal on the hood's flanks (a real texture
// with an alpha edge, tinted per racer), a stripe along the gunwale, an intake grate, a ride plate and a jet nozzle under
// the stern. The handlepole is long and thin, hinged on bracket plates at the bow (bolts, a spring strut), a foam pad on
// its top, wide bars with fat grips, bar ends, a throttle lever and a lanyard to the kill switch.
// Nodes: Hull; Plate1..Plate6 (one shows: the racer's number); Pole (turns about x at its pivot, src/rider.c), its child
// Bars (turns about the pole's own up). Sizes are src/rider.c's POLE_* and src/view.c's STAND_*.
import { join } from 'node:path';
import { deflateSync, crc32 } from 'node:zlib';
import { Glb } from './gltf-writer.mjs';

const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x), lerp = (a, b, t) => a + (b - a) * t;
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]], mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
const frame = (d) => { const up = Math.abs(d[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0]; const u = norm(cross(d, up)); return [u, cross(u, d)]; };

export const POLE_PIVOT = [0, 0.86, 0.94], POLE_LEN = 1.2, GRIP = 0.34, TRAY_FLOOR = 0.42;

// ── PNG in memory ──
function png(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4); }
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td) >>> 0); return Buffer.concat([l, td, c]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  return new Uint8Array(Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]));
}
/** paint a w x h RGBA image: f(x, y) in 0..1 (y down) returns [r, g, b, a] 0..1; 4x4 samples a pixel, so edges are crisp and smooth */
function paint(w, h, f) {
  const out = Buffer.alloc(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let r = 0, g = 0, b = 0, a = 0;
    for (let sy = 0; sy < 4; sy++) for (let sx = 0; sx < 4; sx++) { const c = f((x + (sx + 0.5) / 4) / w, (y + (sy + 0.5) / 4) / h); r += c[0] * c[3]; g += c[1] * c[3]; b += c[2] * c[3]; a += c[3]; }
    const k = a > 0 ? 1 / a : 0, o = (y * w + x) * 4;
    out[o] = Math.round(clamp(r * k) * 255), out[o + 1] = Math.round(clamp(g * k) * 255), out[o + 2] = Math.round(clamp(b * k) * 255), out[o + 3] = Math.round((a / 16) * 255);
  }
  return out;
}
const segDist = (px, py, ax, ay, bx, by) => { const ex = bx - ax, ey = by - ay, t = clamp(((px - ax) * ex + (py - ay) * ey) / (ex * ex + ey * ey + 1e-9)); return Math.hypot(px - ax - ex * t, py - ay - ey * t); };
// our own numerals: a bold rounded stroke font, each a list of polylines in a 0..1 box (y down)
const ARC = (cx, cy, rx, ry, a0, a1, n = 14) => Array.from({ length: n + 1 }, (_, i) => { const a = lerp(a0, a1, i / n); return [cx + rx * Math.cos(a), cy + ry * Math.sin(a)]; });
const DIGITS = {
  1: [[[0.32, 0.22], [0.55, 0.08], [0.55, 0.92]], [[0.3, 0.92], [0.78, 0.92]]],
  2: [[...ARC(0.5, 0.3, 0.26, 0.21, Math.PI * 1.05, Math.PI * 2.2), [0.24, 0.92], [0.8, 0.92]]],
  3: [[...ARC(0.5, 0.28, 0.25, 0.19, Math.PI * 1.1, Math.PI * 2.5)], [...ARC(0.5, 0.7, 0.27, 0.21, Math.PI * 1.5, Math.PI * 2.85)]],
  4: [[[0.66, 0.92], [0.66, 0.08], [0.2, 0.66], [0.84, 0.66]]],
  5: [[[0.76, 0.08], [0.3, 0.08], [0.26, 0.46], ...ARC(0.5, 0.66, 0.28, 0.25, Math.PI * 1.22, Math.PI * 2.8)]],
  6: [[[0.68, 0.1], [0.36, 0.42], ...ARC(0.5, 0.66, 0.27, 0.25, Math.PI * 1.0, Math.PI * 3.0)]],
};
/** the number plates: six cells side by side, a white rounded plate with a dark border and the racer's number */
function plateTexture() {
  const W = 1536, Hh = 256;
  return paint(W, Hh, (x, y) => {
    const cell = Math.min(5, Math.floor(x * 6)), cx = x * 6 - cell, n = cell + 1;
    const dx = Math.abs(cx - 0.5) * 2, dy = Math.abs(y - 0.5) * 2, r = 0.22;
    const qx = Math.max(dx - (1 - r), 0), qy = Math.max(dy - (1 - r), 0), outside = Math.hypot(qx, qy) > r;
    if (outside) return [1, 1, 1, 0];
    const edge = Math.hypot(qx, qy) > r - 0.05 || dx > 0.95 || dy > 0.95;
    if (edge) return [0.06, 0.06, 0.07, 1];
    // the numeral, in a box 0.6 wide and 0.78 high in the cell's middle
    const u = (cx - 0.2) / 0.6, v = (y - 0.11) / 0.78;
    let d = 9;
    for (const line of DIGITS[n]) for (let i = 0; i + 1 < line.length; i++) d = Math.min(d, segDist(u, v, line[i][0], line[i][1], line[i + 1][0], line[i + 1][1]));
    return d < 0.115 ? [0.04, 0.04, 0.05, 1] : [0.97, 0.97, 0.95, 1];
  });
}
/** the splash decal: a crested wave with spray, white with an alpha edge (the racer's colour tints it) */
function splashTexture() {
  return paint(512, 256, (x, y) => {
    const crest = 0.45 + 0.16 * Math.sin(x * 9.0 + 0.6) * (1 - x * 0.4) + 0.07 * Math.sin(x * 23.0) - 0.18 * smooth(0.65, 1, x);
    let inside = y > crest && y < 0.97 - 0.12 * x;
    const curl = Math.hypot((x - 0.18) * 2, y - 0.4) < 0.11 && !(Math.hypot((x - 0.2) * 2, y - 0.43) < 0.065);
    let drop = false;
    for (let k = 0; k < 7; k++) { const dx = 0.25 + k * 0.1, dy = 0.3 - 0.03 * k + 0.05 * Math.sin(k * 2.1); if (Math.hypot((x - dx) * 2, y - dy) < 0.035 - k * 0.002) drop = true; }
    return [1, 1, 1, inside || curl || drop ? 1 : 0];
  });
}
/** the tray mat: a raised diamond grip, mid grey (the material's colour darkens it), tiled */
function matTexture() {
  return paint(128, 128, (x, y) => {
    const u = (x * 8) % 1, v = (y * 8) % 1, d = Math.abs(u - 0.5) + Math.abs(v - 0.5);
    const g = d < 0.36 ? 0.62 : d < 0.42 ? 0.4 : 0.28;
    return [g, g, g, 1];
  });
}

class Plain {
  constructor() { this.g = new Map(); }
  group(mat) { if (!this.g.has(mat)) this.g.set(mat, { pos: [], nrm: [], uv: [], idx: [] }); return this.g.get(mat); }
  grid(mat, P, o = {}) {
    const g = this.group(mat), base = g.pos.length / 3, R = P.length, Cc = P[0].length;
    for (let r = 0; r < R; r++) for (let c = 0; c < Cc; c++) {
      const a = P[Math.max(0, r - 1)][c], b = P[Math.min(R - 1, r + 1)][c];
      const cc = o.wrap ? (x) => (x + Cc - 1) % (Cc - 1) : (x) => clamp(x, 0, Cc - 1);
      let n = norm(cross(sub(b, a), sub(P[r][cc(c + 1)], P[r][cc(c - 1)])));
      if (o.flip) n = mul(n, -1);
      g.pos.push(...P[r][c]); g.nrm.push(...n);
      g.uv.push(o.uv ? o.uv(r / (R - 1), c / (Cc - 1))[0] : c / (Cc - 1), o.uv ? o.uv(r / (R - 1), c / (Cc - 1))[1] : r / (R - 1));
    }
    for (let r = 0; r + 1 < R; r++) for (let c = 0; c + 1 < Cc; c++) {
      const A = base + r * Cc + c, B = A + 1, D = A + Cc, E = D + 1;
      if (o.flip) g.idx.push(A, B, D, B, E, D); else g.idx.push(A, D, B, B, D, E);
    }
  }
  tube(mat, pts, rad, n = 10, o = {}) {
    const rows = [];
    for (let i = 0; i < pts.length; i++) {
      const dir = norm(sub(pts[Math.min(i + 1, pts.length - 1)], pts[Math.max(i - 1, 0)]));
      const [U, V] = o.frame || frame(dir), r = typeof rad === 'function' ? rad(i / (pts.length - 1)) : rad, row = [];
      for (let k = 0; k <= n; k++) { const a = (k % n) / n * Math.PI * 2 + (o.phase ?? 0); row.push(add(pts[i], add(mul(U, Math.cos(a) * r), mul(V, Math.sin(a) * r * (o.ratio ?? 1))))); }
      rows.push(row);
    }
    if (o.cap !== false) { rows.unshift(rows[0].map(() => pts[0])); rows.push(rows[rows.length - 1].map(() => pts[pts.length - 1])); }
    this.grid(mat, rows, { wrap: true, flip: o.flip ?? true });
  }
  /** a bar along -z of a flattened oval section, from z0 to z1, half sizes hx, hy, middle at height yc */
  bar(mat, z0, z1, hx, hy, yc, n = 16) { this.tube(mat, [[0, yc, z0], [0, yc, z1]], 1, n, { frame: [[hx * 1.08, 0, 0], [0, hy * 1.08, 0]], phase: Math.PI / n }); }
  prims(mi) { return [...this.g].map(([k, g]) => [g, mi[k]]); }
  tris() { let t = 0; for (const g of this.g.values()) t += g.idx.length / 3; return t; }
}
function merged(prims) {
  const by = new Map();
  for (const [g, mi] of prims) {
    if (!by.has(mi)) { by.set(mi, { pos: [...g.pos], nrm: [...g.nrm], uv: [...g.uv], idx: [...g.idx] }); continue; }
    const t = by.get(mi), base = t.pos.length / 3;
    t.pos.push(...g.pos); t.nrm.push(...g.nrm); t.uv.push(...g.uv); for (const k of g.idx) t.idx.push(k + base);
  }
  return [...by].map(([mi, g]) => [g, mi]);
}

/** detail 1: the race model; 0.4: the distant one (fewer rings, no small parts) */
function build(OUT, file, detail) {
  const P = new Plain(), plates = [];
  const Z0 = -1.0, Z1 = 1.25, UT = (-0.22 - Z0) / (Z1 - Z0);
  const sec = (u) => {
    const bow = smooth(0.5, 1.0, u), sb = smooth(0.0, 0.03, u);
    const beam = (0.37 * (1 - Math.pow(bow, 1.35)) + 0.03) * lerp(0.94, 1, sb);
    const rise = Math.pow(smooth(0.45, 1.0, u), 1.8);
    return { z: lerp(Z0, Z1, u), beam, keel: 0.06 + 0.52 * rise, chine: 0.2 + 0.4 * rise, gun: 0.55 + 0.17 * Math.pow(smooth(0.45, 1, u), 1.3) };
  };
  const US = Math.round(96 * detail), NT = Math.max(8, Math.round(32 * detail));
  // the sides: gunwale stripe (racer's colour), white hull, chine
  const sidePt = (u, sg, f) => { const s = sec(u); const bulge = Math.sin(f * Math.PI) * 0.018; return [sg * (s.beam * lerp(1, 1.035, f) + bulge), lerp(s.gun, s.chine, f), s.z]; };
  const band = (mat, f0, f1, rows = 4) => { for (const sg of [-1, 1]) { const R = []; for (let k = 0; k <= US; k++) { const u = k / US, row = []; for (let j = 0; j <= rows; j++) row.push(sidePt(u, sg, lerp(f0, f1, j / rows))); R.push(row); } P.grid(mat, R, { flip: sg > 0 }); } };
  band('deck', 0.0, 0.16, 2); band('panel', 0.16, 1.0, Math.max(3, Math.round(8 * detail)));
  // the V bottom (both faces: seen from beside the bow it must not vanish)
  { const NB = Math.max(4, Math.round(10 * detail)), R = [];
    for (let k = 0; k <= US; k++) { const s = sec(k / US), row = []; for (let j = 0; j <= NB; j++) { const t = j / NB * 2 - 1, a = Math.abs(t); row.push([t * s.beam * 1.035, lerp(s.keel, s.chine, Math.pow(a, 0.85)), s.z]); } R.push(row); }
    P.grid('panel', R, { flip: true }); P.grid('panel', R, { flip: false }); }
  // the deck: tray floor, its red sides up to the rails; the hood: a steep face, then a crowned top with rounded shoulders
  const H = (u) => lerp(TRAY_FLOOR, 0.9, smooth(UT, UT + 0.07, u)) + 0.03 * smooth(UT + 0.07, 0.6, u) - 0.1 * smooth(0.78, 1, u);
  const deckPt = (u, t) => {
    const s = sec(u), a = Math.abs(t), rail = s.gun + 0.03 * smooth(0.82, 0.96, a);
    const inner = 1 - smooth(0.68, 0.84, a);
    if (u < UT) return [t * s.beam, lerp(rail, TRAY_FLOOR, inner * smooth(0.008, 0.03, u)), s.z];
    const h = H(u);
    if (h <= s.gun) return [t * s.beam, lerp(rail, h, inner), s.z];
    const crown = Math.pow(Math.cos(Math.min(1, a) * Math.PI / 2), 0.32); /* a broad, flat-topped hood with rounded shoulders */
    return [t * s.beam * lerp(1, 0.97, crown), s.gun + 0.03 + (h - s.gun - 0.03) * crown, s.z];
  };
  const region = (mat, u0, u1, t0, t1, nu, nt, o = {}) => { const R = []; for (let k = 0; k <= nu; k++) { const u = lerp(u0, u1, k / nu), row = []; for (let j = 0; j <= nt; j++) { const p = deckPt(u, lerp(t0, t1, j / nt)); row.push(o.lift ? [p[0] + o.lift[0], p[1] + o.lift[1], p[2]] : p); } R.push(row); } P.grid(mat, R, o); };
  const nTray = Math.max(6, Math.round(20 * detail));
  region('mat', 0.0, UT, -0.7, 0.7, nTray, Math.round(NT * 0.6), { uv: (r, c) => [c * 3, r * 4] });
  for (const sg of [-1, 1]) region('deck', 0.0, UT + 0.01, sg > 0 ? 0.7 : -1, sg > 0 ? 1 : -0.7, nTray, Math.max(3, Math.round(8 * detail)));
  region('hood', UT, UT + 0.09, -1, 1, Math.max(4, Math.round(12 * detail)), NT);
  region('hood', UT + 0.09, 1.0, -1, 1, Math.round(US * 0.7), NT);
  // the seam where the hood meets the hull: a dark groove along each side
  if (detail > 0.5) for (const sg of [-1, 1]) { const line = []; for (let k = 0; k <= 40; k++) { const u = lerp(UT + 0.02, 0.97, k / 40), s = sec(u); line.push([sg * (s.beam * 0.995 + 0.002), s.gun + 0.03, s.z]); } P.tube('seam', line, 0.0045, 6); }
  // the rubber bumper rail along the gunwale, round the stern
  { const rail = []; const n = Math.round(60 * detail);
    for (let k = 0; k <= n; k++) { const u = lerp(0.96, 0.004, k / n), s = sec(u); rail.push([s.beam + 0.016, s.gun - 0.01, s.z]); }
    for (let k = 0; k <= 8; k++) { const t = lerp(1, -1, k / 8); rail.push([t * (sec(0).beam + 0.016), sec(0).gun - 0.01, Z0 - 0.016]); }
    for (let k = 0; k <= n; k++) { const u = lerp(0.004, 0.96, k / n), s = sec(u); rail.push([-(s.beam + 0.016), s.gun - 0.01, s.z]); }
    P.tube('rubber', rail, 0.024, Math.max(6, Math.round(10 * detail)), { ratio: 0.75, cap: false }); }
  // the splash decal on each flank of the hood: a patch over the surface, a few millimetres out, tinted per racer
  for (const sg of [-1, 1]) region('decal', 0.52, 0.93, sg * 0.45, sg * 1.0, Math.round(26 * detail) + 2, Math.max(4, Math.round(10 * detail)), { lift: [sg * 0.007, 0.005], flip: sg < 0, uv: (r, c) => [sg > 0 ? 1 - r : r, 1 - c] });
  // the number plate on the hood's top: six of them, the racer's shows
  for (let n = 0; n < 6; n++) { const pp = new Plain(); const R = []; for (let k = 0; k <= 24; k++) { const u = lerp(0.52, 0.8, k / 24), row = []; for (let j = 0; j <= 16; j++) { const p = deckPt(u, lerp(-0.6, 0.6, j / 16)); row.push([p[0], p[1] + 0.012, p[2]]); } R.push(row); } pp.grid('plate', R, { uv: (r, c) => [(n + c) / 6, r] }); /* (its top toward the stern: it reads from ahead and from the side) */ plates.push(pp); }
  // the stern: its face, the jet nozzle, the ride plate and the intake grate under the hull
  const s0 = sec(0);
  { const row0 = []; for (let j = 0; j <= 12; j++) row0.push(deckPt(0, j / 12 * 2 - 1)); const row1 = row0.map(([x, , z]) => { const ax = Math.abs(x / s0.beam); return [x, lerp(s0.chine, s0.keel, 1 - Math.pow(ax, 0.85)), z]; }); P.grid('transom', [row1, row0]); }
  const NN = Math.max(10, Math.round(24 * detail));
  P.tube('metal', [[0, 0.17, -0.98], [0, 0.165, -1.13]], (t) => lerp(0.085, 0.07, t), NN, { cap: false });
  P.tube('dark', [[0, 0.165, -1.1], [0, 0.165, -1.135]], 0.055, NN);
  if (detail > 0.5) P.tube('metal', [[0, 0.165, -1.128], [0, 0.165, -1.142]], (t) => lerp(0.074, 0.07, t), NN, { cap: false });
  P.grid('metal', [[[-0.13, 0.065, -1.12], [0.13, 0.065, -1.12]], [[-0.13, 0.068, -0.86], [0.13, 0.068, -0.86]]], { flip: false });
  P.grid('metal', [[[-0.13, 0.065, -1.12], [0.13, 0.065, -1.12]], [[-0.13, 0.068, -0.86], [0.13, 0.068, -0.86]]], { flip: true });
  if (detail > 0.5) { P.grid('dark', [[[-0.11, 0.072, -0.82], [0.11, 0.072, -0.82]], [[-0.11, 0.08, -0.5], [0.11, 0.08, -0.5]]], { flip: true }); for (let k = 0; k < 7; k++) { const x = -0.09 + k * 0.03; P.tube('metal', [[x, 0.066, -0.81], [x, 0.074, -0.51]], 0.006, 6); } }
  // the pole's hinge at the bow: two bracket plates, a bolt through, a spring strut's foot
  const [, PY, PZ] = POLE_PIVOT;
  for (const sg of [-1, 1]) { P.grid('metal', [[[sg * 0.062, PY - 0.1, PZ - 0.07], [sg * 0.062, PY - 0.1, PZ + 0.07]], [[sg * 0.062, PY + 0.04, PZ - 0.05], [sg * 0.062, PY + 0.04, PZ + 0.05]]], { flip: sg < 0 }); P.grid('metal', [[[sg * 0.062, PY - 0.1, PZ - 0.07], [sg * 0.062, PY - 0.1, PZ + 0.07]], [[sg * 0.062, PY + 0.04, PZ - 0.05], [sg * 0.062, PY + 0.04, PZ + 0.05]]], { flip: sg > 0 }); }
  P.tube('metal', [[-0.085, PY, PZ], [0.085, PY, PZ]], 0.014, 12);
  if (detail > 0.5) for (const sg of [-1, 1]) P.tube('dark', [[sg * 0.07, PY, PZ], [sg * 0.088, PY, PZ]], 0.022, 6);
  // the pole: thin (6 by 5 cm), long, the racer's colour; a foam pad on its top near the bars; the spring strut under it
  const pole = new Plain(), bars = new Plain(), L = POLE_LEN;
  pole.bar('deck', 0.05, -L + 0.03, 0.03, 0.025, 0, Math.max(8, Math.round(16 * detail)));
  pole.bar('pad', -L + 0.4, -L + 0.06, 0.042, 0.022, 0.04, Math.max(8, Math.round(16 * detail)));
  if (detail > 0.5) pole.tube('metal', [[0, -0.03, -0.18], [0, -0.16, -0.36]], 0.011, 8);
  // the bars: a clamp, a wide bar, fat yellow grips, bar ends, a throttle lever under the right grip, the kill switch and its lanyard
  const NB2 = Math.max(6, Math.round(12 * detail));
  bars.tube('dark', [[0, -0.03, 0.03], [0, 0.04, 0]], 0.03, NB2);
  bars.tube('metal', [[-GRIP - 0.08, 0.075, 0.03], [-0.17, 0.05, 0.0], [0, 0.045, 0.0], [0.17, 0.05, 0.0], [GRIP + 0.08, 0.075, 0.03]], 0.012, NB2);
  for (const sg of [-1, 1]) {
    bars.tube('grip', [[sg * (GRIP - 0.07), 0.066, 0.016], [sg * (GRIP + 0.07), 0.078, 0.028]], (t) => 0.02 + 0.003 * Math.sin(t * Math.PI), NB2);
    bars.tube('dark', [[sg * (GRIP + 0.072), 0.078, 0.028], [sg * (GRIP + 0.095), 0.08, 0.03]], 0.022, NB2);
  }
  if (detail > 0.5) {
    bars.tube('dark', [[-(GRIP - 0.09), 0.06, 0.02], [-(GRIP - 0.03), 0.035, 0.07], [-(GRIP + 0.03), 0.04, 0.075]], 0.008, 6); /* the throttle lever, under the right grip */
    bars.tube('dark', [[0.08, 0.06, 0.02], [0.12, 0.06, 0.02]], 0.02, 8); /* the kill switch */
    const lan = []; for (let k = 0; k <= 16; k++) { const t = k / 16; lan.push([0.1 + 0.05 * Math.sin(t * 9), 0.05 - 0.22 * Math.sin(t * Math.PI) - 0.02 * t, 0.02 - 0.12 * t]); }
    bars.tube('lanyard', lan, 0.005, 6);
  }
  const g = new Glb();
  const tPlate = g.texBytes('standup_plates.png', png(1536, 256, plateTexture()), true);
  const tSplash = g.texBytes('standup_splash.png', png(512, 256, splashTexture()), true);
  const tMat = g.texBytes('standup_mat.png', png(128, 128, matTexture()));
  const M = (name, rough, metal, color, extra = {}) => { const i = g.material(name, rough, metal); Object.assign(g.materials[i], extra); g.materials[i].pbrMetallicRoughness.baseColorFactor = color; return i; };
  const mi = {
    hood: M('hood', 0.35, 0, [0.95, 0.95, 0.93, 1]), panel: M('panel', 0.38, 0, [0.95, 0.95, 0.93, 1]), deck: M('deck', 0.38, 0, [0.88, 0.1, 0.06, 1]),
    transom: M('trim', 0.5, 0, [0.1, 0.1, 0.11, 1]), seam: M('seam', 0.7, 0, [0.05, 0.05, 0.055, 1]), dark: M('dark', 0.6, 0, [0.06, 0.06, 0.07, 1]),
    rubber: M('rubber', 0.85, 0, [0.04, 0.04, 0.045, 1]), metal: M('metal', 0.3, 0.9, [0.6, 0.62, 0.64, 1]), pad: M('pad', 0.9, 0, [0.05, 0.05, 0.06, 1]),
    grip: M('grip', 0.75, 0, [1.0, 0.82, 0.05, 1]), lanyard: M('lanyard', 0.6, 0, [0.9, 0.12, 0.1, 1]),
    mat: M('mat', 0.9, 0, [0.16, 0.16, 0.17, 1]), decal: M('decal', 0.4, 0, [0.2, 0.25, 0.85, 1], { alphaMode: 'MASK', alphaCutoff: 0.5 }),
    plate: M('plate', 0.4, 0, [1, 1, 1, 1]),
  };
  g.materials[mi.mat].pbrMetallicRoughness.baseColorTexture = { index: tMat };
  g.materials[mi.decal].pbrMetallicRoughness.baseColorTexture = { index: tSplash };
  g.materials[mi.plate].pbrMetallicRoughness.baseColorTexture = { index: tPlate };
  const hullN = g.node({ name: 'Hull', mesh: g.mesh('standup_hull', merged(P.prims(mi))) });
  const plateN = plates.map((pp, n) => g.node({ name: 'Plate' + (n + 1), mesh: g.mesh('standup_plate' + (n + 1), merged(pp.prims(mi))) }));
  const barN = g.node({ name: 'Bars', mesh: g.mesh('standup_bars', merged(bars.prims(mi))), translation: [0, 0, -L] });
  const poleN = g.node({ name: 'Pole', mesh: g.mesh('standup_pole', merged(pole.prims(mi))), translation: POLE_PIVOT, children: [barN] });
  g.sceneNodes.push(hullN, ...plateN, poleN);
  g.write(join(OUT, file));
  return P.tris() + plates[0].tris() + pole.tris() + bars.tris();
}
export function buildStandup(OUT) {
  const hi = build(OUT, 'standup.glb', 1), lo = build(OUT, 'standup_lo.glb', 0.4);
  console.log('standup', hi, 'tris; far', lo);
}
