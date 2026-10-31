// The craft, the rider, buoys and palms, every vertex from our own code: assets/models/*.glb
//   node games/jetski/tools/make-models.mjs
// The rider is skinned on a bind pose with identity bone rotations: src/rider.c poses every bone each frame.
import { mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb } from './gltf-writer.mjs';
import { SkinMesh, sub, add, mul, norm, cross, frame } from './charmesh.mjs';
const OUT = join(resolve(dirname(fileURLToPath(import.meta.url)), '..'), 'assets/models');
mkdirSync(OUT, { recursive: true });
const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x), lerp = (a, b, t) => a + (b - a) * t;
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };

// ── rider ──
const BONES = ['Hips', 'Spine', 'Chest', 'Neck', 'Head', 'ArmL', 'ForeL', 'HandL', 'ArmR', 'ForeR', 'HandR', 'ThighL', 'ShinL', 'FootL', 'ThighR', 'ShinR', 'FootR'];
const PARENT = { Hips: null, Spine: 'Hips', Chest: 'Spine', Neck: 'Chest', Head: 'Neck', ArmL: 'Chest', ForeL: 'ArmL', HandL: 'ForeL', ArmR: 'Chest', ForeR: 'ArmR', HandR: 'ForeR', ThighL: 'Hips', ShinL: 'ThighL', FootL: 'ShinL', ThighR: 'Hips', ShinR: 'ThighR', FootR: 'ShinR' };
const T = { Hips: [0, 0.98, 0], Spine: [0, 0.14, 0], Chest: [0, 0.2, 0], Neck: [0, 0.27, 0], Head: [0, 0.1, 0], ArmL: [0.2, 0.2, 0], ForeL: [0.29, 0, 0], HandL: [0.26, 0, 0], ArmR: [-0.2, 0.2, 0], ForeR: [-0.29, 0, 0], HandR: [-0.26, 0, 0], ThighL: [0.1, -0.06, 0], ShinL: [0, -0.44, 0], FootL: [0, -0.43, 0], ThighR: [-0.1, -0.06, 0], ShinR: [0, -0.44, 0], FootR: [0, -0.43, 0] };
const BI = Object.fromEntries(BONES.map((b, i) => [b, i]));
const W = {}; for (const b of BONES) W[b] = PARENT[b] ? add(W[PARENT[b]], T[b]) : T[b];
const m = new SkinMesh();
const XA = [1, 0, 0], ZA = [0, 0, 1];
// a limb from a bone's head through its end, blending into the next bone over the joint
function limb(mat, chain, radii, o = {}) {
  const rings = []; const n = chain.length;
  for (let i = 0; i < n - 1; i++) {
    const a = W[chain[i]], b = W[chain[i + 1]], steps = o.steps ?? 6;
    for (let k = i ? 1 : 0; k <= steps; k++) {
      const t = k / steps, c = add(a, mul(sub(b, a), t)), ra = radii[i], rb = radii[i + 1];
      const bulge = 1 + (o.muscle ?? 0.12) * Math.sin(t * Math.PI) ;
      const r = lerp(ra, rb, t) * bulge;
      rings.push({ c, r, i, t });
    }
  }
  const dir = norm(sub(W[chain[n - 1]], W[chain[0]]));
  const [U, V] = frame(dir);
  const full = [];
  const r0 = rings[0], rl = rings[rings.length - 1];
  if (!o.flat) {
    full.push({ c: sub(r0.c, mul(dir, r0.r * 0.6)), U, V, ru: 0.002, rv: 0.002, i: r0.i, t: 0 });
    full.push({ c: sub(r0.c, mul(dir, r0.r * 0.3)), U, V, ru: r0.r * 0.8, rv: r0.r * 0.8, i: r0.i, t: 0 });
  }
  for (const q of rings) full.push({ c: q.c, U, V, ru: q.r, rv: q.r * (o.ratio ?? 1), i: q.i, t: q.t });
  if (!o.flat) {
    full.push({ c: add(rl.c, mul(dir, rl.r * 0.3)), U, V, ru: rl.r * 0.8, rv: rl.r * 0.8, i: rl.i, t: 1 });
    full.push({ c: add(rl.c, mul(dir, rl.r * 0.6)), U, V, ru: 0.002, rv: 0.002, i: rl.i, t: 1 });
  }
  m.loft(mat, full, { n: o.n ?? 12, arc: o.arc, weights: (k) => {
    const q = full[k], cur = BI[chain[q.i]], nxt = q.i + 1 < n - 1 ? BI[chain[q.i + 1]] : cur;
    const w = smooth(0.78, 1.0, q.t);
    if (q.t < 0.22 && q.i > 0) { const pw = 1 - smooth(0, 0.22, q.t); return [[BI[chain[q.i - 1]], pw * 0.5], [cur, 1 - pw * 0.5]]; }
    return [[cur, 1 - w * 0.5], [nxt, w * 0.5]];
  } });
}
// the torso: rings along y with soft weights
function torso(mat, grow, ys) {
  const rings = ys.map(([y, rx, rz, b, zo]) => ({ c: [0, y, zo ?? 0], U: XA, V: ZA, ru: rx + grow, rv: rz + grow, e: 2.6, b }));
  m.loft(mat, rings, { n: 22, weights: (k) => rings[k].b });
}
const H = W.Hips[1], S = W.Spine[1], C = W.Chest[1], N = W.Neck[1];
const hip = BI.Hips, sp = BI.Spine, ch = BI.Chest, ne = BI.Neck;
// the torso's outline, top to bottom of a real body: y, half width, half depth, how far forward (+z), weights
const BODY = [[H - 0.13, 0.002, 0.002, -0.01, [[hip, 1]]], [H - 0.115, 0.09, 0.08, -0.012, [[hip, 1]]], [H - 0.07, 0.15, 0.112, -0.022, [[hip, 1]]], [H - 0.01, 0.163, 0.118, -0.018, [[hip, 1]]], [H + 0.07, 0.148, 0.102, -0.002, [[hip, 0.8], [sp, 0.2]]], [S + 0.02, 0.132, 0.092, 0.006, [[hip, 0.25], [sp, 0.75]]], [S + 0.12, 0.138, 0.096, 0.012, [[sp, 0.7], [ch, 0.3]]], [C + 0.03, 0.158, 0.106, 0.016, [[ch, 1]]], [C + 0.14, 0.178, 0.113, 0.02, [[ch, 1]]], [C + 0.215, 0.168, 0.098, 0.006, [[ch, 1]]], [C + 0.26, 0.095, 0.072, -0.004, [[ch, 1]]], [N + 0.002, 0.056, 0.056, 0, [[ch, 0.6], [ne, 0.4]]], [N + 0.08, 0.05, 0.052, 0.006, [[ne, 1]]], [N + 0.11, 0.002, 0.002, 0.006, [[ne, 1]]]];
const bodyAt = (y) => { let k = 0; while (k < BODY.length - 2 && BODY[k + 1][0] < y) k++; const A = BODY[k], B = BODY[k + 1], t = clamp((y - A[0]) / (B[0] - A[0])), mixW = t < 0.5 ? A[4] : B[4]; return { rx: lerp(A[1], B[1], t), rz: lerp(A[2], B[2], t), zo: lerp(A[3], B[3], t), w: mixW }; };
{
  const rings = BODY.map(([y, rx, rz, zo, b]) => ({ c: [0, y, zo], U: XA, V: ZA, ru: rx, rv: rz, e: 2.4, b, mod: (an) => 1 + 0.018 * Math.cos(an * 2) - 0.012 * Math.pow(Math.max(0, -Math.sin(an)), 8) }));
  m.loft('suit', rings, { n: 28, weights: (k) => rings[k].b });
}
// the buoyancy vest: foam ribs around the body, deeper at the back, a channel down the spine, cut in at the sides, a yoke over
// the shoulders in a darker cloth, and buckled straps
{
  const y0 = S - 0.05, y1 = C + 0.2, n = 36, rings = [];
  for (let k = 0; k <= n; k++) {
    const y = lerp(y0, y1, k / n), q = bodyAt(y), ribs = 1 + 0.045 * Math.pow(Math.abs(Math.sin((y - y0) * 25.1)), 0.6);
    const end = Math.min(1, Math.min(k, n - k) / 1.2);
    rings.push({ c: [0, y, q.zo], U: XA, V: ZA, ru: (q.rx + 0.024) * lerp(0.97, 1, end), rv: (q.rz + 0.03) * lerp(0.96, 1, end), e: 2.6, b: q.w, mod: (an) => ribs * (1 - 0.1 * Math.pow(Math.abs(Math.cos(an)), 14)) * (1 - 0.13 * Math.pow(Math.max(0, -Math.sin(an)), 40)) });
  }
  m.loft('vest', rings, { n: 44, weights: (k) => rings[k].b });
  const yoke = [];
  for (let k = 0; k <= 8; k++) {
    const y = lerp(C + 0.19, C + 0.272, k / 8), q = bodyAt(y);
    yoke.push({ c: [0, y, q.zo], U: XA, V: ZA, ru: q.rx + 0.03, rv: q.rz + 0.032, e: 2.5, b: q.w });
  }
  m.loft('strap', yoke, { n: 28, weights: (k) => yoke[k].b });
  for (const y of [S + 0.06, C + 0.05]) {
    const q = bodyAt(y), r = [];
    for (let k = 0; k <= 2; k++) { const yy = y - 0.016 + 0.016 * k; const qq = bodyAt(yy); r.push({ c: [0, yy, qq.zo], U: XA, V: ZA, ru: qq.rx + 0.039, rv: qq.rz + 0.046, e: 2.6, b: q.w }); }
    m.loft('strap', r, { n: 32, weights: (k) => r[k].b });
  }
  // the collar, a soft roll
  m.loft('strap', [0, 1, 2, 3, 4].map((k) => { const an = (k / 4) * Math.PI, y = C + 0.27 + 0.03 * Math.sin(an), rr = 0.078 + 0.028 * Math.cos(an) + 0.012; return { c: [0, y, -0.004], U: XA, V: ZA, ru: rr, rv: rr * 0.92, e: 2 }; }), { n: 20, weights: () => [[ch, 1]] });
}
for (const s of ['L', 'R']) {
  const sg = s === 'L' ? 1 : -1, arm = 'Arm' + s, fore = 'Fore' + s, hand = W['Hand' + s], foot = W['Foot' + s];
  limb('suit', [arm, fore, 'Hand' + s], [0.058, 0.044, 0.031], { muscle: 0.16, n: 14 });
  // the shoulder's round cap
  m.ellipsoid('suit', add(W[arm], [sg * 0.008, 0, 0]), 0.074, 0.078, 0.07, { steps: 8, n: 16, weights: () => [[BI[arm], 0.8], [ch, 0.2]] });
  // a bright panel down the outside of each arm and thigh
  limb('accent', [arm, fore], [0.0605, 0.047], { muscle: 0.16, n: 10, arc: sg > 0 ? [Math.PI * 0.75, Math.PI * 1.25] : [-Math.PI * 0.25, Math.PI * 0.25], flat: true });
  limb('suit', ['Thigh' + s, 'Shin' + s, 'Foot' + s], [0.088, 0.056, 0.041], { muscle: 0.14, n: 16 });
  limb('accent', ['Thigh' + s, 'Shin' + s], [0.0905, 0.0585], { muscle: 0.14, n: 10, arc: sg > 0 ? [Math.PI * 0.75, Math.PI * 1.25] : [-Math.PI * 0.25, Math.PI * 0.25], flat: true });
  // a knee pad
  m.ellipsoid('strap', add(W['Shin' + s], [0, 0.005, 0.042]), 0.05, 0.062, 0.03, { steps: 6, n: 12, weights: () => [[BI['Thigh' + s], 0.5], [BI['Shin' + s], 0.5]] });
  // the glove: a padded palm, four fingers curled round the grip, a thumb, a cuff
  const hw_ = () => [[BI['Hand' + s], 1]], glove = 'glove';
  m.capsule(glove, sub(hand, [sg * 0.018, 0, 0]), add(hand, [sg * 0.052, 0.002, -0.004]), 0.03, 0.034, { ratio: 0.62, weights: hw_ });
  m.capsule('strap', sub(hand, [sg * 0.03, 0, 0]), sub(hand, [sg * 0.012, 0, 0]), 0.034, 0.034, { weights: hw_ });
  for (let f = 0; f < 4; f++) {
    const x = sg * (0.06 + 0.0), z = -0.014 + 0.0, off = -0.023 + 0.0155 * f;
    m.capsule(glove, add(hand, [x + sg * 0.012, 0.008, off + z]), add(hand, [x + sg * 0.04, -0.012, off + z]), 0.0095, 0.0085, { caps: 2, rings: 2, n: 7, weights: hw_ });
    m.capsule(glove, add(hand, [x + sg * 0.04, -0.012, off + z]), add(hand, [x + sg * 0.036, -0.034, off + z]), 0.0085, 0.0075, { caps: 2, rings: 2, n: 7, weights: hw_ });
  }
  m.capsule(glove, add(hand, [sg * 0.03, -0.012, 0.026]), add(hand, [sg * 0.06, -0.028, 0.03]), 0.0105, 0.009, { caps: 2, rings: 2, n: 7, weights: hw_ });
  // the boot: shaped upper, a thick sole
  const bw = () => [[BI['Foot' + s], 1]];
  m.capsule('boot', add(foot, [0, -0.02, -0.03]), add(foot, [0, -0.052, 0.14]), 0.052, 0.042, { ratio: 0.82, weights: bw });
  m.capsule('boot', add(foot, [0, 0.03, 0]), add(foot, [0, -0.015, -0.02]), 0.05, 0.054, { weights: bw });
  m.capsule('strap', add(foot, [0, -0.085, -0.05]), add(foot, [0, -0.09, 0.17]), 0.046, 0.04, { ratio: 0.32, weights: bw });
}
// the helmet: a shell smaller than a cartoon's, a brow, a chin bar, a wrapping visor, a neck roll, vents and a stripe
const Hd = W.Head, he = BI.Head, hw = () => [[he, 1]];
m.ellipsoid('helmet', add(Hd, [0, 0.085, -0.012]), 0.103, 0.11, 0.122, { steps: 14, n: 26, weights: hw });
m.capsule('helmet', add(Hd, [-0.065, 0.002, 0.075]), add(Hd, [0.065, 0.002, 0.075]), 0.036, 0.036, { n: 12, weights: hw });
m.capsule('helmet', add(Hd, [-0.08, 0.135, 0.075]), add(Hd, [0.08, 0.135, 0.075]), 0.013, 0.013, { n: 8, weights: hw });
m.ellipsoid('visor', add(Hd, [0, 0.08, 0.052]), 0.092, 0.05, 0.066, { steps: 8, n: 20, weights: hw });
m.capsule('strap', add(Hd, [0, 0.0, -0.05]), add(Hd, [0, -0.055, -0.075]), 0.086, 0.07, { ratio: 0.9, n: 16, weights: () => [[he, 0.6], [ne, 0.4]] });
m.ellipsoid('accent', add(Hd, [0, 0.086, -0.014]), 0.0255, 0.1125, 0.124, { steps: 10, n: 14, weights: hw });
for (const s of [-1, 1]) m.capsule('strap', add(Hd, [s * 0.042, 0.15, -0.06]), add(Hd, [s * 0.046, 0.12, -0.115]), 0.008, 0.008, { caps: 2, rings: 2, n: 6, weights: hw });
const glb = new Glb();
const mats = { suit: glb.material('suit', 0.58), vest: glb.material('vest', 0.86), strap: glb.material('strap', 0.85), accent: glb.material('accent', 0.55), glove: glb.material('glove', 0.7), boot: glb.material('boot', 0.62), helmet: glb.material('helmet', 0.26), visor: glb.material('visor', 0.08, 0.35) };
const color = { suit: [0.035, 0.04, 0.05, 1], vest: [0.9, 0.3, 0.05, 1], strap: [0.018, 0.018, 0.022, 1], accent: [0.9, 0.3, 0.05, 1], glove: [0.06, 0.06, 0.07, 1], boot: [0.05, 0.05, 0.06, 1], helmet: [0.82, 0.82, 0.8, 1], visor: [0.02, 0.03, 0.05, 1] };
for (const [k, c] of Object.entries(color)) glb.materials[mats[k]].pbrMetallicRoughness.baseColorFactor = c;
const jn = BONES.map((n) => glb.node({ name: n, translation: T[n] }));
BONES.forEach((n, i) => { if (PARENT[n]) (glb.nodes[jn[BI[PARENT[n]]]].children ||= []).push(jn[i]); });
const ibm = new Float32Array(BONES.length * 16);
BONES.forEach((n, i) => ibm.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -W[n][0], -W[n][1], -W[n][2], 1], i * 16));
glb.skins.push({ name: 'Rider', inverseBindMatrices: glb.acc(ibm, 'MAT4', 5126, 0), joints: jn, skeleton: jn[0] });
const body = glb.node({ name: 'Body', mesh: glb.mesh('rider', m.prims(mats)), skin: 0 });
glb.sceneNodes.push(body, jn[0]);
glb.write(join(OUT, 'rider.glb'));
console.log('rider', m.tris(), 'tris');

