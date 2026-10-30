// The descent's scenery checks (LEVELS.md, section 6), over each assets/levels/<id>.cwlevel and the
// .cwchar models it places:
//   - grounded: every placement's base (its model's lowest point at rest, plus its y) is on the floor or on
//     the top of the prop under it, within 3 cm (no floating, no sunken props); a stacked prop's centre
//     is inside the footprint of the prop it stands on (no overhang);
//   - the hatch's clear circle: no solid within 4.5 m of the hatch (the team spawns there);
//   - solids that do not overlap (more than 0.3 m into each other; 0.8 m where two runs of a wall meet at a corner) and
//     stay inside the arena; no solid on a hazard strip;
//   - every file a level names exists.
// Exits 1 on any failure. --control lifts the first placement of each level 0.5 m: the check must fail.
//   node tools/check-levels.mjs [level ...] [--control]
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCwchar } from './cwchar-info.mjs';

const GAME = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const A = (p) => resolve(GAME, 'assets', p);
const args = process.argv.slice(2);
const control = args.includes('--control');
const want = args.filter((a) => !a.startsWith('--'));
const ids = want.length ? want : readdirSync(A('levels')).filter((f) => f.endsWith('.cwlevel')).map((f) => f.replace('.cwlevel', ''));
const HALF = 38, TOL = 0.03;

function parse(id) {
  const lv = { id, props: {}, at: [], haz: [], files: [] };
  for (const raw of readFileSync(A(`levels/${id}.cwlevel`), 'utf8').split('\n')) {
    const line = raw.replace(/#.*/, '').trim();
    if (!line) continue;
    const [k, ...t] = line.split(/\s+/);
    if (k === 'prop') { lv.props[t[0]] = { file: t[1], solid: +t[6] }; lv.files.push(t[1]); }
    else if (k === 'at') lv.at.push({ name: t[0], x: +t[1], y: +t[2], z: +t[3], yaw: +t[4], hx: +(t[5] ?? 0), hz: +(t[6] ?? 0) });
    else if (k === 'hazard') lv.haz.push({ kind: t[0], x: +t[1], z: +t[2], hx: +t[3], hz: +t[4] });
    else if (['monster', 'scene', 'door', 'floor'].includes(k)) lv.files.push(t[0]);
    else if (k === 'boss') lv.files.push(t[0]);
    else if (k === 'obstacle') lv.files.push(t[1]);
    else if (k === 'lightmap') lv.lightmap = t[0];
  }
  return lv;
}

// a placement's footprint as an axis-aligned box (its model's rotated bounds)
function footprint(b, a) {
  const c = Math.cos(a.yaw), s = Math.sin(a.yaw);
  let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
  for (const [lx, lz] of [[b.lo[0], b.lo[2]], [b.hi[0], b.lo[2]], [b.lo[0], b.hi[2]], [b.hi[0], b.hi[2]]]) {
    const x = a.x + lx * c + lz * s, z = a.z - lx * s + lz * c;
    x0 = Math.min(x0, x); x1 = Math.max(x1, x); z0 = Math.min(z0, z); z1 = Math.max(z1, z);
  }
  return { x0, x1, z0, z1 };
}

let failed = 0;
const fail = (id, msg) => { failed++; console.log(`FAIL ${id}: ${msg}`); };
const models = {};
const model = (file) => (models[file] ??= readCwchar(A(file)));
for (const id of ids) {
  const lv = parse(id);
  for (const file of lv.files) if (!existsSync(A(file))) fail(id, `missing ${file}`);
  if (control && lv.at.length) lv.at[0].y += 0.5;
  let seated = 0, stacked = 0;
  const boxes = lv.at.map((a) => ({ a, p: lv.props[a.name], b: model(lv.props[a.name].file) }));
  for (const q of boxes) {
    q.base = q.a.y + q.b.frameLo[0];
    q.top = q.a.y + q.b.hi[1];
    q.fp = footprint(q.b, q.a);
  }
  for (const q of boxes) {
    const where = `${q.a.name} at (${q.a.x}, ${q.a.y}, ${q.a.z})`;
    if (Math.abs(q.base) <= TOL) { seated++; continue; }
    // standing on another prop: one whose top is at this base and whose footprint holds this centre
    const under = boxes.find((o) => o !== q && Math.abs(o.top - q.base) <= TOL && q.a.x >= o.fp.x0 && q.a.x <= o.fp.x1 && q.a.z >= o.fp.z0 && q.a.z <= o.fp.z1);
    if (under) { stacked++; if (process.env.CHECK_DEBUG) console.log(`  ${where} stands on ${under.a.name} at (${under.a.x}, ${under.a.z})`); continue; }
    fail(id, `${where}: its base is at ${q.base.toFixed(3)} m, ${q.base > 0 ? 'floating' : 'sunken'} (nothing under it within ${TOL * 100} cm)`);
  }
  const solids = boxes.filter((q) => q.p.solid);
  for (const q of solids) {
    const where = `${q.a.name} at (${q.a.x}, ${q.a.z})`;
    if (Math.hypot(q.a.x, q.a.z) < 4.5) fail(id, `${where}: inside the hatch's clear circle`);
    if (Math.abs(q.a.x) + q.a.hx > HALF + 0.01 || Math.abs(q.a.z) + q.a.hz > HALF + 0.01) fail(id, `${where}: outside the arena`);
    for (const h of lv.haz)
      if (Math.abs(q.a.x - h.x) < h.hx + q.a.hx - 0.05 && Math.abs(q.a.z - h.z) < h.hz + q.a.hz - 0.05) fail(id, `${where}: on the ${h.kind} at (${h.x}, ${h.z})`);
  }
  for (let i = 0; i < solids.length; i++) for (let j = i + 1; j < solids.length; j++) {
    const p = solids[i], q = solids[j];
    if (p.a.y !== q.a.y) continue; // stacked: checked above
    const ox = Math.min(p.a.x + p.a.hx, q.a.x + q.a.hx) - Math.max(p.a.x - p.a.hx, q.a.x - q.a.hx);
    const oz = Math.min(p.a.z + p.a.hz, q.a.z + q.a.hz) - Math.max(p.a.z - p.a.hz, q.a.z - q.a.hz);
    const corner = p.a.name === q.a.name && p.a.yaw !== q.a.yaw; // two runs of the same wall meeting at a corner
    if (ox > (corner ? 0.8 : 0.3) && oz > (corner ? 0.8 : 0.3)) fail(id, `${p.a.name} at (${p.a.x}, ${p.a.z}) and ${q.a.name} at (${q.a.x}, ${q.a.z}) overlap ${ox.toFixed(2)} x ${oz.toFixed(2)} m`);
  }
  console.log(`${id}: ${lv.at.length} placements, ${seated} on the floor, ${stacked} stacked, ${solids.length} solid${lv.lightmap && existsSync(A(lv.lightmap)) ? '' : ', no bake yet'}`);
}
if (failed) { console.log(`${failed} failure(s)${control ? ' (the control: as it must)' : ''}`); process.exit(1); }
console.log(control ? 'CONTROL DID NOT FAIL: the check cannot see a lifted prop' : 'all levels pass');
process.exit(control ? 2 : 0);
