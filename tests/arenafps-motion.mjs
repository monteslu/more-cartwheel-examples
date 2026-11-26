// arenafps motion check: how the soldiers move, measured from their bones, not judged from stills. One run of the cart
// on romdev with a pad script for player 1 and the bots in a split screen deathmatch on the Foundry (a second seat
// joins and stands still, so player 1's own body is drawn and animated: in one view it is not). The cart's motion
// recorder (soldier.c "motion rec") keeps player 1's and a bot's bones in the world each tick, with the state that made
// them and the ground under each ankle. The pad script: stand, run forward, stop, strafe left, strafe right, run back,
// forward and back diagonals, a slow walk, look up and fire, look down and fire, strafe while firing, a jump, a grenade,
// a bash and a weapon change. The measures:
//   skate   while it runs steadily on the floor (over 2 m/s, its speed within 5% for eight ticks either side), each foot's speed over the ground in its
//           stance (the ball of the foot within 1.5 cm of the lowest it gets in that second), against the body's speed:
//           a planted foot stays put (0), a foot that slides with the body moves at its speed (1). By the way it runs:
//           forward, sideways, backwards, diagonally
//   strafe  while it runs sideways at a steady speed (its velocity 60 to 120 degrees off its facing), the share of ticks the strongest
//           locomotion clip is a sideways one, on the side it goes (walk_l, run_l, walk_bl90, run_bl90 to the left;
//           _r, _br90 to the right); and backwards (over 120 degrees off) a backwards one
//   aim     the rifle's bore (stock to muzzle) against the shot's direction (the yaw and pitch the shot leaves along),
//           standing and running, looking level, up and down, away from the throw, the bash and the weapon change
//   ground  the bot's feet where the ground under them is not flat (the two ankles' grounds more than 4 cm apart: stairs,
//           ramps, the terraces): each planted ankle's height over the ground under it against its height at rest
// Bars (PASS needs all): skate <= 0.25 (median over stances) in every direction it ran, 10 or more stance ticks in
// three directions at least; strafe >= 0.8 of the sideways
// ticks and >= 0.8 of the backwards ones; aim median <= 4 degrees and 90th percentile <= 10, up and down included;
// ground median error <= 3 cm over at least 30 planted ticks on uneven ground.
// CONTROL, which must fail every check: the same run on the build before (the hand-keyed 15 bone rig and its three
// clips) with only the recorder added (tests/arenafps-anim-control.patch on the parent commit): --control <run dir>.
//
//   node tests/arenafps-motion.mjs run [--cart <wasc>] [--gpu gl|webgpu] [--out <dir>]   the run (trace.json)
//   node tests/arenafps-motion.mjs measure <run dir> [--control <run dir>] [--json]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const TPS = 60;

// the pad script for player 1: [frames, port0 state]; port 1 stays still
export function script() {
  const S = [];
  const add = (n, p = {}) => S.push([n, p]);
  // (each run is short and the next goes back the way it came, so the player stays near the spawn, off the walls)
  add(60);                                         // stand
  add(100, { ly: -1 }); add(40); add(100, { ly: 1 }); add(40);     // run forward, stop, run back, stop
  add(100, { ly: -1 }); add(40); add(100, { ly: 1 }); add(40);     // and again
  add(100, { lx: -1 }); add(100, { lx: 1 });       // strafe left, strafe right
  add(100, { lx: 1 }); add(100, { lx: -1 });       // and again the other way first
  add(90, { lx: -0.7, ly: -0.7 }); add(90, { lx: 0.7, ly: 0.7 });  // forward left, back right
  add(90, { lx: 0.7, ly: -0.7 }); add(90, { lx: -0.7, ly: 0.7 });  // forward right, back left
  add(70, { ly: -0.35 }); add(70, { ly: 0.35 });   // a slow walk, there and back
  add(40);
  add(40, { ry: -1 }); add(50, { fire: true });     // look up, fire
  add(80, { ry: 1 }); add(50, { fire: true });      // look down, fire
  add(40, { ry: -1 });                             // level again
  add(70, { lx: -1, fire: true }); add(70, { lx: 1, fire: true });   // strafe both ways, firing
  add(20, { ly: -1 }); add(4, { ly: -1, south: true }); add(40, { ly: -1 }); add(20); // a running jump
  add(20, { ly: 1 }); add(30);
  add(4, { l: true }); add(80);                    // a grenade
  add(4, { east: true }); add(60);                 // a bash
  add(4, { north: true }); add(90);                // a weapon change
  add(60);
  return S;
}
// the actions' windows (frames from the script's start) left out of the aim: the throw, the bash, the weapon change
function actionWindows() {
  const w = [];
  let f = 0;
  for (const [n, p] of script()) {
    if (p.l || p.east || p.north) w.push([f, f + 70]);
    f += n;
  }
  return w;
}
export const FRAMES = script().reduce((s, [n]) => s + n, 0);
const BOT_FRAMES = 2000;
const port = (p) => {
  const o = {};
  for (const b of ['south', 'east', 'north', 'west', 'l', 'r', 'start']) if (p[b]) o[b] = true;
  const axes = {};
  if (p.lx || p.ly) axes.lx = p.lx || 0, axes.ly = p.ly || 0;
  if (p.rx || p.ry) axes.rx = p.rx || 0, axes.ry = p.ry || 0;
  if (p.fire) axes.rt = 1, o.r = true;
  if (Object.keys(axes).length) o.axes = axes;
  return o;
};

