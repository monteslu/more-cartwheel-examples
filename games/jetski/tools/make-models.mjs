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
  full.push({ c: sub(r0.c, mul(dir, r0.r * 0.6)), U, V, ru: 0.002, rv: 0.002, i: r0.i, t: 0 });
  full.push({ c: sub(r0.c, mul(dir, r0.r * 0.3)), U, V, ru: r0.r * 0.8, rv: r0.r * 0.8, i: r0.i, t: 0 });
  for (const q of rings) full.push({ c: q.c, U, V, ru: q.r, rv: q.r * (o.ratio ?? 1), i: q.i, t: q.t });
  full.push({ c: add(rl.c, mul(dir, rl.r * 0.3)), U, V, ru: rl.r * 0.8, rv: rl.r * 0.8, i: rl.i, t: 1 });
  full.push({ c: add(rl.c, mul(dir, rl.r * 0.6)), U, V, ru: 0.002, rv: 0.002, i: rl.i, t: 1 });
  m.loft(mat, full, { n: o.n ?? 12, weights: (k) => {
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
torso('suit', 0, [[H - 0.12, 0.002, 0.002, [[hip, 1]]], [H - 0.1, 0.13, 0.11, [[hip, 1]]], [H - 0.04, 0.17, 0.125, [[hip, 1]]], [H + 0.05, 0.165, 0.115, [[hip, 0.8], [sp, 0.2]]], [S + 0.03, 0.145, 0.1, [[hip, 0.25], [sp, 0.75]]], [S + 0.12, 0.15, 0.1, [[sp, 0.7], [ch, 0.3]]], [C + 0.05, 0.175, 0.11, [[ch, 1]]], [C + 0.18, 0.185, 0.115, [[ch, 1]]], [C + 0.25, 0.13, 0.09, [[ch, 1]]], [N, 0.062, 0.062, [[ch, 0.6], [ne, 0.4]]], [N + 0.07, 0.052, 0.054, [[ne, 1]]], [N + 0.1, 0.002, 0.002, [[ne, 1]]]]);
// the buoyancy vest, matte, over the chest
torso('vest', 0.022, [[S - 0.04, 0.002, 0.002, [[sp, 1]]], [S - 0.02, 0.15, 0.105, [[hip, 0.3], [sp, 0.7]]], [S + 0.1, 0.152, 0.104, [[sp, 0.7], [ch, 0.3]]], [C + 0.05, 0.176, 0.115, [[ch, 1]]], [C + 0.17, 0.185, 0.118, [[ch, 1]]], [C + 0.235, 0.12, 0.085, [[ch, 1]]], [C + 0.245, 0.002, 0.002, [[ch, 1]]]]);
for (const s of ['L', 'R']) {
  limb('suit', ['Arm' + s, 'Fore' + s, 'Hand' + s], [0.062, 0.047, 0.036]);
  limb('suit', ['Thigh' + s, 'Shin' + s, 'Foot' + s], [0.092, 0.063, 0.046], { n: 14 });
  const sg = s === 'L' ? 1 : -1, hand = W['Hand' + s], foot = W['Foot' + s];
  m.capsule('glove', sub(hand, [sg * 0.02, 0, 0]), add(hand, [sg * 0.075, 0, 0]), 0.04, 0.042, { ratio: 0.72, weights: () => [[BI['Hand' + s], 1]] });
  m.capsule('boot', add(foot, [0, -0.02, -0.03]), add(foot, [0, -0.045, 0.16]), 0.056, 0.05, { ratio: 0.8, weights: () => [[BI['Foot' + s], 1]] });
}
// helmet: shell, chin bar, dark visor
const Hd = W.Head, he = BI.Head, hw = () => [[he, 1]];
m.ellipsoid('helmet', add(Hd, [0, 0.085, -0.008]), 0.117, 0.125, 0.135, { steps: 12, n: 22, weights: hw });
m.capsule('helmet', add(Hd, [-0.06, -0.0, 0.1]), add(Hd, [0.06, -0.0, 0.1]), 0.04, 0.04, { weights: hw });
m.ellipsoid('visor', add(Hd, [0, 0.095, 0.062]), 0.098, 0.052, 0.075, { steps: 8, n: 18, weights: hw });
const glb = new Glb();
const mats = { suit: glb.material('suit', 0.42), vest: glb.material('vest', 0.88), glove: glb.material('glove', 0.5), boot: glb.material('boot', 0.55), helmet: glb.material('helmet', 0.22), visor: glb.material('visor', 0.08, 0.3) };
const color = { suit: [0.05, 0.055, 0.07, 1], vest: [0.9, 0.3, 0.05, 1], glove: [0.03, 0.03, 0.035, 1], boot: [0.04, 0.04, 0.05, 1], helmet: [0.85, 0.85, 0.82, 1], visor: [0.02, 0.03, 0.04, 1] };
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
  // the hull section at u (0 stern .. 1 bow): half beam, keel depth (deadvee), chine, gunwale height
  const sec = (u) => {
    const bow = smooth(0.58, 1.0, u), sb = smooth(0.0, 0.06, u);
    const beam = lerp(0.58, 0.02, Math.pow(bow, 1.5)) * lerp(0.86, 1, sb);
    const rocker = 0.34 * Math.pow(smooth(0.6, 1.0, u), 1.6);
    return { z: lerp(-1.5, 1.6, u), beam, keel: -0.2 + rocker, chine: -0.09 + rocker * 0.85, gun: 0.2 + rocker * 0.55 + 0.04 * Math.sin(u * Math.PI) };
  };
  const US = 28, lower = [], upper = [], deck = [];
  for (let k = 0; k <= US; k++) {
    const u = k / US, s = sec(u), row = [];
    // port gunwale, chine, keel, chine, starboard gunwale
    const pts = [[-s.beam, s.gun], [-s.beam * 1.02, s.gun * 0.45 + s.chine * 0.55], [-s.beam * 0.92, s.chine], [-s.beam * 0.5, (s.chine + s.keel) / 2 ], [0, s.keel], [s.beam * 0.5, (s.chine + s.keel) / 2], [s.beam * 0.92, s.chine], [s.beam * 1.02, s.gun * 0.45 + s.chine * 0.55], [s.beam, s.gun]];
    for (const [x, y] of pts) row.push([x, y, s.z]);
    lower.push(row);
    // deck: domed over the gunwales, higher amidships into the steering tower, a footwell each side of the seat
    const drow = [];
    for (let i = 0; i <= 12; i++) {
      const t = i / 12 * 2 - 1, ax = Math.abs(t);
      const crown = 0.1 + 0.05 * (1 - u) ;
      const tower = 0.34 * Math.exp(-Math.pow((u - 0.62) / 0.1, 2)) * (1 - ax * ax);
      const nose = 0.22 * smooth(0.62, 0.85, u) * (1 - smooth(0.85, 1, u)) * (1 - ax * ax);
      const well = -0.13 * smooth(0.32, 0.48, ax) * (1 - smooth(0.78, 0.96, ax)) * smooth(0.05, 0.12, u) * (1 - smooth(0.46, 0.58, u));
      drow.push([t * s.beam, s.gun + crown * (1 - ax * ax) + tower + nose + well, s.z]);
    }
    deck.push(drow);
  }
  P.grid('hull', lower, { flip: true });
  P.grid('deck', deck);
  // the stern and the bow closed
  const sr = sec(0);
  P.grid('hull', [lower[0], lower[0].map((p) => [0, p[1] * 0.2 + 0.02, sr.z])], { flip: false });
  // the seat: a long vinyl saddle
  const seat = [];
  for (let k = 0; k <= 16; k++) {
    const t = k / 16, z = lerp(-1.32, 0.22, t), wid = 0.2 * Math.pow(Math.sin(Math.PI * clamp(t * 0.96 + 0.02)), 0.35), row = [];
    const top = 0.5 + 0.1 * Math.sin(t * Math.PI) - 0.1 * (1 - smooth(0, 0.2, t));
    for (let i = 0; i <= 10; i++) { const a = i / 10 * Math.PI; row.push([Math.cos(a) * wid * 1.05, 0.28 + (top - 0.28) * Math.pow(Math.sin(a), 0.5), z]); }
    seat.push(row);
  }
  P.grid('seat', seat, { flip: true });
  // rub rail and stern step in dark rubber
  for (const s of [-1, 1]) P.tube('trim', Array.from({ length: 20 }, (_, i) => { const q = sec(0.02 + i / 19 * 0.78); return [s * (q.beam + 0.012), q.gun - 0.02, q.z]; }), 0.022, 6, {});
  // jet nozzle
  P.tube('metal', [[0, -0.06, -1.5], [0, -0.05, -1.62]], (t) => lerp(0.1, 0.08, t), 12);
  // the cowl behind the bars
  const cowl = [];
  for (let k = 0; k <= 8; k++) { const t = k / 8, z = lerp(0.22, 0.6, t), row = []; for (let i = 0; i <= 10; i++) { const a = i / 10 * Math.PI; const w = 0.17 * (1 - 0.25 * t); row.push([Math.cos(a) * w, 0.6 + (0.22 * Math.pow(Math.sin(a), 0.7)) * (1 - 0.6 * t) - 0.05 * t, z]); } cowl.push(row); }
  P.grid('trim', cowl, { flip: true });
  const bars = new Plain();
  // the handlebars: a separate node that steers, about (0, 0.86, 0.3)
  bars.tube('metal', [[0, -0.28, 0.04], [0, 0, 0]], 0.03, 8);
  bars.tube('metal', [[-0.31, 0.02, -0.03], [-0.15, 0.0, 0.0], [0, -0.01, 0.0], [0.15, 0.0, 0.0], [0.31, 0.02, -0.03]], 0.014, 8);
  for (const s of [-1, 1]) bars.tube('trim', [[s * 0.23, 0.015, -0.015], [s * 0.34, 0.025, -0.033]], 0.02, 8);
  const g = new Glb();
  const mi = { hull: g.material('hull', 0.36), deck: g.material('deck', 0.24), seat: g.material('seat', 0.42), trim: g.material('trim', 0.62), metal: g.material('metal', 0.3, 0.9) };
  const col = { hull: [0.06, 0.07, 0.09, 1], deck: [0.88, 0.1, 0.06, 1], seat: [0.03, 0.03, 0.035, 1], trim: [0.02, 0.02, 0.025, 1], metal: [0.6, 0.62, 0.64, 1] };
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
