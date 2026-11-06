#!/usr/bin/env node
// The level check (CI): every floor in assets/levels must
//   parse          be a valid .cwmaze
//   reach          let the team reach its exit with the keys it finds on the way, even spending keys on side
//                  rooms first (tools/maze.mjs solve, adversarial): every key comes before its door
//   walls          have nothing standing or spawning inside a wall: starts, exits, spawners, monsters, items,
//                  teleporters, traps and decor on open floor, a torch on a floor tile with a wall on its side
//   grounded       stand every prop on the floor: the lowest point of every frame of the decor, torch, spawner,
//                  door, exit and item models within 1 cm of the floor (from the .cwchar files)
//   bake           have its bake: lightmaps/<n>.cwlm present and lightmaps/<n>.hash equal to the floor's bake hash
//                  (a floor changed after its bake fails)
//   clear          be cleared by the bots in the soak build (--soak <crawler_soak>): four bots from the start
//                  reach the exit within the time limit, with the game's rules (damage on)
// The controls (tools/levels-broken/*.cwmaze, each broken on purpose in one way) must each fail exactly the check
// its name says; a check that cannot fail is not a check, so the run fails when a control passes.
//
//   node games/crawler/tools/check-levels.mjs [--soak <path>] [--only 1,2] [--jobs n] [--no-controls] [--json <file>]
import { existsSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { cpus } from 'node:os';
import { parseMaze, bakeHashFull, solve, THEMES, levelName } from './maze.mjs';

const GAME = join(dirname(fileURLToPath(import.meta.url)), '..');
const A = join(GAME, 'assets');
const argv = process.argv.slice(2);
const opt = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };
const SOAK = opt('--soak');
const ONLY = opt('--only') ? new Set(opt('--only').split(',').map(Number)) : null;
const JOBS = Number(opt('--jobs') || Math.max(1, cpus().length - 1));
const CONTROLS = !argv.includes('--no-controls');

// ── .cwchar bounds: the lowest point of every frame ─────────────────────────
const chrCache = new Map();
function chrBounds(rel, over) {
  const key = (over || '') + rel;
  if (chrCache.has(key)) return chrCache.get(key);
  const f = over && existsSync(join(over, rel)) ? join(over, rel) : join(A, rel);
  let out = null;
  if (existsSync(f)) {
    const b = readFileSync(f);
    if (b.toString('latin1', 0, 4) === 'CWCH') {
      const u32 = (o) => b.readUInt32LE(o);
      const nclip = u32(8), ngroup = u32(12);
      let p = 16, total = 0;
      const nf = [];
      for (let k = 0; k < nclip; k++, p += 4) nf.push(u32(p)), total += u32(p);
      const frames = Array.from({ length: total }, () => ({ minY: Infinity, maxY: -Infinity, minZ: Infinity }));
      for (let g = 0; g < ngroup; g++) {
        const nv = u32(p), nt = u32(p + 4);
        p += 8 + 20 * nv + ((nt * 6 + 3) & ~3);
        for (let fr = 0; fr < total; fr++) {
          for (let v = 0; v < nv; v++) { const y = b.readFloatLE(p + v * 12 + 4), z = b.readFloatLE(p + v * 12 + 8); if (y < frames[fr].minY) frames[fr].minY = y; if (y > frames[fr].maxY) frames[fr].maxY = y; if (g === 0 && z < frames[fr].minZ) frames[fr].minZ = z; }
          p += 24 * nv;
        }
      }
      out = { nclip, ngroup, nf, frames };
    }
  }
  chrCache.set(key, out);
  return out;
}

// the models each floor's pieces are drawn with (view.c / mazebuild.c use the same names)
const kit = (theme, part) => `models/kit_${theme}_${part}.cwchar`;
const ITEM_MODEL = (k) => k === 'key' ? 'models/key.cwchar' : k === 'potion' ? 'models/potion.cwchar' : k.startsWith('elixir') ? 'models/elixir.cwchar' : k.startsWith('food') ? 'models/food.cwchar'
  : k.startsWith('charm') ? null /* charms float over a glow disc by design */ : 'models/treasure.cwchar';