async function run() {
  const SERVER = process.env.ROMDEV_URL || ROMDEV_URL;
  const gpu = arg('--gpu', 'webgpu');
  const cart = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/arenafps.wasc')));
  const out = resolve(arg('--out', join(ROOT, 'out/arenafps-motion', gpu)));
  mkdirSync(out, { recursive: true });
  const HDR = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.ARENA_SESSION || 'arenafps-motion', 'x-romdev-agent': 'arenafps-motion' };
  const bye = async () => { try { await fetch(`${SERVER}/tool/host`, { method: 'POST', headers: HDR, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {} };
  const tool = async (name, a) => {
    const r = await fetch(`${SERVER}/tool/${name}`, { method: 'POST', headers: HDR, body: JSON.stringify(a), signal: AbortSignal.timeout(1800000) });
    const t = await r.text();
    let j; try { j = JSON.parse(t); } catch { j = t; }
    if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(); process.exit(3); }
    return j;
  };
  const cmd = async (text) => (await tool('wasm', { op: 'command', text })).reply;
  try {
    const l = await tool('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(gpu === 'gl' ? { wgpu: false } : {}) });
    if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); process.exit(3); }
    await tool('frame', { op: 'step', frames: 120 });
    const take = async () => {
      const info = await cmd('motion info');
      const rows = [];
      for (let k = 0; k < info.rows; k += 60) rows.push(...(await cmd(`motion get ${k} 60`)));
      return { info, rows };
    };
    // the pad script: the Courtyard, two seats and no bots (nobody downs player 1 halfway)
    await cmd('match 1 0 0 2');
    await tool('frame', { op: 'step', frames: 90 });
    await cmd(`motion rec ${FRAMES + 4} 0`);
    const steps = script().map(([frames, p]) => ({ input: { ports: [port(p), {}] }, frames }));
    await tool('input', { op: 'sequence', steps });
    const a = await take();
    // the bots: the Terraces (stairs, terraces, ramps), six bots playing; four of them recorded
    await cmd('match 2 0 6 2');
    await tool('frame', { op: 'step', frames: 120 });
    await cmd(`motion rec ${4 * BOT_FRAMES + 16} 2 3 4 5`);
    await tool('frame', { op: 'step', frames: BOT_FRAMES });
    const b = await take();
    writeFileSync(join(out, 'trace.json'), JSON.stringify({ cart, gpu, bones: a.info.bones, ankle: a.info.ankle, travel: a.info.travel, rows: a.rows, bots: b.rows }));
    console.log(`${out}: ${a.rows.length} rows of player 1, ${b.rows.length} of the bots`);
  } finally { await bye(); }
}

// ── the measures ───────────────────────────────────────────────────────────────────────────────────────────────
const med = (a) => { if (!a.length) return NaN; const s = [...a].sort((x, y) => x - y); return s[Math.floor(s.length / 2)]; };
const pct = (a, p) => { if (!a.length) return NaN; const s = [...a].sort((x, y) => x - y); return s[Math.min(s.length - 1, Math.floor(s.length * p))]; };
const wrap = (a) => { while (a > Math.PI) a -= 2 * Math.PI; while (a < -Math.PI) a += 2 * Math.PI; return a; };