// ── a plain (unskinned) mesh helper: smooth grid strips ──
class Plain {
  constructor() { this.g = new Map(); }
  group(mat) { if (!this.g.has(mat)) this.g.set(mat, { pos: [], nrm: [], uv: [], idx: [] }); return this.g.get(mat); }
  // a grid of points [row][col]; normals from the grid
  grid(mat, P, o = {}) {
    const g = this.group(mat), base = g.pos.length / 3, R = P.length, Cc = P[0].length;
    for (let r = 0; r < R; r++) for (let c = 0; c < Cc; c++) {
      const a = P[Math.max(0, r - 1)][c], b = P[Math.min(R - 1, r + 1)][c];
      const cc = o.wrap ? (x) => (x + Cc - 1) % (Cc - 1) : (x) => clamp(x, 0, Cc - 1);
      const d = P[r][cc(c - 1)], e = P[r][cc(c + 1)];
      let n = norm(cross(sub(b, a), sub(e, d)));
      if (o.flip) n = mul(n, -1);
      g.pos.push(...P[r][c]); g.nrm.push(...n); g.uv.push(c / (Cc - 1), r / (R - 1));
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
      const [U, V] = frame(dir), r = typeof rad === 'function' ? rad(i / (pts.length - 1)) : rad, row = [];
      for (let k = 0; k <= n; k++) { const a = (k % n) / n * Math.PI * 2; row.push(add(pts[i], add(mul(U, Math.cos(a) * r), mul(V, Math.sin(a) * r * (o.ratio ?? 1))))); }
      rows.push(row);
    }
    if (o.cap !== false) { rows.unshift(rows[0].map(() => pts[0])); rows.push(rows[rows.length - 1].map(() => pts[pts.length - 1])); }
    this.grid(mat, rows, { wrap: true, flip: o.flip ?? true });
  }
  prims(mi) { return [...this.g].map(([k, g]) => [g, mi[k]]); }
  tris() { let t = 0; for (const g of this.g.values()) t += g.idx.length / 3; return t; }
}