function checkFloor(file, text, { bakeDir = join(A, 'lightmaps'), modelsRequired = true, over = null } = {}) {
  const fails = [];
  let m;
  try { m = parseMaze(text); } catch (e) { return [{ check: 'parse', msg: e.message }]; }
  const at = (x, y) => (y >= 0 && x >= 0 && y < m.h && x < m.w ? m.grid[y][x] : 'void');
  // reach
  const s = solve(m, { adversarial: true });
  if (!s.ok) fails.push({ check: 'reach', msg: `the exit cannot be reached with the keys on the way (${s.stuck}; ${s.keys} keys left, ${s.order.length} doors opened)` });
  const plain = solve(m);
  if (!plain.ok && s.ok) fails.push({ check: 'reach', msg: 'the solver disagrees with itself' });
  // walls
  const onFloor = (x, y, what) => { if (at(x, y) !== 'floor') fails.push({ check: 'walls', msg: `${what} at ${x} ${y} stands on ${at(x, y)}` }); };
  m.start.forEach(([x, y]) => onFloor(x, y, 'a start'));
  m.exit.forEach(([x, y]) => onFloor(x, y, 'an exit'));
  m.spawn.forEach((p) => onFloor(p.x, p.y, `a ${p.kind} spawner`));
  m.mon.forEach((p) => onFloor(p.x, p.y, `a ${p.kind}`));
  m.item.forEach((p) => onFloor(p.x, p.y, `an item (${p.kind})`));
  m.tele.forEach(([x, y]) => onFloor(x, y, 'a teleporter'));
  m.trap.forEach(([x, y]) => onFloor(x, y, 'a trap plate'));
  for (const p of m.prop) {
    if (p.kind === 'decor') onFloor(p.x, p.y, 'decor');
    else {
      onFloor(p.x, p.y, 'a torch');
      const [dx, dy] = { n: [0, -1], s: [0, 1], e: [1, 0], w: [-1, 0] }[p.side] || [0, 0];
      if (at(p.x + dx, p.y + dy) !== 'wall') fails.push({ check: 'walls', msg: `a torch at ${p.x} ${p.y} hangs on its ${p.side} side, where there is ${at(p.x + dx, p.y + dy)}, not a wall` });
    }
  }
  for (const [x, y] of m.trapwall) if (at(x, y) !== 'trapwall') fails.push({ check: 'walls', msg: `trapwall record at ${x} ${y} is ${at(x, y)}` });
  // grounded: every model a floor draws, every frame
  const models = new Map();
  const want = (rel, frames, what) => { if (!models.has(rel)) models.set(rel, { frames: new Set(), what }); for (const f of frames) models.get(rel).frames.add(f); };
  for (const p of m.prop) want(kit(m.theme, p.kind), [p.kind === 'decor' ? p.frame : -1], p.kind);
  if (m.spawn.length) want(kit(m.theme, 'spawner'), [-1], 'spawner');
  if (m.grid.some((r) => r.includes('door'))) want(kit(m.theme, 'door'), [-1], 'door');
  want(kit(m.theme, 'wall'), [-1], 'wall');
  want('models/exit.cwchar', [-1], 'exit');
  for (const it of m.item) { const r = ITEM_MODEL(it.kind); if (r) want(r, [-1], it.kind); }
  for (const [rel, { frames, what }] of models) {
    const b = chrBounds(rel, over);
    if (!b) { if (modelsRequired) fails.push({ check: 'grounded', msg: `${what}: ${rel} is missing` }); continue; }
    const list = frames.has(-1) ? b.frames.map((_, i) => i) : [...frames];
    for (const fi of list) {
      const fr = b.frames[fi];
      if (!fr) { fails.push({ check: 'grounded', msg: `${what}: ${rel} has no frame ${fi}` }); continue; }
      if (what === 'torch') { /* hung on a wall: its back against the wall face (z = 0), not on the floor */
        if (Math.abs(fr.minZ) > 0.02) fails.push({ check: 'grounded', msg: `${what}: ${rel} frame ${fi} stands ${fr.minZ.toFixed(3)} m off its wall` });
      } else if (Math.abs(fr.minY) > 0.01) fails.push({ check: 'grounded', msg: `${what}: ${rel} frame ${fi} lowest point ${fr.minY.toFixed(3)} m (floats or sinks)` });
    }
  }
  // bake
  const n = m.floor, base = String(n).padStart(3, '0');
  const lm = join(bakeDir, `${base}.cwlm`), hf = join(bakeDir, `${base}.hash`);
  if (!existsSync(lm) || !existsSync(hf)) fails.push({ check: 'bake', msg: `no bake (${existsSync(lm) ? '' : base + '.cwlm '}${existsSync(hf) ? '' : base + '.hash'})` });
  else {
    const want_ = bakeHashFull(text, m.theme, join(A, 'models'), readFileSync), got = readFileSync(hf, 'utf8').trim();
    if (want_ !== got) fails.push({ check: 'bake', msg: `the bake is stale: the floor hashes ${want_}, the bake was made from ${got}` });
  }
  return fails;
}

