// The craft models against the real ones, read from the model files themselves (no GPU): the shape a player sees.
//   sit-down: forward of the seat the keel sweeps up to the stem in a convex curve (rising, its slope growing), the sheer
//     line rises all the way, the foredeck falls from the bars to the stem without a dip, and the stem rakes forward 20 to
//     30 degrees into a beak. Control: the first sit-down hull, whose nose was scooped (tests/jetski-sit-scoop.glb).
//   stand-up: the hood stands at least 0.45 m over the tray floor just ahead of the rider's shins and the hull side is at
//     least 0.3 m deep at the stern. Control: the first, blade-like stand-up (tests/jetski-standup-blade.glb).
//   node tests/jetski-craft-models.mjs
import { readFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), M = join(ROOT, 'games/jetski/assets/models');
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
/** the bow's profile from the model itself: vertical rays through its surfaces (the hull's own, not fittings) */
function bowOf(file, z0 = 0.4) {
  const b = readFileSync(file), jl = b.readUInt32LE(12), j = JSON.parse(b.slice(20, 20 + jl)), bin = b.slice(20 + jl + 8);
  const n = j.nodes.find((x) => x.name === 'Hull'), ty = (n.translation || [0, 0, 0])[1], tris = [];
  const arr = (acc, T) => { const a = j.accessors[acc], v = j.bufferViews[a.bufferView], c = { SCALAR: 1, VEC3: 3 }[a.type]; return new T(bin.buffer, bin.byteOffset + v.byteOffset + (a.byteOffset || 0), a.count * c); };
  for (const p of j.meshes[n.mesh].primitives) {
    if (/metal|lamp|nozzle|strap|seat|rubber|seam|trim|grip|lanyard|pad|plate|decal/.test(j.materials[p.material].name)) continue;
    const pos = arr(p.attributes.POSITION, Float32Array), ia = j.accessors[p.indices], idx = arr(p.indices, ia.componentType === 5125 ? Uint32Array : Uint16Array);
    for (let i = 0; i < idx.length; i += 3) tris.push([0, 1, 2].map((k) => [pos[idx[i + k] * 3], pos[idx[i + k] * 3 + 1] + ty, pos[idx[i + k] * 3 + 2]]));
  }
  const hits = (x, z) => { const ys = [];
    for (const [a, bb, c] of tris) { // barycentric in the xz plane
      const d = (bb[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (bb[2] - a[2]); if (Math.abs(d) < 1e-12) continue;
      const u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / d, v = ((bb[0] - a[0]) * (z - a[2]) - (x - a[0]) * (bb[2] - a[2])) / d;
      if (u >= 0 && v >= 0 && u + v <= 1) ys.push(a[1] + u * (bb[1] - a[1]) + v * (c[1] - a[1])); }
    return ys; };
  const width = (z) => { let w = 0; for (let x = 0; x < 0.8; x += 0.005) if (hits(x, z).length) w = x; return w; };
  const keel = [], deck = [], sheer = [];
  for (let z = z0; z < 2.0; z += 0.05) {
    const c = hits(0, z); if (c.length < 2 || Math.max(...c) - Math.min(...c) < 0.05) break; /* (past the stem's foot only the beak is left) */
    keel.push([z, Math.min(...c)]), deck.push([z, Math.max(...c)]);
    const w = width(z); if (w > 0.06) { const s = hits(w * 0.9, z); if (s.length) sheer.push([z, Math.max(...s)]); }
  }
  const mono = (p, sign, tol) => p.every((q, i) => i === 0 || sign * (q[1] - p[i - 1][1]) >= -tol);
  const d2 = (p) => p.slice(1, -1).map((q, i) => (p[i + 2][1] - q[1]) - (q[1] - p[i][1]));
  const fore = deck.filter((p) => p[0] >= 0.8);
  /* the stem: from the foot of the keel to the tip of the beak, degrees forward of upright */
  let tip = [0, 0, -9]; for (const t of tris) for (const v of t) if (v[2] > tip[2]) tip = v;
  const foot = keel[keel.length - 1], rake = Math.atan2(tip[2] - foot[0], tip[1] - foot[1]) * 180 / Math.PI;
  return { keel, deck, sheer, rake, deckOk: fore.length > 4 && mono(fore, -1, 0.002) && d2(fore).every((x) => x <= 0.004), keelOk: keel.length > 6 && mono(keel, 1, 0.002) && d2(keel).every((x) => x >= -0.004), sheerOk: sheer.length > 6 && mono(sheer, 1, 0.004) };
}
function hullOf(file) {
  const b = readFileSync(file), jl = b.readUInt32LE(12), j = JSON.parse(b.slice(20, 20 + jl)), bin = b.slice(20 + jl + 8);
  const nodeOf = (name) => j.nodes.find((n) => n.name === name);
  const verts = (name) => { const n = nodeOf(name), t = n.translation || [0, 0, 0], out = [];
    for (const p of j.meshes[n.mesh].primitives) { const a = j.accessors[p.attributes.POSITION], v = j.bufferViews[a.bufferView], f = new Float32Array(bin.buffer, bin.byteOffset + v.byteOffset + (a.byteOffset || 0), a.count * 3);
      for (let i = 0; i < a.count; i++) out.push([f[i * 3] + t[0], f[i * 3 + 1] + t[1], f[i * 3 + 2] + t[2], j.materials[p.material].name]); }
    return out; };
  const hull = verts('Hull'), pole = verts('Pole');
  const floor = Math.min(...hull.filter((v) => v[3] === 'mat').map((v) => v[1]));
  const hood = Math.max(...hull.filter((v) => v[2] > 0 && v[2] < 0.6 && Math.abs(v[0]) < 0.15).map((v) => v[1]));
  const stern = hull.filter((v) => v[2] > -0.85 && v[2] < -0.7), wide = Math.max(...stern.map((v) => Math.abs(v[0])));
  const side = stern.filter((v) => Math.abs(v[0]) > wide * 0.88 && v[3] !== 'rubber').map((v) => v[1]);
  const poleXs = pole.filter((v) => v[3] === 'deck').map((v) => v[0]);
  return { hood: hood - floor, side: Math.max(...side) - Math.min(...side), pole: Math.max(...poleXs) - Math.min(...poleXs) };
}
const bowOk = (r) => r.keelOk && r.sheerOk && r.deckOk && r.rake >= 20 && r.rake <= 30;
const bowRow = (r) => `keel ${r.keelOk ? 'rises convex' : 'not convex/rising'} (${r.keel[0][1].toFixed(2)} to ${r.keel.at(-1)[1].toFixed(2)} m), sheer ${r.sheerOk ? 'rises' : 'dips'}, foredeck ${r.deckOk ? 'falls without a dip' : 'dips'}, stem raked ${r.rake.toFixed(1)} deg`;
const sit = bowOf(join(M, 'craft.glb')), scoop = bowOf(join(ROOT, 'tests/jetski-sit-scoop.glb'));
check('sit-down: a beak, not a scoop', bowOk(sit), bowRow(sit));
check('control: the first sit-down bow fails it', !bowOk(scoop), bowRow(scoop));
const hullOk = (h) => h.hood >= 0.45 && h.side >= 0.3;
const st = hullOf(join(M, 'standup.glb')), blade = hullOf(join(ROOT, 'tests/jetski-standup-blade.glb'));
check('stand-up: a tall hood and a deep hull', hullOk(st), `hood ${st.hood.toFixed(2)} m over the tray floor, side ${st.side.toFixed(2)} m deep, pole ${(st.pole * 100).toFixed(1)} cm thick`);
check('control: the first, blade-like stand-up fails it', !hullOk(blade), `hood ${blade.hood.toFixed(2)} m, side ${blade.side.toFixed(2)} m`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