// ── the craft: forward is +z, the waterline about y = 0, length 3.1 m ──
{
  const P = new Plain();
  const sec = (u) => {
    const bow = smooth(0.58, 1.0, u), sb = smooth(0.0, 0.05, u);
    const beam = lerp(0.58, 0.02, Math.pow(bow, 1.5)) * lerp(0.9, 1, sb);
    const rocker = 0.34 * Math.pow(smooth(0.6, 1.0, u), 1.6);
    return { z: lerp(-1.5, 1.6, u), beam, keel: -0.2 + rocker, chine: -0.09 + rocker * 0.85, gun: 0.2 + rocker * 0.55 + 0.04 * Math.sin(u * Math.PI) };
  };
  const US = 36;
  // the sides, top to chine, so each band is its own colour: side panel, a stripe, lower side, then the V of the bottom with a strake
  const sidePt = (u, sg, f) => { const s = sec(u); return [sg * s.beam * lerp(1, 1.02, f), lerp(s.gun, s.chine, f), s.z]; };
  const band = (mat, f0, f1, flip) => { for (const sg of [-1, 1]) { const rows = []; for (let k = 0; k <= US; k++) { const u = k / US, row = []; for (let j = 0; j <= 3; j++) row.push(sidePt(u, sg, lerp(f0, f1, j / 3))); rows.push(row); } P.grid(mat, rows, { flip: sg > 0 ? !flip : flip }); } };
  band('panel', 0, 0.32); band('stripe', 0.32, 0.46); band('panel', 0.46, 0.72); band('hull', 0.72, 1);
  { const rows = []; for (let k = 0; k <= US; k++) { const s = sec(k / US), row = []; for (const [x, y] of [[-s.beam * 1.02, s.chine], [-s.beam * 0.72, (s.chine * 2 + s.keel) / 3 - 0.012], [-s.beam * 0.68, (s.chine * 2 + s.keel) / 3], [-s.beam * 0.3, (s.chine + s.keel * 2) / 3 - 0.012], [-s.beam * 0.26, (s.chine + s.keel * 2) / 3], [0, s.keel], [s.beam * 0.26, (s.chine + s.keel * 2) / 3], [s.beam * 0.3, (s.chine + s.keel * 2) / 3 - 0.012], [s.beam * 0.68, (s.chine * 2 + s.keel) / 3], [s.beam * 0.72, (s.chine * 2 + s.keel) / 3 - 0.012], [s.beam * 1.02, s.chine]]) row.push([x, y, s.z]); rows.push(row); } P.grid('hull', rows, { flip: true }); }
  // the deck: a raised spine with the seat on it, footwells either side, gunwale rails, a stepped rear platform, a tower and a nose
  const deckPt = (u, t) => {
    const s = sec(u), ax = Math.abs(t);
    const crown = 0.1 + 0.05 * (1 - u);
    const tower = 0.34 * Math.exp(-Math.pow((u - 0.62) / 0.1, 2)) * (1 - ax * ax);
    const nose = 0.22 * smooth(0.62, 0.85, u) * (1 - smooth(0.85, 1, u)) * (1 - ax * ax);
    const well = -0.15 * smooth(0.36, 0.46, ax) * (1 - smooth(0.82, 0.92, ax)) * smooth(0.13, 0.18, u) * (1 - smooth(0.5, 0.58, u));
    const rear = -0.1 * (1 - smooth(0.13, 0.17, u));
    return [t * s.beam, s.gun + crown * (1 - ax * ax) + tower + nose + well + rear, s.z];
  };
  const deckRegion = (mat, u0, u1, t0, t1, nu = 18, nt = 6) => { const rows = []; for (let k = 0; k <= nu; k++) { const u = lerp(u0, u1, k / nu), row = []; for (let j = 0; j <= nt; j++) row.push(deckPt(u, lerp(t0, t1, j / nt))); rows.push(row); } P.grid(mat, rows); };
  deckRegion('deck', 0.0, 1.0, -0.4, 0.4, 36, 6);
  for (const sg of [-1, 1]) {
    const [a, b] = sg > 0 ? [0.4, 0.88] : [-0.88, -0.4], [c, d] = sg > 0 ? [0.88, 1] : [-1, -0.88];
    deckRegion('mat', 0.15, 0.56, a, b, 14, 6);
    deckRegion('deck', 0.56, 1, a, b, 18, 6);
    deckRegion('deck2', 0.0, 0.15, a, b, 6, 6);
    deckRegion('deck2', 0.0, 1, c, d, 36, 2);
  }
  // the stern's face: closed, dark, with two tail lamps, and the round steering nozzle standing out of it
  const s0 = sec(0);
  { const row0 = []; for (let j = 0; j <= 12; j++) { const t = j / 12 * 2 - 1; const p = deckPt(0, t); row0.push(p); } const row1 = row0.map(([x, y, z]) => { const ax = Math.abs(x / s0.beam); return [x, lerp(s0.chine, s0.keel, 1 - ax), z]; }); P.grid('transom', [row1, row0]); }
  for (const sg of [-1, 1]) P.tube('lamp', [[sg * s0.beam * 0.72, s0.gun - 0.02, s0.z - 0.012], [sg * s0.beam * 0.72, s0.gun - 0.02, s0.z - 0.03]], 0.035, 10, { ratio: 0.5 });
  // ride plate, under the jet
  P.grid('metal', [[[-0.17, -0.19, -1.72], [0.17, -0.19, -1.72]], [[-0.17, -0.2, -1.42], [0.17, -0.2, -1.42]]], { flip: false });
  P.tube('metal', [[0, -0.04, -1.5], [0, -0.04, -1.56]], 0.135, 18);
  P.tube('nozzle', [[0, -0.045, -1.54], [0, -0.05, -1.7]], (t) => lerp(0.115, 0.088, t), 18, { cap: false });
  P.tube('dark', [[0, -0.05, -1.66], [0, -0.05, -1.701]], 0.07, 16);
  P.tube('metal', [[0, -0.05, -1.695], [0, -0.05, -1.712]], (t) => lerp(0.094, 0.09, t), 18, { cap: false });
  // the rear grab loop and the boarding step
  P.tube('metal', [[-0.2, s0.gun + 0.04, -1.46], [-0.22, s0.gun + 0.12, -1.35], [-0.16, s0.gun + 0.17, -1.2], [0, s0.gun + 0.18, -1.14], [0.16, s0.gun + 0.17, -1.2], [0.22, s0.gun + 0.12, -1.35], [0.2, s0.gun + 0.04, -1.46]], 0.012, 8);
  // the seat: one long saddle in two parts with a stepped back, stitched rolls and bright sides, on a raised plinth
  const seatRow = (z, wid, top, lo) => { const row = []; for (let i = 0; i <= 12; i++) { const an = (i / 12) * Math.PI; row.push([Math.cos(an) * wid, lo + (top - lo) * Math.pow(Math.sin(an), 0.45), z]); } return row; };
  const SZ = [[-0.96, 0.09, 0.37], [-0.92, 0.135, 0.44], [-0.84, 0.158, 0.47], [-0.5, 0.17, 0.48], [-0.32, 0.17, 0.485], [-0.3, 0.172, 0.53], [-0.26, 0.174, 0.555], [-0.05, 0.17, 0.57], [0.15, 0.135, 0.585], [0.24, 0.05, 0.6]];
  P.grid('seat', SZ.map(([z, w, t]) => seatRow(z, w, t, 0.3)), { flip: true });
  for (const sg of [-1, 1]) P.tube('stripe', SZ.slice(1, 8).map(([z, w, t]) => [sg * w * 0.96, t - 0.085, z]), 0.016, 6);
  P.tube('strapdark', [[-0.15, 0.487, -0.56], [0, 0.494, -0.56], [0.15, 0.487, -0.56]], 0.009, 6);
  // the steering tower: a cowl, a dark instrument pod, vents on its flanks
  const cowl = [];
  for (let k = 0; k <= 10; k++) { const t = k / 10, z = lerp(0.2, 0.72, t), row = []; for (let i = 0; i <= 12; i++) { const an = i / 12 * Math.PI; const w = 0.19 * (1 - 0.32 * t); row.push([Math.cos(an) * w, 0.52 + (0.27 * Math.pow(Math.sin(an), 0.7)) * (1 - 0.7 * t) - 0.02 * t, z]); } cowl.push(row); }
  P.grid('deck2', cowl, { flip: true });
  P.grid('dark', [[[-0.1, 0.74, 0.27], [0.1, 0.74, 0.27]], [[-0.11, 0.79, 0.22], [0.11, 0.79, 0.22]]], { flip: false });
  for (const sg of [-1, 1]) for (let k = 0; k < 3; k++) { const z = 0.3 + k * 0.08; P.grid('dark', [[[sg * 0.168, 0.56, z], [sg * 0.15, 0.62, z]], [[sg * 0.164, 0.56, z + 0.05], [sg * 0.146, 0.62, z + 0.05]]], { flip: sg > 0 }); }
  const bars = new Plain();
  bars.tube('metal', [[0, -0.28, 0.04], [0, 0, 0]], 0.03, 8);
  bars.tube('metal', [[-0.31, 0.02, -0.03], [-0.15, 0.0, 0.0], [0, -0.01, 0.0], [0.15, 0.0, 0.0], [0.31, 0.02, -0.03]], 0.014, 8);
  for (const sg of [-1, 1]) bars.tube('trim', [[sg * 0.23, 0.015, -0.015], [sg * 0.34, 0.025, -0.033]], 0.02, 8);
  bars.tube('dark', [[-0.03, 0.01, -0.03], [0.03, 0.01, -0.03]], 0.03, 8);
  const g = new Glb();
  const mi = { hull: g.material('hull', 0.34), panel: g.material('panel', 0.3), stripe: g.material('deck2', 0.3), deck: g.material('deck', 0.24), deck2: g.material('deck2', 0.24), mat: g.material('mat', 0.92), transom: g.material('transom', 0.5), lamp: g.material('lamp', 0.3, 0, [9, 0.5, 0.2]), seat: g.material('seat', 0.62), trim: g.material('trim', 0.62), metal: g.material('metal', 0.28, 0.95), nozzle: g.material('nozzle', 0.38, 0.8), dark: g.material('dark', 0.7), strapdark: g.material('dark', 0.7) };
  const col = { hull: [0.11, 0.13, 0.17, 1], panel: [0.78, 0.78, 0.76, 1], stripe: [0.05, 0.05, 0.06, 1], deck: [0.88, 0.1, 0.06, 1], deck2: [0.05, 0.05, 0.06, 1], mat: [0.14, 0.14, 0.15, 1], transom: [0.09, 0.1, 0.12, 1], lamp: [0.5, 0.03, 0.02, 1], seat: [0.085, 0.09, 0.1, 1], trim: [0.02, 0.02, 0.025, 1], metal: [0.62, 0.64, 0.66, 1], nozzle: [0.28, 0.29, 0.31, 1], dark: [0.015, 0.015, 0.018, 1], strapdark: [0.015, 0.015, 0.018, 1] };
  for (const [k, c] of Object.entries(col)) g.materials[mi[k]].pbrMetallicRoughness.baseColorFactor = c;
  const hullN = g.node({ name: 'Hull', mesh: g.mesh('hull', P.prims(mi)) });
  const barN = g.node({ name: 'Bars', mesh: g.mesh('bars', bars.prims(mi)), translation: [0, 0.86, 0.3] });
  g.sceneNodes.push(hullN, barN);
  g.write(join(OUT, 'craft.glb'));
  console.log('craft', P.tris() + bars.tris(), 'tris');
}

