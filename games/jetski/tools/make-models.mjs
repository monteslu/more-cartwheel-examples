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
// one primitive per material: a draw call is what a racer costs, three times over with the reflection and the shadow
function merged(prims) {
  const by = new Map();
  for (const [g, mi] of prims) {
    if (!by.has(mi)) { by.set(mi, { pos: [...g.pos], nrm: [...g.nrm], uv: [...(g.uv || [])], idx: [...g.idx], joints: g.joints ? [...g.joints] : undefined, weights: g.weights ? [...g.weights] : undefined }); continue; }
    const t = by.get(mi), base = t.pos.length / 3;
    t.pos.push(...g.pos); t.nrm.push(...g.nrm); t.uv.push(...(g.uv || [])); for (const k of g.idx) t.idx.push(k + base);
    if (t.joints) { t.joints.push(...g.joints); t.weights.push(...g.weights); }
  }
  return [...by].map(([mi, g]) => [g, mi]);
}
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
// bare forearms and a ponytail: src/view.c turns them on and colours them rider by rider
for (const s of ['L', 'R']) limb('skin', ['Fore' + s, 'Hand' + s], [0.0465, 0.033], { muscle: 0.16, n: 12 });
for (let k = 0; k < 2; k++) {
  const a = add(W.Head, [0, 0.07 - k * 0.09, -0.125 - k * 0.035]), b = add(W.Head, [0, -0.02 - k * 0.11, -0.16 - k * 0.012]);
  m.capsule('hair', a, b, 0.036 - k * 0.012, 0.026 - k * 0.012, { n: 10, weights: () => [[BI.Head, 1]] });
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
const suitMat = glb.material('suit', 0.66), vestMat = glb.material('vest', 0.82);
const mats = { suit: suitMat, strap: suitMat, glove: suitMat, boot: suitMat, skin: glb.material('skin', 0.55), hair: glb.material('hair', 0.7), vest: vestMat, accent: vestMat, helmet: glb.material('helmet', 0.26), visor: glb.material('visor', 0.08, 0.35) };
const color = { skin: [0.7, 0.5, 0.38, 1], hair: [0.05, 0.03, 0.02, 1], suit: [0.03, 0.033, 0.04, 1], vest: [0.9, 0.3, 0.05, 1], helmet: [0.82, 0.82, 0.8, 1], visor: [0.02, 0.03, 0.05, 1] };
for (const [k, c] of Object.entries(color)) glb.materials[mats[k]].pbrMetallicRoughness.baseColorFactor = c;
const jn = BONES.map((n) => glb.node({ name: n, translation: T[n] }));
BONES.forEach((n, i) => { if (PARENT[n]) (glb.nodes[jn[BI[PARENT[n]]]].children ||= []).push(jn[i]); });
const ibm = new Float32Array(BONES.length * 16);
BONES.forEach((n, i) => ibm.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -W[n][0], -W[n][1], -W[n][2], 1], i * 16));
glb.skins.push({ name: 'Rider', inverseBindMatrices: glb.acc(ibm, 'MAT4', 5126, 0), joints: jn, skeleton: jn[0] });
const body = glb.node({ name: 'Body', mesh: glb.mesh('rider', merged(m.prims(mats))), skin: 0 });
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
  P.grid('hull', [[[-0.17, -0.19, -1.72], [0.17, -0.19, -1.72]], [[-0.17, -0.2, -1.42], [0.17, -0.2, -1.42]]], { flip: false });
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
  const darkM = g.material('dark', 0.6), hullM = g.material('hull', 0.9), metalM = g.material('metal', 0.3, 0.9);
  const mi = { hull: hullM, trim: hullM, dark: hullM, strapdark: hullM, panel: g.material('panel', 0.3), deck: g.material('deck', 0.24), stripe: darkM, deck2: darkM, mat: darkM, transom: darkM, seat: darkM, lamp: g.material('lamp', 0.3, 0, [9, 0.5, 0.2]), metal: metalM, nozzle: metalM };
  const col = { hull: [0.02, 0.022, 0.028, 1], panel: [0.78, 0.78, 0.76, 1], deck: [0.88, 0.1, 0.06, 1], stripe: [0.075, 0.08, 0.09, 1], lamp: [0.5, 0.03, 0.02, 1], metal: [0.5, 0.52, 0.54, 1] };
  for (const [k, c] of Object.entries(col)) g.materials[mi[k]].pbrMetallicRoughness.baseColorFactor = c;
  const hullN = g.node({ name: 'Hull', mesh: g.mesh('hull', merged(P.prims(mi))) });
  const barN = g.node({ name: 'Bars', mesh: g.mesh('bars', merged(bars.prims(mi))), translation: [0, 0.86, 0.3] });
  g.sceneNodes.push(hullN, barN);
  g.write(join(OUT, 'craft.glb'));
  console.log('craft', P.tris() + bars.tris(), 'tris');
}

