// What a rider does in the air, by the pad on the cart through romdev, GL and WebGPU. A ride up a ramp from a run-up, stick and buttons only once it is flying.
//   big ramp: more air than a normal one on the same course. Control: the normal one fails the same bound.
//   flips: the hull's pitch turns one whole turn, up and over for a back flip and nose first for a front flip, and it lands with no wipeout. Control: a barrel roll on the same button turns the roll and not the pitch.
//   kick: the foot goes out to the side as the rider is posed, and stays in its well with no press. Pictures of both are kept to look at.
//   ways down: nose down along the line of flight goes under the surface and comes back up with no wipeout, off a big ramp and a normal one. Tail first is a wipeout that never goes under; a flat landing does neither.
//   Controls: dive 0 the same fall stays on top.
// The game keeps what a flight did in its debug fields (fly.*: the tick it started, the lowest point against the surface,
// the furthest a foot went out, the first trick's turn of pitch and roll, the ticks back on top), so a ride is a few
// ranged requests: the run-up looks every 6 frames for the flight to start, the stick from then on is one input sequence
// timed from the flight's first tick, and the measures are read once at the end. The request count is printed.
//   node tests/jetski-air.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCourse, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.JETSKI_SESSION || 'jetski-air', OUT = join(ROOT, 'out/air-gate');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc'))), BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const TURN = 2 * Math.PI, AIR_MORE = 1.25, LOOK = 6;
mkdirSync(OUT, { recursive: true });
let calls = 0;
const tool = async (name, args) => {
  calls++;
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': process.env.CW_ROMDEV_AGENT || 'jetski-air' }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
// every exit tears this session's romdev host down: a host left loaded holds the server's memory
async function bye(code) {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
}
/** every named field in one request */
const rdAll = async (names) => {
  const st = await tool('wasm', { op: 'debugState' });
  const o = {};
  for (const f of st.fields || []) if (names.includes(f.name)) o[f.name] = f.values ?? f.value;
  return o;
};
const cmd = (text) => tool('wasm', { op: 'command', text }), step = (frames) => tool('frame', { op: 'step', frames });
/* ranges, not loops: each [pad, frames] held for its frames, the whole list in one request */
const hold = (list) => tool('input', { op: 'sequence', steps: list.filter(([, n]) => n > 0).map(([p, frames]) => ({ input: { ports: [p] }, frames })) });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const load = async (backend) => { const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) }); if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); } await step(20); };
const GAS = { south: true };
const F = ['fly.tick', 'fly.flights', 'race.tick', 'p0.air_time', 'fly.land_tick', 'fly.down_ticks'];
/**
 * one ride: the run-up with the gas held, then what the pad does from the moment it flies. stick(k) is the pad k ticks
 * after the flight started (k = 0 the tick after it). `until` is how long the stick is asked: a number of ticks, or
 * 'land' (held until it is down; looked for every 6 frames). The answer is the flight's measures once it has been back
 * on top for `after` ticks (a picture at the first tick with shotAir of air, when asked).
 */