// ── a slalom buoy: y = 0 is the waterline; part 'band' takes the side's colour ──
{
  const P = new Plain(), prof = [[0, -0.5], [0.34, -0.48], [0.56, -0.2], [0.62, 0.1], [0.58, 0.4], [0.46, 0.75], [0.34, 1.1], [0.28, 1.3], [0.05, 1.42], [0, 1.42]];
  const rowsOf = (pr) => pr.map(([r, y]) => Array.from({ length: 25 }, (_, i) => { const a = (i % 24) / 24 * Math.PI * 2; return [Math.cos(a) * r, y, Math.sin(a) * r]; }));
  P.grid('body', rowsOf(prof.slice(0, 4)), { wrap: true });
  P.grid('band', rowsOf(prof.slice(3, 8)), { wrap: true });
  P.grid('body', rowsOf(prof.slice(7)), { wrap: true });
  P.tube('metal', [[0, 1.4, 0], [0, 2.1, 0]], 0.03, 8);
  P.tube('lamp', [[0, 2.08, 0], [0, 2.36, 0]], (t) => 0.1 * Math.sin(Math.PI * clamp(t * 0.85 + 0.1)), 12);
  const g = new Glb();
  const mi = { body: g.material('body', 0.5), band: g.material('band', 0.32), metal: g.material('metal', 0.35, 0.9), lamp: g.material('lamp', 0.3, 0, [1, 1, 1]) };
  g.materials[mi.body].pbrMetallicRoughness.baseColorFactor = [0.05, 0.05, 0.06, 1];
  g.materials[mi.metal].pbrMetallicRoughness.baseColorFactor = [0.5, 0.5, 0.52, 1];
  g.sceneNodes.push(g.node({ name: 'Buoy', mesh: g.mesh('buoy', P.prims(mi)) }));
  g.write(join(OUT, 'buoy.glb'));
}