// ── a ramp: a rigid steel structure standing on the sea bed. Forward +z; y 0 is its top edge. 40 m long, 18 m wide, on a 1 in 2.8 deck
// (sizes match src/game.h RAMP_*), on piles that run far below any trough, braced, ropes along its sides, a wet band on every leg ──
{
  const P = new Plain(), HL = 20, HW = 9, SLOPE = 0.36, FOOT = -50;
  const deckY = (z) => (z - HL) * SLOPE;
  const beam = (mat, A, B, r) => P.tube(mat, [A, B], r, 6);
  // the deck: one sheet with its paint (make-effects: textures/ramp-deck.png), v from the low end to the lip
  P.grid('deck', [[[-HW, deckY(-HL), -HL], [HW, deckY(-HL), -HL]], [[-HW, deckY(HL), HL], [HW, deckY(HL), HL]]], { flip: false });
  // heavy side girders you can see on the way in: a deep web and a flange standing proud of the deck, stiffeners down the web
  for (const s of [-1, 1]) {
    const o = s * (HW + 0.75), rows = (a, b) => [[a, b]];
    for (const [A, B] of [[[o, -1.6, 0], [o, 1.1, 0]]]) {
      const P0 = (z, y) => [o, deckY(z) + y, z];
      P.grid('steel', s > 0 ? [[P0(-HL, -1.6), P0(HL, -1.6)], [P0(-HL, 1.1), P0(HL, 1.1)]] : [[P0(-HL, 1.1), P0(HL, 1.1)], [P0(-HL, -1.6), P0(HL, -1.6)]], { flip: false });
      const Q0 = (z, x, y) => [x, deckY(z) + y, z];
      P.grid('paint', s > 0 ? [[Q0(-HL, o, 1.1), Q0(HL, o, 1.1)], [Q0(-HL, s * HW, 1.1), Q0(HL, s * HW, 1.1)]] : [[Q0(-HL, s * HW, 1.1), Q0(HL, s * HW, 1.1)], [Q0(-HL, o, 1.1), Q0(HL, o, 1.1)]], { flip: false });
      P.grid('steel', s > 0 ? [[Q0(-HL, s * HW, 1.1), Q0(HL, s * HW, 1.1)], [Q0(-HL, s * HW, 0), Q0(HL, s * HW, 0)]] : [[Q0(-HL, s * HW, 0), Q0(HL, s * HW, 0)], [Q0(-HL, s * HW, 1.1), Q0(HL, s * HW, 1.1)]], { flip: false });
      for (let z = -HL; z <= HL + 0.01; z += 4) P.tube('steel', [[o + s * 0.1, deckY(z) - 1.6, z], [o + s * 0.1, deckY(z) + 1.1, z]], 0.12, 5);
    }
  }
  // the frame under it: long girders, cross beams
  for (const x of [-HW, -4.5, 0, 4.5, HW]) beam('steel', [x, deckY(-HL) - 0.35, -HL], [x, deckY(HL) - 0.35, HL], 0.28);
  for (let z = -HL; z <= HL + 0.01; z += 5) beam('steel', [-HW, deckY(z) - 0.5, z], [HW, deckY(z) - 0.5, z], 0.2);
  // a heavy lip bar
  beam('paint', [-HW, 0.05, HL], [HW, 0.05, HL], 0.2);
  // the piles: rows across, down past any sea; a dark wet band where the tide works; braces between them
  const PZ = [-8, 0, 8, 14, 20], PX = [-HW - 0.75, 0, HW + 0.75]; // (only where the deck stands clear of the water does a leg show)
  for (const z of PZ) for (const x of PX) {
    const top = deckY(z) - 0.5;
    // (the wet band: the tide's range on a ramp whose top is 5 to 6.5 m over the rest level. A leg starts under its part of the deck)
    if (top > -2) beam('steel', [x, top, z], [x, -2, z], 0.42);
    if (top > -10.5) beam('wet', [x, Math.min(top, -2), z], [x, -10.5, z], 0.44);
    beam('steel', [x, Math.min(top, -10.5), z], [x, FOOT, z], 0.42);
  }
  for (let i = 0; i < PZ.length - 1; i++) for (const x of PX) {
    // crossed between neighbouring legs, from under the deck on each down to the same depth
    const a = PZ[i], b = PZ[i + 1], ya = deckY(a) - 0.8, yb = deckY(b) - 0.8, low = Math.min(ya, yb) - 7;
    beam('steel', [x, ya, a], [x, low, b], 0.13);
    beam('steel', [x, yb, b], [x, low, a], 0.13);
  }
  for (const z of PZ) for (const s of [-1, 1]) {
    const y0 = deckY(z) - 0.8;
    beam('steel', [s * HW, y0, z], [0, y0 - 4.5, z], 0.13);
    beam('steel', [0, y0, z], [s * HW, y0 - 4.5, z], 0.13);
  }
  // ropes slung along the sides, and fenders on the first legs
  for (const s of [-1, 1]) {
    const pts = [];
    for (let i = 0; i <= 24; i++) { const t = i / 24, z = 4 + t * 16, sag = 0.5 * Math.sin((t * 2 % 1) * Math.PI); pts.push([s * (HW + 0.12), deckY(z) + 0.55 - sag, z]); }
    P.tube('rope', pts, 0.045, 5, { cap: false });
    for (const z of [12, 20]) P.tube('rust', [[s * (HW + 0.5), -1.2, z - 0.6], [s * (HW + 0.5), -3.4, z - 0.6]], 0.4, 8);
  }
  const g = new Glb();
  const mi = { deck: g.material('deck', 0.85, 0.05), paint: g.material('paint', 0.62), steel: g.material('steel', 0.62, 0.3), wet: g.material('wet', 0.55, 0.1), rope: g.material('rope', 0.95) };
  const col = { paint: [0.92, 0.38, 0.05], steel: [0.27, 0.27, 0.27], wet: [0.05, 0.07, 0.055], rope: [0.5, 0.42, 0.28] };
  for (const k of Object.keys(col)) { g.materials[mi[k]].pbrMetallicRoughness.baseColorFactor = [...col[k], 1]; g.materials[mi[k]].doubleSided = true; }
  g.materials[mi.deck].pbrMetallicRoughness.baseColorTexture = { index: g.tex(join(OUT, '../textures/ramp-deck.png')) };
  g.sceneNodes.push(g.node({ name: 'Ramp', mesh: g.mesh('ramp', P.prims(mi)) }));
  g.write(join(OUT, 'ramp.glb'));
  console.log('ramp', P.tris(), 'tris');
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

// ── a pine: a dark trunk under tiers of drooping boughs, height about 11 m (the scatter scales it) ──
{
  const P = new Plain();
  P.tube('trunk', [[0, -0.4, 0], [0, 10.5, 0]], (t) => lerp(0.26, 0.05, t), 7, { cap: false });
  const tiers = 7;
  for (let t = 0; t < tiers; t++) {
    const k = t / (tiers - 1), y0 = 1.6 + k * 8.2, r = lerp(2.9, 0.55, k), top = y0 + lerp(2.3, 1.5, k);
    const rows = [];
    for (let q = 0; q <= 3; q++) {
      const f = q / 3, row = [];
      for (let i = 0; i <= 12; i++) {
        const a = (i % 12) / 12 * Math.PI * 2 + t * 0.5, ragged = q === 3 ? 1 + 0.16 * Math.sin(i * 2.7 + t) : 1;
        row.push([Math.cos(a) * r * f * ragged, lerp(top, y0, Math.pow(f, 0.8)), Math.sin(a) * r * f * ragged]);
      }
      rows.push(row);
    }
    P.grid('bough', rows, { flip: true });
  }
  const g = new Glb();
  const mi = { trunk: g.material('trunk', 0.92), bough: g.material('bough', 0.85) };
  g.materials[mi.trunk].pbrMetallicRoughness.baseColorFactor = [0.13, 0.1, 0.08, 1];
  g.materials[mi.bough].pbrMetallicRoughness.baseColorFactor = [0.045, 0.1, 0.07, 1];
  g.materials[mi.bough].doubleSided = true;
  g.sceneNodes.push(g.node({ name: 'Pine', mesh: g.mesh('pine', P.prims(mi)) }));
  g.write(join(OUT, 'pine.glb'));
}

// ── a drowned arch: granite over a cut 22 m wide. Local y 0 is the sea's rest level, its underside at 3.6 m in the middle (the course
// definition's lintel). Runs across z, the direction of travel along its own x-axis once placed ──
{
  // a span of broken granite: thick in the middle, its ends running down into the headland on each side. Cross sections around the span,
  // knocked about at several scales and in ledges, moss on its back and the tide's dark stain under it. The opening stays clear: no point
  // inside the cut comes below the lintel
  const P = new Plain(), W = 42, LINTEL = 3.6, CUT = 12.5, NA = 36, NB = 26;
  const h = (i, j, k) => { let v = Math.imul(i * 374761393 + j * 668265263 + k * 1274126177, 1274126177); v = Math.imul(v ^ (v >>> 13), 1274126177); return ((v ^ (v >>> 16)) >>> 0) / 4294967296; };
  const noise = (x, y, k) => { const xi = Math.floor(x), yi = Math.floor(y), u = x - xi, t = y - yi, su = u * u * (3 - 2 * u), st = t * t * (3 - 2 * t); return lerp(lerp(h(xi, yi, k), h(xi + 1, yi, k), su), lerp(h(xi, yi + 1, k), h(xi + 1, yi + 1, k), su), st); };
  const point = (i, j) => {
    const t = i / NA * 2 - 1, x = t * W, a = (j % NB) / NB * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
    const ax = Math.abs(x), under = LINTEL - 24 * smooth(CUT, 25, ax), ry = 4.2 + 13 * smooth(CUT - 2, 26, ax), rz = 6 + 5 * smooth(CUT, 26, ax); // a true arch: its legs stand in the water at the cut's banks and widen into them
    const rough = 1 + 0.45 * (noise(i * 0.45, j * 0.5, 1) - 0.5) + 0.3 * (noise(i * 1.3, j * 1.5, 2) - 0.5) + 0.16 * (h(i, j % NB, 3) - 0.5);
    let y = under + ry + sa * ry * rough, z = ca * rz * rough;
    y += 0.5 * Math.sin((y + 3 * noise(i * 0.3, 0.5, 4)) * 2.1); // ledges
    if (ax < CUT && y < LINTEL) y = LINTEL + 0.1 * h(i, j, 5);
    return [x + 1.2 * (noise(i * 0.8, j * 0.8, 6) - 0.5), y, z];
  };
  const part = (m, from, to) => { const rows = []; for (let i = 0; i <= NA; i++) { const row = []; for (let j = from; j <= to; j++) row.push(point(i, j)); rows.push(row); } P.grid(m, rows, { flip: false }); };
  part('dark', -Math.round(NB * 0.38), -Math.round(NB * 0.12)); // under it, the tide's stain
  part('rock', -Math.round(NB * 0.12), Math.round(NB * 0.14));
  part('moss', Math.round(NB * 0.14), Math.round(NB * 0.36));
  part('rock', Math.round(NB * 0.36), Math.round(NB * 0.62));
  const g = new Glb();
  const mi = { rock: g.material('rock', 0.95), moss: g.material('moss', 0.95), dark: g.material('dark', 0.85) };
  const col = { rock: [0.2, 0.21, 0.23], moss: [0.13, 0.19, 0.08], dark: [0.05, 0.06, 0.055] };
  for (const k of Object.keys(mi)) g.materials[mi[k]].pbrMetallicRoughness.baseColorFactor = [...col[k], 1];
  g.sceneNodes.push(g.node({ name: 'Arch', mesh: g.mesh('arch', P.prims(mi)) }));
  g.write(join(OUT, 'arch.glb'));
}

// ── Gale Bank's kit: a drilling rig, wrecks, marker towers, the beacon on its rock with its beam ──
{
  const box = (P, m, x0, x1, y0, y1, z0, z1) => {
    const q = (a, b, c, d) => P.grid(m, [[a, b], [d, c]], { flip: false });
    q([x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]); q([x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0]);
    q([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]); q([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0]); q([x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1]);
  };
  const write = (name, node, P, cols) => {
    const g = new Glb(), mi = {};
    for (const [k, [rgb, rough, metal, em]] of Object.entries(cols)) { mi[k] = g.material(k, rough, metal ?? 0, em ?? null); g.materials[mi[k]].pbrMetallicRoughness.baseColorFactor = [...rgb, 1]; g.materials[mi[k]].doubleSided = true; }
    g.sceneNodes.push(g.node({ name: node, mesh: g.mesh(name, P.prims(mi)) }));
    g.write(join(OUT, name + '.glb'));
  };
  { // a rig: four legs in the sea, braced, a two-storey deck high over any wave, a derrick and a flare
    const P = new Plain(), b = (m, A, B, r) => P.tube(m, [A, B], r, 8);
    for (const x of [-22, 22]) for (const z of [-22, 22]) { b('leg', [x, -30, z], [x * 0.8, 34, z * 0.8], 2.4); b('band', [x * 0.96, -4, z * 0.96], [x * 0.92, 5, z * 0.92], 2.6); }
    for (const y of [8, 20]) for (const [a, c] of [[[-20, y, -20], [20, y, -20]], [[-20, y, 20], [20, y, 20]], [[-20, y, -20], [-20, y, 20]], [[20, y, -20], [20, y, 20]]]) b('leg', a, c, 0.7);
    for (const [a, c] of [[[-20, 8, -20], [20, 20, -20]], [[20, 8, 20], [-20, 20, 20]], [[-20, 8, 20], [-20, 20, -20]], [[20, 8, -20], [20, 20, 20]]]) b('leg', a, c, 0.5);
    box(P, 'deck', -28, 28, 34, 38, -28, 28); box(P, 'deck', -24, 24, 42, 45, -24, 24);
    for (const x of [-22, 22]) for (const z of [-22, 22]) b('leg', [x, 38, z], [x, 42, z], 0.8);
    box(P, 'white', -22, 2, 45, 56, -22, -2); box(P, 'red', 6, 22, 45, 49, 6, 22);
    for (let k = 0; k < 6; k++) { box(P, 'lit', -21 + k * 3.8, -19 + k * 3.8, 52, 53.4, -2.0, -1.9); box(P, 'lit', -21 + k * 3.8, -19 + k * 3.8, 48, 49.4, -2.0, -1.9); }
    for (const [x, z] of [[-5, -5], [5, -5], [-5, 5], [5, 5]]) b('derrick', [x + 14, 45, z + 14], [14, 86, 14], 0.45);
    for (let y = 52; y < 84; y += 7) { const w = 5 * (1 - (y - 45) / 41); for (const [a, c] of [[[-w, -w], [w, -w]], [[w, -w], [w, w]], [[w, w], [-w, w]], [[-w, w], [-w, -w]]]) b('derrick', [14 + a[0], y, 14 + a[1]], [14 + c[0], y, 14 + c[1]], 0.2); }
    b('leg', [-24, 44, 24], [-54, 66, 54], 0.5);
    P.tube('flare', [[-54, 66, 54], [-55, 72, 55], [-54, 78, 56], [-55, 83, 54]], (t) => 2.2 * (1 - t) + 0.3, 8);
    write('rig', 'Rig', P, { leg: [[0.34, 0.26, 0.1], 0.7, 0.3], band: [[0.05, 0.06, 0.04], 0.6], deck: [[0.26, 0.27, 0.28], 0.8, 0.2], white: [[0.6, 0.6, 0.56], 0.7], red: [[0.5, 0.1, 0.06], 0.7], derrick: [[0.6, 0.12, 0.06], 0.6, 0.3], lit: [[0.05, 0.05, 0.05], 0.3, 0, [3.2, 2.2, 1.2]], flare: [[0.3, 0.1, 0.02], 0.5, 0, [28, 11, 2]] });
  }
  { // a wreck, down by the stern and listing: y 0 the water
    const P = new Plain(), rows = [];
    for (let i = 0; i <= 12; i++) { const t = i / 12, z = -33 + 66 * t, w = 7 * (t > 0.8 ? Math.pow(1 - (t - 0.8) / 0.2, 0.55) : 1) + 0.02, tilt = t * 13 - 5, list = 2.2; rows.push([[-w, tilt + 6 - list, z], [-w * 0.9, tilt - 10, z], [w * 0.9, tilt - 10, z], [w, tilt + 6 + list, z]]); }
    P.grid('rust', rows, { flip: false });
    const d = []; for (let i = 0; i <= 12; i++) { const t = i / 12, z = -33 + 66 * t, w = 7 * (t > 0.8 ? Math.pow(1 - (t - 0.8) / 0.2, 0.55) : 1) + 0.02, tilt = t * 13 - 5; d.push([[-w, tilt + 6 - 2.2, z], [w, tilt + 6 + 2.2, z]]); }
    P.grid('deck', d, { flip: false });
    box(P, 'rust2', -5, 5, 3, 12, -30, -20);
    P.tube('rust2', [[0, 4, -8], [1.5, 20, -6]], 0.5, 6); P.tube('rust2', [[1.5, 15, -6], [8, 7, 6]], 0.14, 5);
    write('wreck', 'Wreck', P, { rust: [[0.22, 0.09, 0.05], 0.9, 0.1], rust2: [[0.15, 0.08, 0.05], 0.9, 0.1], deck: [[0.08, 0.06, 0.05], 0.9] });
  }
  { // a marker tower on a concrete foot, with a flashing top
    const P = new Plain();
    P.tube('foot', [[0, -8, 0], [0, 4.5, 0]], 3.4, 12);
    P.tube('mark', [[0, 4.5, 0], [0, 12, 0]], (t) => 1.7 - t * 0.6, 10);
    P.tube('black', [[0, 12, 0], [0, 19, 0]], (t) => 1.1 - t * 0.5, 10);
    P.tube('lamp', [[0, 19, 0], [0, 20.4, 0]], 0.55, 8);
    write('tower', 'Tower', P, { foot: [[0.3, 0.3, 0.28], 0.9], mark: [[0.72, 0.5, 0.05], 0.6], black: [[0.04, 0.04, 0.04], 0.6], lamp: [[0.2, 0.2, 0.1], 0.3, 0, [22, 18, 5]] });
  }
  { // the beacon: a tall tower for the bank's biggest rock. Local y 0 the rock's top
    const P = new Plain();
    P.tube('white', [[0, -8, 0], [0, 26, 0]], (t) => 5.4 - t * 2.2, 16);
    P.tube('red', [[0, 26, 0], [0, 40, 0]], (t) => 3.2 - t * 0.7, 16);
    P.tube('white', [[0, 40, 0], [0, 44, 0]], 3.4, 16);
    P.tube('lamp', [[0, 44, 0], [0, 49, 0]], 1.8, 12);
    P.tube('red', [[0, 49, 0], [0, 52, 0]], (t) => 2.4 * (1 - t) + 0.05, 12);
    write('beacon', 'Beacon', P, { white: [[0.72, 0.72, 0.68], 0.75], red: [[0.5, 0.08, 0.05], 0.7], lamp: [[0.3, 0.3, 0.2], 0.3, 0, [30, 27, 15]] });
  }
}

// ── Iron Quay's kit: a tanker, moored coasters, sheds and tall blocks with lit windows, stacks of containers, gantry cranes, quay lamps ──
{
  const box = (P, m, x0, x1, y0, y1, z0, z1) => {
    const q = (a, b, c, d) => P.grid(m, [[a, b], [d, c]], { flip: false });
    const v = (x, y, z) => [x, y, z];
    q(v(x0, y1, z0), v(x1, y1, z0), v(x1, y1, z1), v(x0, y1, z1)); // the top
    q(v(x1, y0, z0), v(x0, y0, z0), v(x0, y1, z0), v(x1, y1, z0)); // back
    q(v(x0, y0, z1), v(x1, y0, z1), v(x1, y1, z1), v(x0, y1, z1)); // front
    q(v(x0, y0, z0), v(x0, y0, z1), v(x0, y1, z1), v(x0, y1, z0)); // left
    q(v(x1, y0, z1), v(x1, y0, z0), v(x1, y1, z0), v(x1, y1, z1)); // right
  };
  const write = (name, node, P, cols) => {
    const g = new Glb(), mi = {};
    for (const [k, [rgb, rough, metal, em]] of Object.entries(cols)) {
      mi[k] = g.material(k, rough, metal ?? 0, em ?? null);
      g.materials[mi[k]].pbrMetallicRoughness.baseColorFactor = [...rgb, 1];
      g.materials[mi[k]].doubleSided = true;
    }
    g.sceneNodes.push(g.node({ name: node, mesh: g.mesh(name, P.prims(mi)) }));
    g.write(join(OUT, name + '.glb'));
  };
  const LIT = { windows: [[0.02, 0.02, 0.02], 0.3, 0, [3.2, 2.1, 0.9]], cold: [[0.02, 0.02, 0.02], 0.3, 0, [1.2, 1.9, 2.6]] };
  const hullShape = (P, m, L, W, top, bottom) => {
    const rows = [];
    for (let i = 0; i <= 14; i++) {
      const t = i / 14, z = -L + 2 * L * t, w = W * (t > 0.82 ? Math.pow(1 - (t - 0.82) / 0.18, 0.55) : t < 0.05 ? 0.82 + t * 3.6 : 1) + 0.02;
      rows.push([[-w, top, z], [-w * 0.92, bottom, z], [w * 0.92, bottom, z], [w, top, z]]);
    }
    P.grid(m, rows, { flip: false });
    const d = []; for (let i = 0; i <= 14; i++) { const t = i / 14, z = -L + 2 * L * t, w = W * (t > 0.82 ? Math.pow(1 - (t - 0.82) / 0.18, 0.55) : t < 0.05 ? 0.82 + t * 3.6 : 1) + 0.02; d.push([[-w, top, z], [w, top, z]]); }
    P.grid('deck', d, { flip: false });
  };
  { // the tanker: 124 m long, y 0 the waterline, forward +z
    const P = new Plain();
    hullShape(P, 'hull', 62, 11, 7, -9);
    box(P, 'boot', -11.4, 11.4, -0.8, 0.6, -56, 46);
    box(P, 'house', -8.5, 8.5, 7, 20, -58, -40);
    box(P, 'house', -6, 6, 20, 26, -55, -44);
    box(P, 'funnel', -2.5, 2.5, 20, 32, -58, -52);
    for (let k = 0; k < 7; k++) box(P, 'windows', -8.6 + k * 2.4, -7.2 + k * 2.4, 17, 18.6, -39.9, -39.8);
    for (let k = 0; k < 7; k++) box(P, 'windows', -8.6 + k * 2.4, -7.2 + k * 2.4, 13, 14.6, -39.9, -39.8);
    for (let z = -30; z <= 44; z += 12) { P.tube('pipe', [[-3, 8.6, z], [3, 8.6, z]], 0.5, 6); P.tube('pipe', [[-6, 7, z + 4], [-6, 9.4, z + 4]], 0.3, 6); }
    P.tube('pipe', [[-4, 8, -38], [-4, 8, 50]], 0.34, 6); P.tube('pipe', [[4, 8, -38], [4, 8, 50]], 0.34, 6);
    box(P, 'cold', -0.5, 0.5, 10, 11, 58, 59); box(P, 'red', -11.6, -11.2, 8, 9, 0, 1); box(P, 'green', 11.2, 11.6, 8, 9, 0, 1);
    write('tanker', 'Tanker', P, { hull: [[0.06, 0.07, 0.09], 0.6, 0.2], boot: [[0.32, 0.05, 0.03], 0.7], deck: [[0.2, 0.08, 0.06], 0.8], house: [[0.62, 0.62, 0.58], 0.6], funnel: [[0.12, 0.2, 0.34], 0.6], pipe: [[0.3, 0.26, 0.18], 0.6, 0.4], windows: LIT.windows, cold: [[0.1, 0.1, 0.1], 0.3, 0, [8, 8, 8]], red: [[0.1, 0, 0], 0.3, 0, [9, 0.5, 0.3]], green: [[0, 0.1, 0], 0.3, 0, [0.4, 9, 0.8]] });
  }
  { // a moored coaster, 80 m
    const P = new Plain();
    hullShape(P, 'hull', 40, 8, 5, -7);
    box(P, 'house', -6, 6, 5, 15, -38, -27);
    for (let k = 0; k < 5; k++) box(P, 'windows', -5.5 + k * 2.4, -4.2 + k * 2.4, 12, 13.4, -26.9, -26.8);
    for (let z = -20; z <= 30; z += 12) box(P, 'cargo', -6, 6, 5, 8.2, z - 5, z + 5);
    P.tube('mast', [[0, 5, -12], [0, 22, -12]], 0.3, 6); P.tube('mast', [[0, 20, -12], [0, 8, 24]], 0.12, 5);
    write('ship', 'Ship', P, { hull: [[0.22, 0.05, 0.05], 0.7, 0.1], deck: [[0.14, 0.14, 0.13], 0.8], house: [[0.56, 0.58, 0.56], 0.65], cargo: [[0.12, 0.24, 0.3], 0.7], mast: [[0.2, 0.2, 0.2], 0.6, 0.4], windows: LIT.windows });
  }
  { // a shed
    const P = new Plain();
    box(P, 'wall', -18, 18, 0, 11, -18, 18);
    P.grid('roof', [[[-18, 11, -18], [0, 15, -18], [18, 11, -18]], [[-18, 11, 18], [0, 15, 18], [18, 11, 18]]], { flip: false });
    for (const s of [-1, 1]) { for (let k = -2; k <= 2; k++) box(P, 'windows', k * 6.4 - 1.6, k * 6.4 + 1.6, 7.5, 9.2, s * 18.05 - 0.05, s * 18.05 + 0.05); box(P, 'door', -5, 5, 0, 5.8, s * 18.05 - 0.05, s * 18.05 + 0.05); }
    write('warehouse', 'Warehouse', P, { wall: [[0.26, 0.28, 0.3], 0.85], roof: [[0.14, 0.15, 0.17], 0.7, 0.3], door: [[0.34, 0.13, 0.05], 0.8], windows: LIT.windows });
  }
  { // an office block with some of its lights on
    const P = new Plain();
    box(P, 'wall', -14, 14, 0, 44, -14, 14);
    for (let f = 0; f < 11; f++) for (let k = -3; k <= 3; k++) for (const [s, ax] of [[1, 0], [-1, 0], [1, 1], [-1, 1]]) {
      const lit = ((f * 7 + k * 13 + s * 5 + ax * 3) % 5 + 5) % 5 < 2, m = lit ? ((f + k) % 3 ? 'windows' : 'cold') : 'glass', y0 = 2 + f * 3.9, y1 = y0 + 2.4, a = k * 3.8 - 1.2, b = k * 3.8 + 1.2;
      if (ax) box(P, m, s * 14.05 - 0.05, s * 14.05 + 0.05, y0, y1, a, b); else box(P, m, a, b, y0, y1, s * 14.05 - 0.05, s * 14.05 + 0.05);
    }
    write('block', 'Block', P, { wall: [[0.2, 0.21, 0.24], 0.8], glass: [[0.03, 0.04, 0.05], 0.2, 0.4], windows: LIT.windows, cold: LIT.cold });
  }
  { // stacks of containers
    const P = new Plain(), cols = ['c0', 'c1', 'c2', 'c3'];
    for (let row = 0; row < 3; row++) for (let i = 0; i < 4; i++) { const h = 1 + ((row * 3 + i * 5) % 4); for (let k = 0; k < h; k++) box(P, cols[(row * 7 + i * 3 + k * 5) % 4], -13 + row * 9, -13 + row * 9 + 8, k * 2.7, k * 2.7 + 2.6, -18 + i * 9, -18 + i * 9 + 8.6); }
    write('containers', 'Containers', P, { c0: [[0.46, 0.1, 0.05], 0.7], c1: [[0.06, 0.2, 0.36], 0.7], c2: [[0.5, 0.36, 0.06], 0.7], c3: [[0.06, 0.28, 0.16], 0.7] });
  }
  { // a gantry crane reaching out over the water (+z)
    const P = new Plain();
    const beam = (m, a, b, r) => P.tube(m, [a, b], r, 6);
    for (const x of [-9, 9]) for (const z of [-9, 9]) beam('crane', [x, 0, z], [x * 0.75, 36, z * 0.75], 0.8);
    for (const y of [12, 24]) { beam('crane', [-8, y, -8], [8, y, -8], 0.4); beam('crane', [-8, y, 8], [8, y, 8], 0.4); beam('crane', [-8, y, -8], [-8, y, 8], 0.4); beam('crane', [8, y, -8], [8, y, 8], 0.4); }
    box(P, 'crane', -5, 5, 34, 40, -34, 58);
    box(P, 'house', -3, 3, 28, 34, 6, 13);
    box(P, 'windows', -2.6, 2.6, 30, 32, 13, 13.1);
    beam('crane', [0, 40, 0], [0, 54, 0], 0.7); beam('crane', [0, 54, 0], [0, 40, 56], 0.2); beam('crane', [0, 54, 0], [0, 40, -32], 0.2);
    beam('cable', [0, 34, 42], [0, 14, 42], 0.09); box(P, 'crane', -6, 6, 12, 14, 39, 45);
    box(P, 'cold', -0.6, 0.6, 54, 55, -0.6, 0.6);
    write('crane', 'Crane', P, { crane: [[0.62, 0.34, 0.06], 0.65, 0.2], house: [[0.5, 0.5, 0.48], 0.7], cable: [[0.05, 0.05, 0.05], 0.6], windows: LIT.windows, cold: [[0.1, 0.1, 0.1], 0.3, 0, [8, 2, 1.4]] });
  }
  { // a quay lamp and a bollard
    const P = new Plain();
    P.tube('post', [[0, 0, 0], [0, 12, 0], [0, 12.4, 1.2]], 0.14, 6);
    box(P, 'head', -0.5, 0.5, 12.1, 12.4, 0.8, 2.2);
    P.tube('post', [[0, 0, 4.5], [0, 1, 4.5]], 0.3, 8);
    write('lamp', 'Lamp', P, { post: [[0.16, 0.17, 0.18], 0.6, 0.4], head: [[0.1, 0.1, 0.1], 0.3, 0, [14, 8.5, 3.5]] });
  }
}

// ── Red Reach's kit: a saguaro with its arms, a hoodoo of stacked sandstone, a bleached drift log ──
{
  const P = new Plain(), arm = (s, y, h, r) => { const pts = []; for (let i = 0; i <= 6; i++) { const t = i / 6, out = Math.min(1, t * 2.4); pts.push([s * (0.25 + out * 1.0), y + 1.3 * smooth(0.2, 1, t) * h, 0]); } P.tube('flesh', pts, r, 8); };
  P.tube('flesh', [[0, -0.5, 0], [0, 2, 0], [0, 4.5, 0], [0, 6.2, 0], [0, 6.7, 0]], (t) => 0.34 * (1 - Math.pow(t, 6) * 0.65), 12);
  arm(1, 2.7, 1.9, 0.2); arm(-1, 3.4, 1.5, 0.18);
  const g = new Glb(), mi = { flesh: g.material('flesh', 0.75) };
  g.materials[mi.flesh].pbrMetallicRoughness.baseColorFactor = [0.1, 0.2, 0.08, 1];
  g.sceneNodes.push(g.node({ name: 'Cactus', mesh: g.mesh('cactus', P.prims(mi)) }));
  g.write(join(OUT, 'cactus.glb'));
}
{
  const P = new Plain(), h2 = (a, b) => { let v = Math.imul(a * 374761393 + b * 668265263, 1274126177); v = Math.imul(v ^ (v >>> 13), 1274126177); return ((v ^ (v >>> 16)) >>> 0) / 4294967296; };
  const rows = [], N = 18, prof = [[1.9, -1.5], [1.75, 0], [1.3, 1.5], [1.45, 2.4], [0.95, 3.6], [0.85, 5], [1.25, 5.8], [1.3, 6.4], [0.8, 6.9], [0.02, 7.1]];
  for (let j = 0; j < prof.length; j++) { const row = []; for (let i = 0; i <= N; i++) { const a = (i % N) / N * Math.PI * 2, r = prof[j][0] * (0.85 + 0.3 * h2(j, i % N)); row.push([Math.cos(a) * r, prof[j][1], Math.sin(a) * r]); } rows.push(row); }
  P.grid('stone', rows, { wrap: true, flip: true });
  const g = new Glb(), mi = { stone: g.material('stone', 0.95) };
  g.materials[mi.stone].pbrMetallicRoughness.baseColorFactor = [0.55, 0.25, 0.14, 1];
  g.sceneNodes.push(g.node({ name: 'Hoodoo', mesh: g.mesh('hoodoo', P.prims(mi)) }));
  g.write(join(OUT, 'hoodoo.glb'));
}
{
  const P = new Plain();
  P.tube('wood', [[-2.4, 0.1, 0], [-0.6, 0.22, 0.14], [1.2, 0.2, -0.1], [2.6, 0.34, 0.1]], (t) => lerp(0.3, 0.12, t), 8);
  P.tube('wood', [[1.2, 0.2, -0.1], [1.7, 0.75, -0.45], [2.05, 1.15, -0.62]], (t) => lerp(0.1, 0.03, t), 5);
  P.tube('wood', [[-1, 0.2, 0.1], [-1.4, 0.7, 0.5]], (t) => lerp(0.1, 0.04, t), 5);
  const g = new Glb(), mi = { wood: g.material('wood', 0.9) };
  g.materials[mi.wood].pbrMetallicRoughness.baseColorFactor = [0.42, 0.37, 0.3, 1];
  g.sceneNodes.push(g.node({ name: 'Driftwood', mesh: g.mesh('driftwood', P.prims(mi)) }));
  g.write(join(OUT, 'driftwood.glb'));
}

// ── far versions of the trees, for the scatter past its LOD distance: same outline and the same materials in the same order, a tenth of the triangles ──
{
  const P = new Plain();
  P.tube('trunk', [[0, -0.4, 0], [0, 6, 0]], (t) => lerp(0.26, 0.1, t), 3, { cap: false });
  for (let t = 0; t < 3; t++) {
    const k = t / 2, y0 = 1.6 + k * 7.2, r = lerp(3.0, 1.0, k), top = y0 + lerp(4, 2.4, k), rows = [];
    for (let q = 0; q <= 1; q++) { const row = []; for (let i = 0; i <= 6; i++) { const a = (i % 6) / 6 * Math.PI * 2 + t * 0.5; row.push([Math.cos(a) * r * q, lerp(top, y0, q), Math.sin(a) * r * q]); } rows.push(row); }
    P.grid('bough', rows, { flip: true });
  }
  const g = new Glb(), mi = { trunk: g.material('trunk', 0.92), bough: g.material('bough', 0.85) };
  g.materials[mi.trunk].pbrMetallicRoughness.baseColorFactor = [0.13, 0.1, 0.08, 1];
  g.materials[mi.bough].pbrMetallicRoughness.baseColorFactor = [0.045, 0.1, 0.07, 1];
  g.materials[mi.bough].doubleSided = true;
  g.sceneNodes.push(g.node({ name: 'PineFar', mesh: g.mesh('pine', P.prims(mi)) }));
  g.write(join(OUT, 'pine_far.glb'));
}
{
  const trunk = new Plain(), crown = new Plain(), lean = 0.8, top = [lean * 1.6, 8.7, 0.3];
  trunk.tube('trunk', [[0, -0.3, 0], [lean * 0.4, 4.2, 0.1], top], (t) => lerp(0.2, 0.1, t), 3, { cap: false });
  for (let f = 0; f < 6; f++) {
    const az = f / 6 * Math.PI * 2, L = 3.8, dx = Math.cos(az), dz = Math.sin(az), side = [-dz, 0, dx], rows = [];
    for (let i = 0; i <= 2; i++) { const t = i / 2, d = t * L, y = 0.7 * Math.sin(t * 2.3) - 2.2 * t * t, w = 0.75 * Math.sin(Math.pow(t, 0.7) * Math.PI) + 0.02, c = add(top, [dx * d, y, dz * d]); rows.push([add(c, add(mul(side, -w), [0, -w * 0.5, 0])), add(c, [0, 0.03, 0]), add(c, add(mul(side, w), [0, -w * 0.5, 0]))]); }
    crown.grid('frond', rows);
  }
  const g = new Glb(), mi = { trunk: g.material('trunk', 0.92), frond: g.material('frond', 0.68) };
  g.materials[mi.trunk].pbrMetallicRoughness.baseColorFactor = [0.26, 0.2, 0.14, 1];
  g.materials[mi.frond].pbrMetallicRoughness.baseColorFactor = [0.13, 0.3, 0.06, 1];
  g.materials[mi.frond].doubleSided = true;
  g.sceneNodes.push(g.node({ name: 'PalmFar', mesh: g.mesh('palm', [...trunk.prims(mi), ...crown.prims(mi)]) }));
  g.write(join(OUT, 'palm_far.glb'));
  console.log('far trees', trunk.tris() + crown.tris());
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