async function ride(course, ramp, stick, until, { pre = '', shotAir = -1, shot = null, after = 150 } = {}) {
  await hold([[{}, 1]]);
  await cmd(`course ${course}`); await cmd('no_dq 1'); await cmd('swell 0.5');
  if (pre) await cmd(pre);
  await cmd('race'); await step(4); await cmd(`ramp ${ramp} 50`);
  const n0 = +(await rdAll(['fly.flights']))['fly.flights'];
  let s = null;
  // the run-up from 50 m short of the ramp takes 4 to 5 s: its first 200 frames in one stretch, then a look every 12
  // frames for a stick that starts 12 ticks into the flight (it is never missed), every 6 for one held from its start
  const look = until === 'land' ? LOOK : FROM;
  await hold([[GAS, 200]]);
  for (let f = 200; f < 1000; f += look) {
    await hold([[GAS, look]]);
    s = await rdAll(F);
    if (+s['fly.flights'] > n0) break;
  }
  if (!(+s['fly.flights'] > n0)) return { flew: false };
  // the stick from the flight's first tick: the ticks the run-up's look already ran had the gas alone
  let k = +s['race.tick'] - +s['fly.tick'];
  const pad = (i) => ({ ...GAS, ...stick(i) });
  if (until === 'land') {
    // held while it flies: a look every 6 frames for the tick it is down, then the gas alone
    for (let f = 0; f < 1000 && !+s['fly.land_tick']; f += LOOK, k += LOOK) { await hold([[pad(k), LOOK]]); s = await rdAll(F); }
  } else {
    // the stick's whole timeline in one request, each run of the same pad held as one step
    const list = [];
    for (; k < until; k++) { const p = pad(k); if (list.length && JSON.stringify(list.at(-1)[0]) === JSON.stringify(p)) list.at(-1)[1]++; else list.push([p, 1]); }
    if (list.length) await hold(list);
  }
  if (shot) {
    // the picture: the first frame after a tick with shotAir of air
    s = await rdAll(F);
    const air = +s['p0.air_time'];
    if (air < shotAir) await hold([[GAS, Math.ceil((shotAir - air) * 60 - 1e-6)]]);
    await tool('frame', { op: 'stepAndShot', frames: 1, path: shot });
  }
  // back on top for `after` ticks: once down, the ticks still to go in one stretch; still up, a look every 10 frames
  for (let f = 0; f < 1500;) {
    s = await rdAll(F);
    const down = +s['fly.land_tick'] ? +s['fly.down_ticks'] : -1;
    if (down >= after) break;
    const n = down >= 0 ? after - down : 10;
    await hold([[GAS, n]]);
    f += n;
  }
  await hold([[{}, 1]]);
  const m = await rdAll(['fly.low', 'fly.foot', 'fly.turn_pitch', 'fly.turn_roll', 'p0.air_max', 'p0.spills', 'p0.dives', 'p0.dive_max', 'p0.dive_under', 'p0.tricks', 'p0.kicks', 'p0.land_keep', 'cam.under_ticks', 'p0.speed', 'p0.pos', 'p0.surf', 'fly.down_ticks']);
  return { flew: true, min: +m['fly.low'], foot: +m['fly.foot'], turnP: +m['fly.turn_pitch'], turnR: +m['fly.turn_roll'], air: +m['p0.air_max'], spills: +m['p0.spills'], dives: +m['p0.dives'], depth: +m['p0.dive_max'], underS: +m['p0.dive_under'], tricks: +m['p0.tricks'], kicks: +m['p0.kicks'], keep: +m['p0.land_keep'], camUnder: +m['cam.under_ticks'], spdEnd: +m['p0.speed'], endH: m['p0.pos'][1] - +m['p0.surf'], down: +m['fly.down_ticks'] };
}
const f2 = (v) => (+v).toFixed(2);
const PRESS = 6, FROM = 12; // ticks: the press 0.2 s into the flight, held 0.1 s, the stick 0.5 s from then
const held = (ly, button) => (k) => (k < FROM ? {} : k < FROM + PRESS ? { [button]: true, axes: { lx: 0, ly } } : k < FROM + 30 ? { axes: { lx: 0, ly } } : {});
const fwd = () => ({ axes: { lx: 0, ly: -1 } }), back = () => ({ axes: { lx: 0, ly: 1 } }), none = () => ({});
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const c0 = calls;
  await load(backend);
  const c = readCourse('atoll'), lv = COURSE_IDS.indexOf('atoll'), big = c.ramps.findIndex((r) => r[5] > 0), norm = c.ramps.findIndex((r) => !(r[5] > 0) && r[4] < 1);
  // the ramps
  const plainB = await ride(lv, big, none, 0), plainN = await ride(lv, norm, none, 0);
  check('a big ramp gives more air than a normal one', plainB.flew && plainB.air >= plainN.air * AIR_MORE, `${f2(plainB.air)} s against ${f2(plainN.air)} s, bound ${AIR_MORE} times`);
  check('control: the normal ramp against the big one fails the same bound', plainN.flew && plainN.air < plainB.air * AIR_MORE, `${f2(plainN.air)} s is under ${f2(plainB.air * AIR_MORE)} s`);
  check('flat off a big ramp: no wipeout, no dive', plainB.spills === 0 && plainB.dives === 0 && plainB.min > -1, `${plainB.spills} spills, ${plainB.dives} dives, ${f2(plainB.min)} m at the lowest, kept ${f2(plainB.keep)}`);
  // flips
  const bf = await ride(lv, big, held(1, 'west'), FROM + 30), ff = await ride(lv, big, held(-1, 'west'), FROM + 30), roll = await ride(lv, big, held(0, 'west'), FROM + 30);
  check('a back flip turns the pitch one whole turn, nose up and over', bf.turnP > TURN * 0.9 && bf.turnP < TURN * 1.15 && bf.tricks === 1, `${f2(bf.turnP / TURN)} turns, ${bf.tricks} trick`);
  check('a back flip lands', bf.flew && bf.spills === 0, `${bf.spills} spills, kept ${f2(bf.keep)}`);
  check('a front flip turns the pitch one whole turn, nose first', ff.turnP < -TURN * 0.9 && ff.turnP > -TURN * 1.15 && ff.tricks === 1, `${f2(ff.turnP / TURN)} turns, ${ff.tricks} trick`);
  check('a front flip lands', ff.flew && ff.spills === 0, `${ff.spills} spills, kept ${f2(ff.keep)}`);
  check('control: a barrel roll turns the roll, not the pitch', roll.flew && Math.abs(roll.turnP) < TURN * 0.2 && Math.abs(roll.turnR) > TURN * 0.9, `${f2(roll.turnP / TURN)} turns of pitch, ${f2(roll.turnR / TURN)} of roll`);
  // the kick: the foot as the rider is posed and drawn; the pictures of both rides are kept to look at (two romdev rides do not line up to the tick, so no pixel count)
  const kick = await ride(lv, big, (k) => (k >= FROM && k < FROM + PRESS ? { north: true } : {}), FROM + PRESS, { shotAir: 0.6, shot: join(OUT, `${backend}-kick.png`), after: 30 });
  const noKick = await ride(lv, big, none, 0, { shotAir: 0.6, shot: join(OUT, `${backend}-nokick.png`), after: 30 });
  check('the kick button puts a foot out to the side', kick.foot > 0.7 && kick.kicks === 1 && kick.spills === 0, `foot ${f2(kick.foot)} m out, ${kick.kicks} kick, ${kick.spills} spills`);
  check('control: no button, the foot stays in its well', noKick.flew && noKick.foot < 0.5 && noKick.kicks === 0, `foot ${f2(noKick.foot)} m out, ${noKick.kicks} kicks`);
  // the ways down
  await load(backend);
  for (const [name, r] of [['a big ramp', big], ['a normal ramp', norm]]) {
    const d = await ride(lv, r, fwd, 'land');
    check(`nose down off ${name}: under the surface`, d.dives === 1 && d.min < -1 && d.depth > 1 && d.underS > 0.2, `${d.dives} dive, ${f2(d.min)} m at the lowest, ${f2(d.underS)} s more than 0.5 m under`);
    check(`nose down off ${name}: back up, no wipeout, keeps going`, d.spills === 0 && d.endH > -1 && d.spdEnd > 10 && d.camUnder === 0, `${d.spills} spills, ${f2(d.endH)} m at the end, ${f2(d.spdEnd)} m/s, camera under ${d.camUnder} ticks`);
  }
  const off = await ride(lv, big, fwd, 'land', { pre: 'sw dive 0' }); await cmd('sw dive 1');
  check('control: dive 0, the same fall stays on top', off.flew && off.dives === 0 && off.min > -1, `${off.dives} dives, ${f2(off.min)} m at the lowest, ${off.spills} spills`);
  const tail = await ride(lv, big, back, 'land');
  check('tail first: a wipeout, and it never goes under', tail.spills === 1 && tail.dives === 0 && tail.min > -1, `${tail.spills} spills, ${tail.dives} dives, ${f2(tail.min)} m at the lowest`);
  console.log(`  ${backend}: ${calls - c0} romdev requests`);
}
await hold([[{}, 1]]);
console.log(`${calls} romdev requests`);
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
