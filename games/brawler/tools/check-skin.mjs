// Skinning audit for the generated characters: poses every clip on the CPU (linear blend skinning, as the engine does)
// and reports triangles that stretch far beyond their rest size (webbing between limbs: a strip of one leg weighted to the
// other leg reads as "gum" between the legs on wide poses), and vertices weighted to limbs on both sides of the body.
//   node tools/check-skin.mjs [--max 8] [--clips kick,jumpkick,...] [name ...]
// CHECK_SKIN_DIR=<dir> audits another models folder (the control: the models before the leg fix must fail).
// Exit 1 when any character has a stretched triangle over --max or a vertex weighted to both legs (or both arms).
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const DIR = process.env.CHECK_SKIN_DIR || join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'models');
const args = process.argv.slice(2);
let MAX = 8.0, CLIPS = null, VERBOSE = false;
const names = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--max') MAX = +args[++i];
  else if (args[i] === '--clips') CLIPS = args[++i].split(',');
  else if (args[i] === '-v') VERBOSE = true;
  else names.push(args[i]);
}
const ALL = ['rook', 'mika', 'jet', 'thug', 'punk', 'runner', 'knifer', 'bruiser', 'thrower', 'guard', 'foreman', 'matron', 'ringmaster', 'colossus', 'arthur'];

function readGlb(path) {
  const b = readFileSync(path);
  const jl = b.readUInt32LE(12), json = JSON.parse(b.subarray(20, 20 + jl).toString());
  const bin = b.subarray(20 + jl + 8);
  const acc = (i) => {
    const a = json.accessors[i], v = json.bufferViews[a.bufferView], n = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[a.type];
    const C = { 5126: Float32Array, 5125: Uint32Array, 5123: Uint16Array, 5121: Uint8Array }[a.componentType];
    const off = bin.byteOffset + v.byteOffset + (a.byteOffset || 0);
    return new C(bin.buffer.slice(off, off + a.count * n * C.BYTES_PER_ELEMENT));
  };
  return { json, acc };
}
// quaternion and matrix helpers (column-major 4x4)
const qmul = (a, b) => [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0], a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
function trs(t = [0, 0, 0], q = [0, 0, 0, 1], s = [1, 1, 1]) {
  const [x, y, z, w] = q;
  return [
    (1 - 2 * (y * y + z * z)) * s[0], 2 * (x * y + z * w) * s[0], 2 * (x * z - y * w) * s[0], 0,
    2 * (x * y - z * w) * s[1], (1 - 2 * (x * x + z * z)) * s[1], 2 * (y * z + x * w) * s[1], 0,
    2 * (x * z + y * w) * s[2], 2 * (y * z - x * w) * s[2], (1 - 2 * (x * x + y * y)) * s[2], 0,
    t[0], t[1], t[2], 1];
}
function mm(a, b) {
  const o = new Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) { let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s; }
  return o;
}
function sample(times, vals, n, t) {
  if (t <= times[0]) return Array.from(vals.subarray(0, n));
  for (let i = 1; i < times.length; i++) if (t <= times[i]) {
    const u = (t - times[i - 1]) / (times[i] - times[i - 1]), a = vals.subarray((i - 1) * n, i * n), b = vals.subarray(i * n, (i + 1) * n);
    const o = Array.from(a, (x, k) => x + (b[k] - x) * u);
    if (n === 4) { const l = Math.hypot(...o) || 1; return o.map((x) => x / l); }
    return o;
  }
  return Array.from(vals.subarray(vals.length - n));
}

