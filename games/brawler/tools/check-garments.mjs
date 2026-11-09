// Garment shape audit of the generated characters: reads the glb files (the bind pose, as they ship).
//  The gut (B-PT22): the belly's depth up the middle of the front: its peak stands above the belt, it spans the soft front from under the ribs, its
//  bottom is round and not a point (radius of curvature at the peak), it is wide (flanks grow with it) and round in plan (not a lump at the very front).
//  The sleeves cut off a man's T-shirt (B-PT23): from above, along the shoulder line, how much cloth lies between the neck and the bare arm, in metres on the scaled body.
//   node tools/check-garments.mjs [--dir <models>]
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
let DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'models');
for (let i = 0; i < args.length; i++) if (args[i] === '--dir') DIR = args[++i];

function load(name) {
  const b = readFileSync(join(DIR, name + '.glb')), jl = b.readUInt32LE(12), json = JSON.parse(b.subarray(20, 20 + jl).toString()), bin = b.subarray(20 + jl + 8);
  const pos = (acc) => { const A = json.accessors[acc], V = json.bufferViews[A.bufferView], off = (V.byteOffset ?? 0) + (A.byteOffset ?? 0), st = V.byteStride ?? 12, o = []; for (let i = 0; i < A.count; i++) o.push([bin.readFloatLE(off + i * st), bin.readFloatLE(off + i * st + 4), bin.readFloatLE(off + i * st + 8)]); return o; };
  const idx = (acc) => { const A = json.accessors[acc], V = json.bufferViews[A.bufferView], off = (V.byteOffset ?? 0) + (A.byteOffset ?? 0), o = []; for (let i = 0; i < A.count; i++) o.push(A.componentType === 5125 ? bin.readUInt32LE(off + i * 4) : bin.readUInt16LE(off + i * 2)); return o; };
  const node = (n) => json.nodes.find((x) => x.name === n);
  const prims = json.meshes[node('Body').mesh].primitives.map((p) => ({ mat: json.materials[p.material].name, P: pos(p.attributes.POSITION), I: idx(p.indices) }));
  const par = new Map(); json.nodes.forEach((n, i) => (n.children ?? []).forEach((c) => par.set(c, i)));
  const wy = (name) => { let v = [0, 0, 0]; for (let k = json.nodes.indexOf(node(name)); k !== undefined; k = par.get(k)) { const n = json.nodes[k], t = n.translation ?? [0, 0, 0], s = n.scale?.[0] ?? 1, root = par.get(k) === undefined, tt = root ? t.map((x) => x / s) : t; v = v.map((x, i) => x + tt[i]); } return v; };
  return { prims, wy, scale: node('Hips').scale?.[0] ?? 1 };
}