// ── a palm: a leaning ringed trunk and a crown of arching fronds, height about 9 m (the scatter scales it) ──
{
  const trunk = new Plain(), crown = new Plain();
  const lean = 0.8;
  const pts = Array.from({ length: 9 }, (_, i) => { const t = i / 8; return [lean * t * t * 1.6, -0.3 + t * 9, 0.3 * t]; });
  trunk.tube('trunk', pts, (t) => lerp(0.2, 0.1, Math.pow(t, 0.7)) * (1 + 0.1 * Math.sin(t * 60)), 7, { cap: false });
  const top = pts[8];
  for (let f = 0; f < 11; f++) {
    const az = f / 11 * Math.PI * 2 + (f % 3) * 0.2, rise = f % 2 ? 0.75 : 0.25, L = 3.4 + (f % 3) * 0.6, dirx = Math.cos(az), dirz = Math.sin(az);
    // a spine that arches up and droops, with leaflets as a saw-edged strip: two rows of points either side
    const rows = [];
    for (let i = 0; i <= 12; i++) {
      const t = i / 12, d = t * L, y = rise * Math.sin(t * Math.PI * 0.75) * 1.4 - 2.2 * t * t;
      const w = 0.62 * Math.sin(Math.pow(t, 0.7) * Math.PI) * (i % 2 ? 1 : 0.62);
      const c = add(top, [dirx * d, y, dirz * d]), side = [-dirz, 0, dirx];
      rows.push([add(c, add(mul(side, -w), [0, -w * 0.5, 0])), add(c, [0, 0.03, 0]), add(c, add(mul(side, w), [0, -w * 0.5, 0]))]);
    }
    crown.grid('frond', rows);
  }
  const g = new Glb();
  const mi = { trunk: g.material('trunk', 0.92), frond: g.material('frond', 0.68) };
  g.materials[mi.trunk].pbrMetallicRoughness.baseColorFactor = [0.26, 0.2, 0.14, 1];
  g.materials[mi.frond].pbrMetallicRoughness.baseColorFactor = [0.13, 0.3, 0.06, 1];
  g.materials[mi.frond].doubleSided = true;
  g.sceneNodes.push(g.node({ name: 'Palm', mesh: g.mesh('palm', [...trunk.prims(mi), ...crown.prims(mi)]) }));
  g.write(join(OUT, 'palm.glb'));
  console.log('palm', trunk.tris() + crown.tris(), 'tris');
}