function parse(file) {
  const t = JSON.parse(readFileSync(file, 'utf8'));
  const B = t.bones, nb = B.length;
  const conv = (r) => {
    const o = { tick: r[0], slot: r[1], alive: r[2], floor: r[3], lod: r[4], x: r[5], y: r[6], z: r[7], yaw: r[8], pitch: r[9], vx: r[10], vz: r[11], back: r[12], clip: r[13], w: r[14], ground: [r[15], r[16]], b: {} };
    for (let k = 0; k < nb; k++) o.b[B[k]] = [r[17 + k * 3], r[18 + k * 3], r[19 + k * 3]];
    return o;
  };
  return { rows: t.rows.map(conv), bots: (t.bots || []).map(conv), ankle: t.ankle };
}

export function measure(dir) {
  const { rows, bots, ankle } = parse(join(dir, 'trace.json'));
  const me = rows.filter((r) => r.slot === 0);
  const t0 = me.length ? me[0].tick : 0;
  const acts = actionWindows();
  const inAct = (r) => acts.some(([a, b]) => r.tick - t0 >= a && r.tick - t0 < b);
  // the way it runs, against its facing (the body faces -z at yaw 0; + is to the left)
  const travel = (r) => { const fx = -Math.sin(r.yaw), fz = -Math.cos(r.yaw); const lx = -Math.cos(r.yaw), lz = Math.sin(r.yaw); return Math.atan2(r.vx * lx + r.vz * lz, r.vx * fx + r.vz * fz); };
  const speed = (r) => Math.hypot(r.vx, r.vz);
  const cls = (a) => { const d = Math.abs(a); return d < 0.35 ? 'forward' : d < 1.05 ? 'diagonal' : d <= 2.1 ? 'sideways' : d < 2.8 ? 'back diagonal' : 'backwards'; };
  // skate: per stance of each foot, its ground speed against the body's
  const skate = {};
  // at a steady speed: within 5% over eight ticks either side (a start or a stop is not a stride)
  const isSteady = (i) => {
    const r = me[i];
    if (!(r.alive && r.floor && speed(r) > 2)) return false;
    for (let q = Math.max(0, i - 8); q < Math.min(me.length, i + 9); q++) if (Math.abs(speed(me[q]) - speed(r)) > 0.05 * speed(r) || !me[q].floor) return false;
    return true;
  };
  const steady = me.filter((r, i) => isSteady(i));
  for (let i = 1; i < me.length - 1; i++) {
    const r = me[i], p = me[i - 1];
    if (!(isSteady(i) && p.alive && p.floor && r.tick === p.tick + 1)) continue;
    for (const f of ['ball_l', 'ball_r']) {
      // in contact: the ball of the foot within 1.5 cm of the lowest it gets within half a second either side (at a
      // sprint the ankle is off the ground for most of the stance; the ball is what stays put)
      let lo = Infinity;
      for (let k = Math.max(0, i - 30); k < Math.min(me.length, i + 30); k++) lo = Math.min(lo, me[k].b[f][1]);
      if (r.b[f][1] > lo + 0.015 || p.b[f][1] > lo + 0.015) continue;
      const v = Math.hypot(r.b[f][0] - p.b[f][0], r.b[f][2] - p.b[f][2]) * TPS;
      (skate[cls(travel(r))] ||= []).push(v / speed(r));
    }
  }
  const skateMed = Object.fromEntries(Object.entries(skate).map(([k, v]) => [k, { median: +med(v).toFixed(3), n: v.length }]));
  // strafe and backwards: the clip that leads
  const side = steady.filter((r) => Math.abs(travel(r)) >= 1.05 && Math.abs(travel(r)) <= 2.1);
  const sideOk = side.filter((r) => (travel(r) > 0 ? /^(walk|run)_(l|bl90)$/ : /^(walk|run)_(r|br90)$/).test(r.clip)).length;
  const backs = steady.filter((r) => Math.abs(travel(r)) > 2.4);
  const backOk = backs.filter((r) => /^(walk|run)_b/.test(r.clip)).length;
  // aim: the bore against the shot's direction
  const aimAll = [], aimUp = [], aimDown = [];
  for (const r of me) {
    if (!r.alive || inAct(r) || !r.b.rifle || !r.b.muzzle) continue;
    const d = [r.b.muzzle[0] - r.b.rifle[0], r.b.muzzle[1] - r.b.rifle[1], r.b.muzzle[2] - r.b.rifle[2]];
    const l = Math.hypot(...d);
    if (l < 1e-4) continue;
    const s = [-Math.sin(r.yaw) * Math.cos(r.pitch), Math.sin(r.pitch), -Math.cos(r.yaw) * Math.cos(r.pitch)];
    const a = Math.acos(Math.max(-1, Math.min(1, (d[0] * s[0] + d[1] * s[1] + d[2] * s[2]) / l))) * 180 / Math.PI;
    aimAll.push(a);
    if (r.pitch > 0.35) aimUp.push(a);
    if (r.pitch < -0.35) aimDown.push(a);
  }
  // the bots' planted feet over uneven ground
  const ground = [];
  for (const slot of [2, 3, 4, 5]) {
  const bot = bots.filter((r) => r.slot === slot);
  for (let i = 1; i < bot.length; i++) {
    const r = bot[i];
    if (!r.alive || !r.floor || r.lod > 1) continue;
    if (Math.abs(r.ground[0] - r.ground[1]) < 0.04 || r.ground[0] < -999 || r.ground[1] < -999) continue;
    for (const [k, f] of [[0, 'foot_l'], [1, 'foot_r']]) {
      let lo = Infinity;
      for (let q = Math.max(0, i - 30); q < Math.min(bot.length, i + 30); q++) lo = Math.min(lo, bot[q].b[f][1] - bot[q].ground[k]);
      const h = r.b[f][1] - r.ground[k];
      if (h > lo + 0.03) continue; // in its stance
      ground.push(Math.abs(h - ankle));
    }
  }
  }
  const dirs = Object.keys(skateMed);
  const checks = [
    { name: 'skate', ok: dirs.filter((k) => skateMed[k].n >= 10).length >= 3 && dirs.every((k) => skateMed[k].median <= 0.25), detail: JSON.stringify(skateMed) },
    { name: 'strafe', ok: side.length >= 30 && sideOk / side.length >= 0.8 && backs.length >= 30 && backOk / backs.length >= 0.8, detail: `sideways ${sideOk}/${side.length} on a sideways clip, backwards ${backOk}/${backs.length} on a backwards one` },
    { name: 'aim', ok: aimAll.length > 60 && med(aimAll) <= 4 && pct(aimAll, 0.9) <= 10 && aimUp.length >= 10 && med(aimUp) <= 4 && aimDown.length >= 10 && med(aimDown) <= 4,
      detail: `bore off the shot: median ${med(aimAll).toFixed(2)} deg, p90 ${pct(aimAll, 0.9).toFixed(2)} (n ${aimAll.length}); up ${med(aimUp).toFixed(2)} (n ${aimUp.length}); down ${med(aimDown).toFixed(2)} (n ${aimDown.length})` },
    { name: 'ground', ok: ground.length >= 30 && med(ground) <= 0.03, detail: `planted ankle off its height over uneven ground: median ${(med(ground) * 100).toFixed(1)} cm, p90 ${(pct(ground, 0.9) * 100).toFixed(1)} cm (n ${ground.length})` },
  ];
  return { checks, rows: rows.length, steady: steady.length };
}

const cmdArg = process.argv[2];
if (cmdArg === 'run') await run();
else if (cmdArg === 'measure') {
  const dir = process.argv[3];
  const m = measure(dir);
  const json = process.argv.includes('--json');
  let fail = 0;
  for (const c of m.checks) { if (!c.ok) fail++; if (!json) console.log(`${c.ok ? 'ok  ' : 'FAIL'} ${c.name}: ${c.detail}`); }
  const cdir = arg('--control', null);
  let cfail = 0;
  if (cdir) {
    const c = measure(cdir);
    for (const k of c.checks) { if (!k.ok) cfail++; if (!json) console.log(`control ${k.ok ? 'PASSES (bad)' : 'fails (good)'} ${k.name}: ${k.detail}`); }
  }
  if (json) console.log(JSON.stringify({ run: m, control: cdir ? measure(cdir) : null }, null, 1));
  const ok = fail === 0 && (!cdir || cfail === 4);
  console.log(ok ? 'PASS' : 'FAIL');
  process.exit(ok ? 0 : 1);
} else {
  console.error('usage: node tests/arenafps-motion.mjs run [--cart <wasc>] [--gpu gl|webgpu] [--out <dir>] | measure <run dir> [--control <run dir>] [--json]');
  process.exit(2);
}
