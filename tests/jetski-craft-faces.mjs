// Every face of the craft a camera outside it can see is drawn: read from the model files themselves (no GPU).
// Rays from cameras on a sphere round each craft model to points on it; the first surface a ray meets must face the
// camera (counter-clockwise from there, glTF's front face) unless its material is double-sided, or the renderer culls it
// and the craft shows its far side through the hole: hollow. A second count checks each vertex normal agrees with the
// winding of its triangles (a face drawn but lit from behind reads black).
// Control, run after: the models as they were before the fix (git fdd3464), whose side panels faced into the hull and
// showed the far side through them, must fail (--control=<rev> reads another commit's models for it).
//   node tests/jetski-craft-faces.mjs [--control=<rev>] [--verbose]
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), M = join(ROOT, 'games/jetski/assets/models');
const CTL = process.argv.find((a) => a.startsWith('--control=')), CONTROL_REV = CTL ? CTL.split('=')[1] : 'fdd3464', VERBOSE = process.argv.includes('--verbose');
const MODELS = ['craft.glb', 'standup.glb', 'standup_lo.glb'];
const RAYS = 6000, MAX_BACK = 0.01; /* at most 1% of the rays that meet a craft may meet a culled face first: rays that slip through a seam a few mm wide (the hollow craft: 18 to 32%) */

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const mmul = (a, b) => { const o = new Array(16).fill(0); for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) for (let k = 0; k < 4; k++) o[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k]; return o; };
const xf = (m, p, w = 1) => [0, 1, 2].map((r) => m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * w);
function local(n) {
  if (n.matrix) return n.matrix;
  const [x, y, z, w] = n.rotation || [0, 0, 0, 1], [sx, sy, sz] = n.scale || [1, 1, 1], [tx, ty, tz] = n.translation || [0, 0, 0];
  return [(1 - 2 * (y * y + z * z)) * sx, 2 * (x * y + z * w) * sx, 2 * (x * z - y * w) * sx, 0, 2 * (x * y - z * w) * sy, (1 - 2 * (x * x + z * z)) * sy, 2 * (y * z + x * w) * sy, 0, 2 * (x * z + y * w) * sz, 2 * (y * z - x * w) * sz, (1 - 2 * (x * x + y * y)) * sz, 0, tx, ty, tz, 1];
}
/** every triangle of a glb in world space: {a, b, c, n (from the winding), two (double-sided), mat, nrm (vertex normals)} */
function load(b) {
  const jl = b.readUInt32LE(12), j = JSON.parse(b.slice(20, 20 + jl)), bin = b.slice(20 + jl + 8), tris = [];
  const arr = (acc) => { const a = j.accessors[acc], v = j.bufferViews[a.bufferView], c = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 }[a.type];
    const T = { 5126: Float32Array, 5125: Uint32Array, 5123: Uint16Array, 5121: Uint8Array }[a.componentType];
    return new T(bin.buffer, bin.byteOffset + v.byteOffset + (a.byteOffset || 0), a.count * c); };
  const walk = (ni, parent) => {
    const n = j.nodes[ni], m = mmul(parent, local(n));
    if (n.mesh != null && n.skin == null) {
      const det = dot(cross(m.slice(0, 3), m.slice(4, 7)), m.slice(8, 11)); /* a mirroring transform swaps which side is front */
      for (const p of j.meshes[n.mesh].primitives) {
        const mat = j.materials[p.material] || {}, pos = arr(p.attributes.POSITION), nr = p.attributes.NORMAL != null ? arr(p.attributes.NORMAL) : null, idx = arr(p.indices);
        const V = (i) => xf(m, [pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]]), N = (i) => nr && xf(m, [nr[i * 3], nr[i * 3 + 1], nr[i * 3 + 2]], 0);
        for (let i = 0; i < idx.length; i += 3) {
          const a = V(idx[i]), bb = V(idx[i + 1]), c = V(idx[i + 2]);
          let n0 = cross(sub(bb, a), sub(c, a)); if (det < 0) n0 = n0.map((x) => -x);
          tris.push({ a, b: bb, c, n: n0, two: !!mat.doubleSided, mat: mat.name || '?', nrm: [N(idx[i]), N(idx[i + 1]), N(idx[i + 2])] });
        }
      }
    }
    for (const c of n.children || []) walk(c, m);
  };
  for (const r of j.scenes[j.scene || 0].nodes) walk(r, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
  return tris;
}
/** Moller-Trumbore, both sides: the distance along the ray, or Infinity */
function hit(o, d, t) {
  const e1 = sub(t.b, t.a), e2 = sub(t.c, t.a), p = cross(d, e2), det = dot(e1, p);
  if (Math.abs(det) < 1e-14) return Infinity;
  const inv = 1 / det, s = sub(o, t.a), u = dot(s, p) * inv; if (u < 0 || u > 1) return Infinity;
  const q = cross(s, e1), v = dot(d, q) * inv; if (v < 0 || u + v > 1) return Infinity;
  const k = dot(e2, q) * inv; return k > 1e-6 ? k : Infinity;
}
let seed = 12345; const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296);
/** the check over every craft model, from the working tree or (rev) a commit; returns how many fail */
function check(rev) {
let fail = 0;
for (const file of MODELS) {
  const tris = load(rev ? execFileSync('git', ['-C', ROOT, 'show', `${rev}:games/jetski/assets/models/${file}`], { maxBuffer: 1 << 28 }) : readFileSync(join(M, file)));
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (const t of tris) for (const p of [t.a, t.b, t.c]) for (let i = 0; i < 3; i++) { lo[i] = Math.min(lo[i], p[i]); hi[i] = Math.max(hi[i], p[i]); }
  const ctr = lo.map((v, i) => (v + hi[i]) / 2), R = Math.hypot(...sub(hi, lo)) * 1.5;
  for (const t of tris) { /* a cheap bound per triangle for the ray loop */
    t.mid = [0, 1, 2].map((i) => (t.a[i] + t.b[i] + t.c[i]) / 3); t.r = Math.max(...[t.a, t.b, t.c].map((p) => Math.hypot(...sub(p, t.mid))));
  }
  seed = 12345; let met = 0, back = 0; const byMat = {}, byView = { above: [0, 0], beside: [0, 0], below: [0, 0] };
  for (let r = 0; r < RAYS; r++) {
    const z = rnd() * 2 - 1, ph = rnd() * Math.PI * 2, s = Math.sqrt(1 - z * z), dirC = [s * Math.cos(ph), z, s * Math.sin(ph)];
    const o = ctr.map((v, i) => v + dirC[i] * R), aim = ctr.map((v, i) => v + (rnd() - 0.5) * (hi[i] - lo[i]));
    let d = sub(aim, o); const L = Math.hypot(...d); d = d.map((x) => x / L);
    /* every surface at the nearest depth (a panel drawn both ways is two triangles in one place: either facing the camera draws) */
    let best = Infinity, near = [];
    for (const t of tris) { const w = sub(t.mid, o), along = dot(w, d), off2 = dot(w, w) - along * along; if (off2 > t.r * t.r || along > best + 1e-4 + t.r) continue; const k = hit(o, d, t); if (k === Infinity) continue;
      if (k < best - 1e-4) best = k, near = [t]; else if (k <= best + 1e-4) { near.push(t); best = Math.min(best, k); } }
    if (!near.length) continue;
    const bt = near[0];
    met++; const view = dirC[1] > 0.35 ? 'above' : dirC[1] < -0.35 ? 'below' : 'beside'; byView[view][0]++;
    if (VERBOSE && !near.some((t) => t.two || dot(t.n, d) <= 0)) { /* what the ray meets, nearest first: F faces the camera, B is culled */
      const along = tris.map((t) => [hit(o, d, t), t]).filter(([k]) => k < Infinity).sort((p, q) => p[0] - q[0]).slice(0, 4);
      console.log('  culled', file, along.map(([k, t]) => `${t.mat}${t.two ? '2' : dot(t.n, d) > 0 ? 'B' : 'F'}@${k.toFixed(3)}[${t.mid.map((v) => v.toFixed(2))}]`).join(' ')); }
    if (!near.some((t) => t.two || dot(t.n, d) <= 0)) { back++; byView[view][1]++; byMat[bt.mat] = (byMat[bt.mat] || 0) + 1; }
  }
  /* vertex normals against the winding: the share of (non double-sided) triangles lit from the wrong side */
  let wrongLit = 0, lit = 0;
  for (const t of tris) { if (t.two || !t.nrm[0]) continue; lit++; const s = t.nrm.reduce((acc, n) => acc + Math.sign(dot(n, t.n)), 0); if (s < 0) wrongLit++; }
  const frac = back / Math.max(1, met), ok = frac <= MAX_BACK && wrongLit / Math.max(1, lit) <= 0.002;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${rev ? 'control ' + rev + ' ' : ''}${file}: ${back} of ${met} rays meet a culled face first (${(frac * 100).toFixed(2)}%; above ${byView.above[1]}/${byView.above[0]}, beside ${byView.beside[1]}/${byView.beside[0]}, below ${byView.below[1]}/${byView.below[0]})${back ? ' by material ' + JSON.stringify(byMat) : ''}; ${wrongLit} of ${lit} triangles lit from behind`);
  if (!ok) fail++;
}
return fail;
}
const fail = check(null), ctl = check(CONTROL_REV);
console.log(ctl ? `control fails as it must (${ctl} models of ${CONTROL_REV})` : `FAIL control: the check cannot see the hollow craft of ${CONTROL_REV}`);
console.log(!fail && ctl ? 'all ok' : 'FAILED');
process.exit(!fail && ctl ? 0 : 1);