// ── the lighthouse: white tower, red band, gallery and lamp; y = 0 at its foot ──
{
  const P = new Plain();
  const ring = (r, y) => Array.from({ length: 25 }, (_, i) => { const a = (i % 24) / 24 * Math.PI * 2; return [Math.cos(a) * r, y, Math.sin(a) * r]; });
  const lathe = (mat, prof) => P.grid(mat, prof.map(([r, y]) => ring(r, y)), { wrap: true });
  lathe('wall', [[3.2, -2], [3.1, 1], [2.75, 6], [2.5, 10]]);
  lathe('band', [[2.5, 10], [2.25, 14], [2.15, 16]]);
  lathe('wall', [[2.15, 16], [1.9, 21]]);
  lathe('dark', [[1.9, 21], [2.7, 21.3], [2.7, 21.7], [1.5, 21.8]]);
  lathe('lamp', [[1.35, 21.8], [1.35, 24]]);
  lathe('dark', [[1.5, 24], [0.3, 25.6], [0.02, 25.8]]);
  const g = new Glb();
  const mi = { wall: g.material('wall', 0.85), band: g.material('band', 0.8), dark: g.material('dark', 0.5, 0.5), lamp: g.material('lamp', 0.3, 0, [8, 5, 2]) };
  g.materials[mi.wall].pbrMetallicRoughness.baseColorFactor = [0.82, 0.8, 0.74, 1];
  g.materials[mi.band].pbrMetallicRoughness.baseColorFactor = [0.55, 0.1, 0.07, 1];
  g.materials[mi.dark].pbrMetallicRoughness.baseColorFactor = [0.08, 0.08, 0.09, 1];
  g.sceneNodes.push(g.node({ name: 'Lighthouse', mesh: g.mesh('lighthouse', P.prims(mi)) }));
  g.write(join(OUT, 'lighthouse.glb'));
}