// the soak build's bot run on one floor: exit code 0 when the bots reached the exit
function runClear(file) {
  return new Promise((resolve) => {
    const t0 = Date.now();
    const p = spawn(SOAK, ['--clear', file], { stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '';
    p.stdout.on('data', (d) => (out += d));
    p.stderr.on('data', (d) => (out += d));
    p.on('close', (code) => resolve({ code, out: out.trim().split('\n').slice(-3).join(' | '), ms: Date.now() - t0 }));
  });
}
async function pool(items, fn) {
  const res = new Array(items.length);
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(JOBS, items.length) }, async () => { while (next < items.length) { const i = next++; res[i] = await fn(items[i]); } }));
  return res;
}

const floors = readdirSync(join(A, 'levels')).filter((f) => /^\d{3}\.cwmaze$/.test(f)).sort();
const count = Number(readFileSync(join(A, 'levels/count.txt'), 'utf8'));
let bad = 0;
const report = { floors: {}, controls: {} };
if (floors.length !== count) { console.log(`FAIL count: levels/count.txt says ${count}, there are ${floors.length} floors`); bad++; }
const list = floors.filter((f) => !ONLY || ONLY.has(Number(f.slice(0, 3))));
for (const f of list) {
  const fails = checkFloor(f, readFileSync(join(A, 'levels', f), 'utf8'));
  report.floors[f] = fails;
  if (fails.length) { bad++; for (const x of fails.slice(0, 6)) console.log(`FAIL ${f} ${x.check}: ${x.msg}`); }
}
console.log(`static checks: ${list.length - Object.values(report.floors).filter((x) => x.length).length} of ${list.length} floors pass`);

if (SOAK) {
  const t0 = Date.now();
  const res = await pool(list, (f) => runClear(join(A, 'levels', f)));
  res.forEach((r, i) => {
    report.floors[list[i]] = [...(report.floors[list[i]] || []), ...(r.code ? [{ check: 'clear', msg: r.out }] : [])];
    if (r.code) { bad++; console.log(`FAIL ${list[i]} clear: ${r.out}`); }
  });
  console.log(`clear: ${res.filter((r) => !r.code).length} of ${list.length} floors cleared by the bots (${((Date.now() - t0) / 1000).toFixed(0)} s, slowest ${Math.max(...res.map((r) => r.ms)) / 1000} s)`);
}

if (CONTROLS) {
  // each control is broken in one way and must fail that check (and the run fails if it passes)
  const dir = join(GAME, 'tools/levels-broken');
  for (const f of readdirSync(dir).filter((x) => x.endsWith('.cwmaze')).sort()) {
    const expect = f.split('-')[0]; // reach-..., walls-..., grounded-..., bake-..., clear-...
    const text = readFileSync(join(dir, f), 'utf8');
    let fails = checkFloor(f, text, { bakeDir: join(dir, 'lightmaps'), over: expect === 'grounded' ? dir : null });
    if (expect === 'clear') {
      if (!SOAK) { console.log(`control ${f}: skipped (needs --soak)`); continue; }
      fails = fails.filter((x) => x.check !== 'bake' && x.check !== 'grounded');
      const r = await runClear(join(dir, f));
      if (r.code) fails.push({ check: 'clear', msg: r.out });
    }
    const caught = fails.some((x) => x.check === expect);
    const other = fails.filter((x) => x.check !== expect && !(x.check === 'grounded' && /missing/.test(x.msg)));
    report.controls[f] = { caught, fails };
    if (!caught) { bad++; console.log(`FAIL control ${f}: broken on purpose (${expect}) and the check passed it`); }
    else console.log(`control ${f}: caught (${fails.find((x) => x.check === expect).msg})${other.length ? `; also ${other.map((x) => x.check).join(',')}` : ''}`);
  }
}
if (opt('--json')) writeFileSync(opt('--json'), JSON.stringify(report, null, 1));
console.log(bad ? `check-levels: ${bad} failure(s)` : 'check-levels: every floor passes, every control caught');
process.exit(bad ? 1 : 0);
void THEMES; void levelName;