/** the gut of a shirt: the front line (x near 0) as depth against the straight line from the belt to the base of the ribs */
function gut(name) {
  const M = load(name), yH = M.wy('Hips')[1], yC = M.wy('Chest')[1], waist = yH + 0.06, top = yC + 0.04;
  const S = M.prims.filter((p) => /shirt/.test(p.mat)).flatMap((p) => p.P);
  const line = [...new Map(S.filter((p) => Math.abs(p[0]) < 0.012 && p[2] > 0 && p[1] > waist + 0.015 && p[1] < top + 0.03).map((p) => [Math.round(p[1] * 2000), p])).values()].sort((a, b) => a[1] - b[1]).map((p) => [p[1], p[2]]);
  const zAt = (y) => { let i = 0; while (i < line.length - 2 && line[i + 1][0] < y) i++; const A = line[i], B = line[i + 1], t = (y - A[0]) / (B[0] - A[0] || 1); return A[1] + (B[1] - A[1]) * t; };
  const y0 = line[0][0], y1 = line[line.length - 1][0];
  // the base: the jeans at the waistband to the ribs, what the front would be with no belly
  const zPants = Math.max(...M.prims.filter((p) => /pants/.test(p.mat)).flatMap((p) => p.P).filter((p) => Math.abs(p[0]) < 0.02 && Math.abs(p[1] - waist) < 0.03).map((p) => p[2])), base = (y) => zPants + ((zAt(y1) - zPants) * (y - waist)) / (y1 - waist);
  const N = 200, prof = []; for (let i = 0; i <= N; i++) { const y = y0 + ((y1 - y0) * i) / N; prof.push([y, zAt(y) - base(y)]); }
  let pk = prof[0]; for (const q of prof) if (q[1] > pk[1]) pk = q;
  const half = prof.filter((q) => q[1] >= pk[1] * 0.5), span = half[half.length - 1][0] - half[0][0];
  // curvature at the peak over 3 cm above and below: a pointed sack has a small radius
  const h = 0.03, rad = (dy) => (dy * dy) / (2 * Math.max(1e-5, pk[1] - (zAt(pk[0] + dy) - base(pk[0] + dy))));
  const radius = Math.min(rad(h), rad(-h));
  // width: the half width of the shirt at the flanks against the jeans at the waistband under them
  const halfW = (y) => Math.max(...S.filter((p) => Math.abs(p[1] - y) < 0.016).map((p) => Math.abs(p[0])));
  const pantsW = Math.max(...M.prims.filter((p) => /pants/.test(p.mat)).flatMap((p) => p.P).filter((p) => Math.abs(p[1] - waist) < 0.03).map((p) => Math.abs(p[0])));
  const flank = Math.max(...[0.04, 0.07, 0.1, 0.13].map((d) => halfW(waist + 0.018 + d))) - pantsW;
  // plan: at the peak's height, the front 60% of the way out to the widest point: an oval stays near 0.8 of its depth, a lump falls away
  const ring = S.filter((p) => Math.abs(p[1] - pk[0]) < 0.018 && p[2] > 0), w = Math.max(...ring.map((p) => Math.abs(p[0]))), zw = Math.min(...ring.filter((p) => Math.abs(p[0]) > w * 0.96).map((p) => p[2]));
  const near = ring.filter((p) => Math.abs(Math.abs(p[0]) - 0.6 * w) < 0.03), zF = Math.max(...ring.map((p) => p[2]));
  const plan = near.length ? (near.reduce((a, p) => a + p[2], 0) / near.length - zw) / (zF - zw) : 0;
  // the soft chest: the two sides of the chest against its middle, under the collar
  const chestY = yC + 0.035, cb = S.filter((p) => Math.abs(p[1] - chestY) < 0.03 && p[2] > 0), cMid = Math.max(...cb.filter((p) => Math.abs(p[0]) < 0.015).map((p) => p[2])), cSide = Math.max(...cb.filter((p) => Math.abs(p[0]) > 0.05 && Math.abs(p[0]) < 0.12).map((p) => p[2]));
  return { depth: pk[1], peakOverBelt: pk[0] - waist, span, radius, flank, plan, softChest: cSide - cMid };
}

/** along the shoulder line, seen from above: the run of shirt on top, from the neck out to where anything else is on top; metres on the scaled body */
function panel(name) {
  const M = load(name), ySh = M.wy('UpperArmL')[1], hits = new Map();
  for (const p of M.prims) for (let t = 0; t < p.I.length; t += 3) {
    const [A, B, C] = [p.P[p.I[t]], p.P[p.I[t + 1]], p.P[p.I[t + 2]]];
    if (Math.max(A[1], B[1], C[1]) < ySh - 0.08 || Math.min(A[2], B[2], C[2]) > 0.012 || Math.max(A[2], B[2], C[2]) < -0.012) continue;
    const x0 = Math.min(A[0], B[0], C[0]), x1 = Math.max(A[0], B[0], C[0]);
    for (let xi = Math.ceil(x0 / 0.004); xi * 0.004 <= x1; xi++) {
      if (xi <= 0) continue;
      const x = xi * 0.004, z = 0, d = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2]);
      if (Math.abs(d) < 1e-12) continue;
      const u = ((B[2] - C[2]) * (x - C[0]) + (C[0] - B[0]) * (z - C[2])) / d, v = ((C[2] - A[2]) * (x - C[0]) + (A[0] - C[0]) * (z - C[2])) / d, w = 1 - u - v;
      if (u < -1e-4 || v < -1e-4 || w < -1e-4) continue;
      const y = u * A[1] + v * B[1] + w * C[1], h = hits.get(xi);
      if (!h || y > h.y) hits.set(xi, { y, mat: p.mat });
    }
  }
  const xs = [...hits.keys()].sort((a, b) => a - b); let run = 0, best = 0;
  for (const xi of xs) { if (/shirt/.test(hits.get(xi).mat)) { run += 0.004; best = Math.max(best, run); } else run = 0; }
  return best * M.scale;
}