// ── the start gate: two pontoons and an arch across the course, floating; y = 0 at the waterline, centre at the origin ──
{
  const P = new Plain(), W = 21;
  for (const s of [-1, 1]) {
    P.tube('float', [[s * W, 0.05, -3.4], [s * W, 0.2, -2.6], [s * W, 0.2, 2.6], [s * W, 0.05, 3.4]], (t) => 1.05 * Math.pow(Math.sin(Math.PI * clamp(t * 0.9 + 0.05)), 0.4), 14);
    P.tube('steel', [[s * W, 0.6, -1.2], [s * W * 0.985, 11.2, 0]], 0.2, 10);
    P.tube('steel', [[s * W, 0.6, 1.2], [s * W * 0.985, 11.2, 0]], 0.2, 10);
  }
  P.tube('steel', Array.from({ length: 25 }, (_, i) => { const t = i / 24 * 2 - 1; return [t * W, 11.2 + 2.6 * (1 - t * t), 0]; }), 0.24, 10);
  // the banner: checks along the arch, a plain strip with the check painted by vertex-free material per square
  for (let i = 0; i < 24; i++) {
    const t0 = i / 24 * 2 - 1, t1 = (i + 1) / 24 * 2 - 1, mat = i % 2 ? 'bannerA' : 'bannerB';
    const top = (t) => [t * W * 0.92, 11.1 + 2.6 * (1 - t * t) * 0.92, 0.02];
    const a = top(t0), b = top(t1), d = 1.5;
    P.grid(mat, [[[a[0], a[1] - d, 0.03], [b[0], b[1] - d, 0.03]], [[a[0], a[1], 0.03], [b[0], b[1], 0.03]]], { flip: true });
    P.grid(mat, [[[a[0], a[1] - d, -0.03], [b[0], b[1] - d, -0.03]], [[a[0], a[1], -0.03], [b[0], b[1], -0.03]]]);
  }
  const g = new Glb();
  const mi = { float: g.material('float', 0.5), steel: g.material('steel', 0.42, 0.7), bannerA: g.material('bannerA', 0.9), bannerB: g.material('bannerB', 0.9) };
  g.materials[mi.float].pbrMetallicRoughness.baseColorFactor = [0.72, 0.16, 0.05, 1];
  g.materials[mi.steel].pbrMetallicRoughness.baseColorFactor = [0.55, 0.56, 0.58, 1];
  g.materials[mi.bannerA].pbrMetallicRoughness.baseColorFactor = [0.9, 0.88, 0.82, 1];
  g.materials[mi.bannerB].pbrMetallicRoughness.baseColorFactor = [0.04, 0.05, 0.07, 1];
  g.sceneNodes.push(g.node({ name: 'Gate', mesh: g.mesh('gate', P.prims(mi)) }));
  g.write(join(OUT, 'gate.glb'));
}