let bad = 0;
for (const name of names.length ? names : ALL) {
  const { json, acc } = readGlb(join(DIR, name + '.glb'));
  const skin = json.skins[0], joints = skin.joints, ibm = acc(skin.inverseBindMatrices);
  const parent = new Map();
  json.nodes.forEach((n, i) => (n.children || []).forEach((c) => parent.set(c, i)));
  const jname = joints.map((j) => json.nodes[j].name);
  const side = jname.map((n) => (/(Arm|Leg|Foot|Hand|Fing|Thumb|Coat)L/.test(n) || /L\d?$/.test(n) && /Fing|Thumb/.test(n) ? 'L' : /(Arm|Leg|Foot|Hand|Fing|Thumb|Coat)R/.test(n) ? 'R' : ''));
  const limb = jname.map((n) => (/Leg|Foot/.test(n) ? 'leg' : /Arm|Hand|Fing|Thumb/.test(n) ? 'arm' : ''));
  // geometry
  const prims = [];
  for (const node of json.nodes) if (node.mesh != null && node.skin != null) for (const p of json.meshes[node.mesh].primitives)
    prims.push({ mat: json.materials[p.material].name, P: acc(p.attributes.POSITION), J: acc(p.attributes.JOINTS_0), W: acc(p.attributes.WEIGHTS_0), I: acc(p.indices) });
  // vertices weighted to the same limb on both sides
  let cross = 0; const crossMats = new Map();
  for (const p of prims) for (let v = 0; v < p.P.length / 3; v++) {
    const s = { L: { leg: 0, arm: 0 }, R: { leg: 0, arm: 0 } };
    for (let k = 0; k < 4; k++) { const j = p.J[v * 4 + k], w = p.W[v * 4 + k]; if (side[j] && limb[j]) s[side[j]][limb[j]] += w; }
    if ((s.L.leg > 0.02 && s.R.leg > 0.02) || (s.L.arm > 0.02 && s.R.arm > 0.02)) { cross++; crossMats.set(p.mat, (crossMats.get(p.mat) || 0) + 1); }
  }
  // stretch over every clip
  const rest = json.nodes.map((n) => ({ t: n.translation, r: n.rotation, s: n.scale }));
  let worst = { r: 0 }; const hot = new Map();
  for (const an of json.animations) {
    if (CLIPS && !CLIPS.includes(an.name)) continue;
    const chans = an.channels.map((c) => ({ node: c.target.node, path: c.target.path, T: acc(an.samplers[c.sampler].input), V: acc(an.samplers[c.sampler].output) }));
    const len = Math.max(...chans.map((c) => c.T[c.T.length - 1]));
    for (let f = 0; f <= 12; f++) {
      const t = (f / 12) * len;
      const loc = rest.map((r) => ({ t: r.t, r: r.r, s: r.s }));
      for (const c of chans) {
        const v = sample(c.T, c.V, c.path === 'rotation' ? 4 : 3, t);
        if (c.path === 'rotation') loc[c.node] = { ...loc[c.node], r: v }; else loc[c.node] = { ...loc[c.node], t: v };
      }
      const world = new Map();
      const W = (i) => { if (world.has(i)) return world.get(i); const l = trs(loc[i].t, loc[i].r, loc[i].s); const m = parent.has(i) ? mm(W(parent.get(i)), l) : l; world.set(i, m); return m; };
      const skinM = joints.map((j, k) => mm(W(j), Array.from(ibm.subarray(k * 16, k * 16 + 16))));
      for (const p of prims) {
        const nv = p.P.length / 3, Q = new Float32Array(nv * 3);
        for (let v = 0; v < nv; v++) {
          const x = p.P[v * 3], y = p.P[v * 3 + 1], z = p.P[v * 3 + 2];
          let ox = 0, oy = 0, oz = 0;
          for (let k = 0; k < 4; k++) {
            const w = p.W[v * 4 + k]; if (!w) continue; const M = skinM[p.J[v * 4 + k]];
            ox += w * (M[0] * x + M[4] * y + M[8] * z + M[12]); oy += w * (M[1] * x + M[5] * y + M[9] * z + M[13]); oz += w * (M[2] * x + M[6] * y + M[10] * z + M[14]);
          }
          Q[v * 3] = ox; Q[v * 3 + 1] = oy; Q[v * 3 + 2] = oz;
        }
        const d = (A, a, b) => Math.hypot(A[a * 3] - A[b * 3], A[a * 3 + 1] - A[b * 3 + 1], A[a * 3 + 2] - A[b * 3 + 2]);
        for (let i = 0; i < p.I.length; i += 3) {
          const tri = [p.I[i], p.I[i + 1], p.I[i + 2]];
          for (let e = 0; e < 3; e++) {
            const a = tri[e], b = tri[(e + 1) % 3], r0 = d(p.P, a, b);
            if (r0 < 0.004) continue; // degenerate pole rings
            if (p.mat.endsWith("_mouth")) continue; // the mouth interior stretches open with the jaw by design
            const r = d(Q, a, b) / r0;
            if (d(Q, a, b) - r0 > 0.03 && r > worst.r) worst = { r, clip: an.name, t: +(t.toFixed(2)), mat: p.mat, y: +p.P[a * 3 + 1].toFixed(3), x: +p.P[a * 3].toFixed(3) };
            if (r > MAX && d(Q, a, b) - r0 > 0.03) { const k = p.mat + ' ' + an.name; hot.set(k, Math.max(hot.get(k) || 0, r)); }
          }
        }
      }
    }
  }
  const fail = hot.size > 0 || cross > 0;
  if (fail) bad++;
  console.log(`${fail ? 'FAIL' : 'ok  '} ${name.padEnd(10)} worst stretch ${worst.r.toFixed(2)} (${worst.mat} ${worst.clip} t=${worst.t} at x=${worst.x} y=${worst.y}); cross-side vertices ${cross}${crossMats.size ? ' ' + JSON.stringify(Object.fromEntries(crossMats)) : ''}`);
  if (VERBOSE || fail) for (const [k, r] of [...hot].sort((a, b) => b[1] - a[1]).slice(0, 8)) console.log(`       ${k}: ${r.toFixed(2)}`);
}
process.exit(bad ? 1 : 0);