/** the creep's coat through his clips, on the CPU: joints and keys from the glb, every vertex skinned the way the host does it */
function posedRig(name) {
  const b = readFileSync(join(DIR, name + '.glb')), jl = b.readUInt32LE(12), json = JSON.parse(b.subarray(20, 20 + jl).toString()), bin = b.subarray(20 + jl + 8);
  const acc = (i) => { const A = json.accessors[i], V = json.bufferViews[A.bufferView], off = (V.byteOffset ?? 0) + (A.byteOffset ?? 0), comps = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[A.type], o = [], sz = { 5126: 4, 5125: 4, 5123: 2, 5121: 1 }[A.componentType], st = V.byteStride ?? comps * sz, rd = (p) => (A.componentType === 5126 ? bin.readFloatLE(p) : A.componentType === 5125 ? bin.readUInt32LE(p) : A.componentType === 5123 ? bin.readUInt16LE(p) : bin.readUInt8(p)); for (let k = 0; k < A.count; k++) { const e = []; for (let c = 0; c < comps; c++) e.push(rd(off + k * st + c * sz)); o.push(e); } return o; };
  const body = json.meshes[json.nodes.find((n) => n.name === 'Body').mesh].primitives.map((p) => ({ mat: json.materials[p.material].name, P: acc(p.attributes.POSITION), J: acc(p.attributes.JOINTS_0), W: acc(p.attributes.WEIGHTS_0), I: acc(p.indices).map((x) => x[0]) }));
  const skin = json.skins[0], joints = skin.joints, ibm = acc(skin.inverseBindMatrices), bind = ibm.map((m) => [-m[12], -m[13], -m[14]]);
  const par = new Map(); json.nodes.forEach((n, i) => (n.children ?? []).forEach((c) => par.set(c, i)));
  const order = [...joints].sort((x, y) => { const d = (k) => { let n = 0; for (let q = par.get(k); q !== undefined; q = par.get(q)) n++; return n; }; return d(x) - d(y); });
  const clips = Object.fromEntries(json.animations.map((an) => [an.name, { len: 0, ch: an.channels.map((c) => { const smp = an.samplers[c.sampler], t = acc(smp.input).map((x) => x[0]), v = acc(smp.output); return { node: c.target.node, path: c.target.path, t, v }; }) }]));
  for (const c of Object.values(clips)) c.len = Math.max(...c.ch.map((h) => h.t[h.t.length - 1]));
  const mq = (a, b) => [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0], a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
  const rv = (q, v) => { const r = mq(mq(q, [v[0], v[1], v[2], 0]), [-q[0], -q[1], -q[2], q[3]]); return [r[0], r[1], r[2]]; };
  const sample = (h, t) => { let i = 0; while (i < h.t.length - 2 && h.t[i + 1] < t) i++; const u = Math.max(0, Math.min(1, (t - h.t[i]) / (h.t[i + 1] - h.t[i] || 1))); let o = h.v[i].map((x, k) => x + (h.v[i + 1][k] - x) * u); if (h.path === 'rotation') { const l = Math.hypot(...o) || 1; o = o.map((x) => x / l); } return o; };
  /** world of every joint for a clip's time; extra: more turns on named joints (a spring at the end of its cone) */
  const pose = (clip, t, extra = {}) => {
    const R = new Map(), T = new Map(); if (clip) for (const h of clips[clip].ch) (h.path === 'rotation' ? R : T).set(h.node, sample(h, t));
    const W = new Map();
    for (const k of order) { const n = json.nodes[k], tr = T.get(k) ?? n.translation ?? [0, 0, 0], ex = extra[n.name], q0 = R.get(k) ?? [0, 0, 0, 1], q = ex ? mq(q0, ex) : q0, p = W.get(par.get(k)); W.set(k, p ? { p: p.p.map((x, i) => x + rv(p.q, tr.map((v) => v * p.s))[i]), q: mq(p.q, q), s: p.s } : { p: tr, q, s: n.scale?.[0] ?? 1 }); }
    return W;
  };
  const skinned = (W, prim) => prim.P.map((v, i) => { const o = [0, 0, 0]; for (let c = 0; c < 4; c++) { const w = prim.W[i][c]; if (!w) continue; const jn = prim.J[i][c], G = W.get(joints[jn]), d = bind[jn], r = rv(G.q, [(v[0] - d[0]) * G.s, (v[1] - d[1]) * G.s, (v[2] - d[2]) * G.s]); for (let k = 0; k < 3; k++) o[k] += w * (G.p[k] + r[k]); } return o; });
  const jointOf = (n) => json.nodes.findIndex((x) => x.name === n);
  return { body, clips, pose, skinned, jointOf, rv };
}