// ── waterline rocks and shrubs: rough lumps, a few of each so nothing repeats; the scatter turns and scales them ──
function lump(mat, seed, rx, ry, rz, rough, P) {
  const hash = (a, b) => { let h = (a * 374761393 + b * 668265263 + seed * 1274126177) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };
  const rows = [];
  const NR = 12, NS = 18;
  const R = [];
  for (let j = 0; j <= NR; j++) { R[j] = []; for (let i = 0; i < NS; i++) R[j][i] = 1 + rough * (hash(j, i) - 0.5) + rough * 0.6 * (hash(j >> 1, i >> 1) - 0.5); }
  for (let j = 0; j <= NR; j++) {
    const th = j / NR * Math.PI, row = [];
    for (let i = 0; i <= NS; i++) { const a = (i % NS) / NS * Math.PI * 2, r = R[j][i % NS]; row.push([Math.cos(a) * Math.sin(th) * rx * r, Math.cos(th) * ry * r + ry * 0.15, Math.sin(a) * Math.sin(th) * rz * r]); }
    rows.push(row);
  }
  P.grid(mat, rows, { wrap: true, flip: true });
}
for (let v = 0; v < 3; v++) {
  const P = new Plain();
  lump('rock', 11 + v, 1.0 + v * 0.25, 0.6 + 0.2 * (v % 2), 0.8, 0.5, P);
  const g = new Glb(), mi = { rock: g.material('rock', 0.93) };
  g.materials[mi.rock].pbrMetallicRoughness.baseColorFactor = [0.075 + 0.02 * v, 0.07 + 0.015 * v, 0.07, 1];
  g.sceneNodes.push(g.node({ name: 'Rock', mesh: g.mesh('rock', P.prims(mi)) }));
  g.write(join(OUT, 'rock' + v + '.glb'));
}
{
  const P = new Plain();
  lump('leaf', 31, 1.4, 1.0, 1.3, 0.9, P);
  lump('leaf', 32, 0.8, 0.8, 0.9, 1.0, { grid: (mat, rows, o) => P.grid(mat, rows.map((r) => r.map(([x, y, z]) => [x + 0.9, y - 0.1, z + 0.5])), o) });
  const g = new Glb(), mi = { leaf: g.material('leaf', 0.8) };
  g.materials[mi.leaf].pbrMetallicRoughness.baseColorFactor = [0.05, 0.13, 0.03, 1];
  g.sceneNodes.push(g.node({ name: 'Shrub', mesh: g.mesh('shrub', P.prims(mi)) }));
  g.write(join(OUT, 'shrub.glb'));
}