/** from the front at the turn the game draws him (yaw): of the briefs he would show with no coat on, how much still shows with it */
function shows(R, W, yaw) {
  const c = Math.cos(yaw), s = Math.sin(yaw), cell = 0.01, buf = new Map(), only = new Map();
  for (const p of R.body) {
    if (!/pants|outer|shirt|legs|skin/.test(p.mat)) continue;
    const V = R.skinned(W, p).map((v) => [v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c]);
    for (let t = 0; t < p.I.length; t += 3) {
      const A = V[p.I[t]], B = V[p.I[t + 1]], C = V[p.I[t + 2]], d = (B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1]);
      if (Math.abs(d) < 1e-9) continue;
      for (let x = Math.ceil(Math.min(A[0], B[0], C[0]) / cell); x * cell <= Math.max(A[0], B[0], C[0]); x++) for (let y = Math.ceil(Math.min(A[1], B[1], C[1]) / cell); y * cell <= Math.max(A[1], B[1], C[1]); y++) {
        const u = ((B[1] - C[1]) * (x * cell - C[0]) + (C[0] - B[0]) * (y * cell - C[1])) / d, v = ((C[1] - A[1]) * (x * cell - C[0]) + (A[0] - C[0]) * (y * cell - C[1])) / d, w = 1 - u - v;
        if (u < 0 || v < 0 || w < 0) continue;
        const z = u * A[2] + v * B[2] + w * C[2], key = x * 100000 + y;
        if (!(buf.get(key)?.z > z)) buf.set(key, { z, mat: p.mat });
        if (!/outer/.test(p.mat) && !(only.get(key)?.z > z)) only.set(key, { z, mat: p.mat });
      }
    }
  }
  let n = 0, vis = 0; for (const [k, o] of only) if (/pants/.test(o.mat)) { n++; if (/pants/.test(buf.get(k).mat)) vis++; }
  return n ? vis / n : 0;
}

const CREEP_PLAYS = ['idle', 'walk', 'lurch', 'punch1', 'flash', 'block', 'hit', 'hit_gut', 'hit_hard', 'react_fear', 'react_rage', 'react_bliss', 'knockdown', 'down', 'dead', 'getup', 'getup_prone', 'grabbed', 'thrown'];
/** the coat against his bare legs, every clip he plays: coat vertices inside the legs as the mesh makes them; deepest in cm, with the coat's springs at rest and swung to the end of their cone */
function creepCoat() {
  const R = posedRig('creep'), outer = R.body.filter((p) => /outer/.test(p.mat)), legs = R.body.filter((p) => /legs/.test(p.mat));
  const pts = (W, prims) => prims.flatMap((p) => R.skinned(W, p));
  const Lbind = pts(R.pose(null, 0), legs);
  // thickness of each leg bone from its own mesh at rest
  const bones = ['L', 'R'].flatMap((sd) => [['UpperLeg' + sd, 'LowerLeg' + sd], ['LowerLeg' + sd, 'Foot' + sd]]);
  const seg = (W, a, b) => [W.get(R.jointOf(a)).p, W.get(R.jointOf(b)).p];
  const along = (P, A, B) => { const d = B.map((x, i) => x - A[i]), l2 = d[0] ** 2 + d[1] ** 2 + d[2] ** 2, t = ((P[0] - A[0]) * d[0] + (P[1] - A[1]) * d[1] + (P[2] - A[2]) * d[2]) / l2; const q = A.map((x, i) => x + d[i] * Math.max(0, Math.min(1, t))); return { t, r: Math.hypot(P[0] - q[0], P[1] - q[1], P[2] - q[2]) }; };
  const W0 = R.pose(null, 0), N = 12, rad = bones.map(([a, b]) => { const [A, B] = seg(W0, a, b), o = new Array(N).fill(0); for (const P of Lbind) { if (P[0] * A[0] < 0) continue; /* his own leg */ const q = along(P, A, B); if (q.t >= 0 && q.t <= 1 && q.r < 0.12) { const k = Math.min(N - 1, Math.floor(q.t * N)); o[k] = Math.max(o[k], q.r); } } const mx = Math.max(...o); for (let k = 1; k < N; k++) o[k] ||= o[k - 1]; return o.map((x) => x || mx); });
  const swing = (x, z) => { const qx = [Math.sin(x / 2), 0, 0, Math.cos(x / 2)], qz = [0, 0, Math.sin(z / 2), Math.cos(z / 2)]; return { CoatL: [qx[0], 0, qz[2], Math.hypot(qx[3], 0) * qz[3]], CoatR: [qx[0], 0, qz[2], qx[3] * qz[3]] }; };
  // how far the tails really swing (spring command in a live fight, B-PT24): up to 0.6 rad behind him when he moves forward (the cone), little when he stands
  const standing = { rest: {}, forward: swing(-0.1, 0), back: swing(0.1, 0) }, moving = { rest: {}, forward: swing(-0.3, 0), back: swing(0.6, 0) }, MOVES = ['walk', 'lurch', 'hit_hard', 'thrown', 'knockdown', 'flash'];
  const res = {}, deepest = { d: 0 };
  for (const [clip, c] of Object.entries(R.clips)) {
    if (!CREEP_PLAYS.includes(clip)) continue; // kicks and the like are the fighters': he never plays them
    let worst = 0;
    for (let t = 0; t <= c.len; t += 1 / 15) for (const [cn, ex] of Object.entries(MOVES.includes(clip) ? moving : standing)) {
      const W = R.pose(clip, t, ex), P = pts(W, outer);
      for (let b = 0; b < bones.length; b++) { const [A, B] = seg(W, bones[b][0], bones[b][1]); for (const v of P) { const q = along(v, A, B); if (q.t < 0.02 || q.t > 0.98) continue; const d = rad[b][Math.min(N - 1, Math.floor(q.t * N))] - q.r; if (d > worst) worst = d; if (d > deepest.d) Object.assign(deepest, { d, clip, t, cn, bone: bones[b][0] }); } }
    }
    res[clip] = worst;
  }
  // the flash, open: the front edge in the hand, the briefs in view from the street, the coat hanging clear of the body
  const tOpen = 0.65, Wo = R.pose('flash', tOpen), yaw = 0.3;
  const gripD = ['L', 'R'].map((sd) => { const J = R.jointOf('Hand' + sd), G = Wo.get(J), palm = G.p.map((x, i) => x + R.rv(G.q, [0, -0.05 * G.s, 0])[i]); let best = 9; for (const v of pts(Wo, outer)) best = Math.min(best, Math.hypot(v[0] - palm[0], v[1] - palm[1], v[2] - palm[2])); return best; });
  const open = Math.max(shows(R, Wo, yaw), shows(R, Wo, -yaw)), openW = Math.min(shows(R, Wo, yaw), shows(R, Wo, -yaw));
  const idle = shows(R, R.pose('idle', 0), 1.5708), closedFront = Math.max(shows(R, R.pose('lurch', 0.3), yaw), shows(R, R.pose('lurch', 0.3), -yaw));
  return { res, deepest, gripD, open, openW, idle, closedFront, frames: Object.fromEntries([0.2, 0.4, 0.45, 0.5, 0.65, 0.8, 0.95, 1.05].map((t) => [t, shows(R, R.pose('flash', t), yaw)])) };
}

const COAT = { showsOpen: 0.9, showsClosed: 0.55, grip: 0.08, inLegs: 0.025 };
const ONLY_COAT_OK = (a) => !a.includes('--no-coat');
let bad = 0;
const row = (n, o) => console.log(n.padEnd(11), Object.entries(o).map(([k, v]) => `${k} ${v.toFixed(3)}`).join('  '));
// the bounds, metres in the model's units: a real gut on the heckler
{
  const n = 'heckler', g = gut(n), off = [];
  {
    if (g.depth < 0.1) off.push(`depth ${g.depth.toFixed(3)} under 0.1`);
    if (g.span < 0.17) off.push(`half height span ${g.span.toFixed(3)} under 0.17`);
    if (g.radius < 0.05) off.push(`bottom radius ${g.radius.toFixed(3)} under 0.05: pointed`);
    if (g.flank < 0.03) off.push(`flanks grow ${g.flank.toFixed(3)} under 0.03`);
    if (g.plan < 0.7) off.push(`plan ${g.plan.toFixed(3)} under 0.7: a lump at the front`);
  }
  if (g.peakOverBelt < 0.09) off.push(`peak ${g.peakOverBelt.toFixed(3)} over the belt, under 0.09: hangs at the belt`);
  row(n, g); if (off.length) { bad++; console.log('  BAD ' + off.join('; ')); }
}
for (const n of ['jet', 'bruiser']) {
  const w = panel(n); console.log(n.padEnd(11), `shirt from the neck to the arm hole ${(w * 100).toFixed(1)} cm`);
  if (!(w >= 0.1 && w <= 0.15)) { bad++; console.log(`  BAD not within 10 to 15 cm: not a sleeveless T-shirt`); }
}
if (ONLY_COAT_OK(args)) {
  const c = creepCoat();
  const top = Object.entries(c.res).sort((a, b) => b[1] - a[1]).slice(0, 16).map(([k, v]) => `${k} ${(v * 100).toFixed(1)}`).join(', ');
  console.log('creep       coat inside the bare legs, deepest cm:', top);
  console.log('creep       deepest', JSON.stringify({ ...c.deepest, d: +(c.deepest.d * 100).toFixed(1), t: +(c.deepest.t ?? 0).toFixed(2) }));
  console.log('creep       flash: grip', c.gripD.map((x) => (x * 100).toFixed(1)).join(' / '), 'cm; briefs in view open', c.openW.toFixed(2), 'to', c.open.toFixed(2), '; closed side on', c.idle.toFixed(2), 'and turned as in the flash', c.closedFront.toFixed(2), '; over the clip', Object.entries(c.frames).map(([t, v]) => t + ':' + v.toFixed(2)).join(' '));
  const off = [];
  if (c.openW < COAT.showsOpen) off.push(`briefs ${c.openW.toFixed(2)} in view with the coat open, under ${COAT.showsOpen}`);
  if (c.closedFront > COAT.showsClosed) off.push(`briefs ${c.closedFront.toFixed(2)} in view with the coat closed, over ${COAT.showsClosed}: nothing left for the flash to show`);
  if (Math.max(...c.gripD) > COAT.grip) off.push(`front edge ${(Math.max(...c.gripD) * 100).toFixed(1)} cm from the hand, over ${COAT.grip * 100}`);
  if (c.deepest.d > COAT.inLegs) off.push(`coat ${(c.deepest.d * 100).toFixed(1)} cm inside a bare leg (${c.deepest.clip} at ${(c.deepest.t ?? 0).toFixed(2)}, ${c.deepest.cn}), over ${COAT.inLegs * 100}`);
  if (off.length) { bad++; console.log('  BAD ' + off.join('; ')); }
}
console.log(bad ? `${bad} garments wrong` : 'all garments right');
process.exit(bad ? 1 : 0);
