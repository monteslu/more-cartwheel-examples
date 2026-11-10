// What a rider does in the air, by the pad on the cart through romdev, GL and WebGPU, sampled every tick. A ride up a ramp from a run-up, stick and buttons only once it is flying.
//   big ramp: more air than a normal one on the same course. Control: the normal one fails the same bound.
//   flips: the hull's pitch turns one whole turn, up and over for a back flip and nose first for a front flip, and it lands with no wipeout. Control: a barrel roll on the same button turns the roll and not the pitch.
//   kick: the foot goes out to the side as the rider is posed, and stays in its well with no press. Pictures of both are kept to look at.
//   ways down: nose down along the line of flight goes under the surface and comes back up with no wipeout, off a big ramp and a normal one. Tail first is a wipeout that never goes under; a flat landing does neither.
//   Controls: dive 0 the same fall stays on top.
//   node tests/jetski-air.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCourse, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
// Not ranged yet: this check samples the craft every frame through romdev (a request or more a frame), which floods the
// shared server. It stays off until the game keeps what it measures in its own debug fields (the lowest point, the turns,
// the words' frames) and the check reads them once after a ranged run. CHECK_PER_FRAME=1 runs it anyway, on a romdev of
// your own only.
if (!process.env.CHECK_PER_FRAME) { console.error(`${process.argv[1]}: steps romdev a frame at a time; not run (see the note at the top)`); process.exit(2); }
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.JETSKI_SESSION || 'jetski-air', OUT = join(ROOT, 'out/air-gate');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc'))), BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const TURN = 2 * Math.PI, AIR_MORE = 1.25;
mkdirSync(OUT, { recursive: true });
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); process.exit(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (text) => tool('wasm', { op: 'command', text }), step = (frames) => tool('frame', { op: 'step', frames }), pad = (p) => tool('input', { op: 'set', ports: [p] });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const wrap = (a) => { while (a > Math.PI) a -= TURN; while (a < -Math.PI) a += TURN; return a; };
const load = async (backend) => { const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) }); if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); process.exit(3); } await step(20); };
// one ride: the run-up, then what the pad does from the moment it flies. The answer is a sample per tick from the first flying tick until it has been down for a while
// (a picture is taken at the first tick with that much air)
async function ride(course, ramp, stick, { pre = '', shotAir = -1, shot = null, after = 150 } = {}) {
  await pad({});
  await cmd(`course ${course}`); await cmd('no_dq 1'); await cmd('swell 0.5');
  if (pre) await cmd(pre);
  await cmd('race'); await step(4); await cmd(`ramp ${ramp} 50`);
  const s = { min: 0, pitch: 0, roll: 0, flying: 0, foot: 0, spd0: 0, spdEnd: 0 };
  let t = -1, last = null, down = 0, tick = 0, turnP = 0, turnR = 0, tricks0 = 0, turning = false, landed = false, shotDone = false;
  for (let f = 0; f < 1000; f++) {
    // flying: off the ramp and well over the water, not a hop on the swell of the run-up
    const air = +(await rd('p0.air_time'));
    if (t < 0 && air > 0.05) { const [p0, s0] = await Promise.all(['p0.pos', 'p0.surf'].map(rd)); if (p0[1] - s0 > 1.5) t = 0; }
    // a rider lets go of the stick once down
    const input = t >= 0 && !landed ? stick(t) : {};
    await pad({ south: true, ...input });
    if (shot && !shotDone && t >= 0 && air >= shotAir) { shotDone = true; await tool('frame', { op: 'stepAndShot', frames: 1, path: shot }); } else await step(1);
    if (t < 0) continue;
    t += 1 / 60, tick++;
    const [pos, surf, pitch, roll, foot, tricks, spd] = await Promise.all(['p0.pos', 'p0.surf', 'p0.pitch', 'p0.roll', 'rider.foot_side', 'p0.tricks', 'p0.speed'].map(rd));
    s.min = Math.min(s.min, pos[1] - surf), s.foot = Math.max(s.foot, +foot);
    if (last) {
      if (input.west && !turning && tricks === tricks0) turning = true;
      if (turning && !s.turned) { turnP += wrap(pitch - last.pitch), turnR += wrap(roll - last.roll); if (tricks > tricks0) s.turned = true; }
    }
    if (!s.spd0) s.spd0 = +spd;
    tricks0 = turning ? tricks0 : tricks;
    last = { pitch, roll };
    const a2 = +(await rd('p0.air_time')), under = +(await rd('p0.dive_t'));
    if (a2 === 0) landed = true;
    if (a2 === 0 && under <= 0) { if (++down >= after) { s.spdEnd = +spd; s.endH = pos[1] - surf; break; } } else down = 0;
  }
  await pad({});
  Object.assign(s, { turnP, turnR, air: +(await rd('p0.air_max')), spills: +(await rd('p0.spills')), dives: +(await rd('p0.dives')), depth: +(await rd('p0.dive_max')), underS: +(await rd('p0.dive_under')), tricks: +(await rd('p0.tricks')), kicks: +(await rd('p0.kicks')), keep: +(await rd('p0.land_keep')), camUnder: +(await rd('cam.under_ticks')), hash: await rd('race.hash') });
  return s;
}
const f2 = (v) => (+v).toFixed(2);
const PRESS = 6 / 60, FROM = 0.2;
const held = (ly, button) => (t) => (t < FROM ? {} : t < FROM + PRESS ? { [button]: true, axes: { lx: 0, ly } } : t < FROM + 0.5 ? { axes: { lx: 0, ly } } : {});
const fwd = () => ({ axes: { lx: 0, ly: -1 } }), back = () => ({ axes: { lx: 0, ly: 1 } }), none = () => ({});
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  await load(backend);
  const c = readCourse('atoll'), lv = COURSE_IDS.indexOf('atoll'), big = c.ramps.findIndex((r) => r[5] > 0), norm = c.ramps.findIndex((r) => !(r[5] > 0) && r[4] < 1);
  // the ramps
  const plainB = await ride(lv, big, none), plainN = await ride(lv, norm, none);
  check('a big ramp gives more air than a normal one', plainB.air >= plainN.air * AIR_MORE, `${f2(plainB.air)} s against ${f2(plainN.air)} s, bound ${AIR_MORE} times`);
  check('control: the normal ramp against the big one fails the same bound', plainN.air < plainB.air * AIR_MORE, `${f2(plainN.air)} s is under ${f2(plainB.air * AIR_MORE)} s`);
  check('flat off a big ramp: no wipeout, no dive', plainB.spills === 0 && plainB.dives === 0 && plainB.min > -1, `${plainB.spills} spills, ${plainB.dives} dives, ${f2(plainB.min)} m at the lowest, kept ${f2(plainB.keep)}`);
  // flips
  const bf = await ride(lv, big, held(1, 'west')), ff = await ride(lv, big, held(-1, 'west')), roll = await ride(lv, big, held(0, 'west'));
  check('a back flip turns the pitch one whole turn, nose up and over', bf.turnP > TURN * 0.9 && bf.turnP < TURN * 1.15 && bf.tricks === 1, `${f2(bf.turnP / TURN)} turns, ${bf.tricks} trick`);
  check('a back flip lands', bf.spills === 0, `${bf.spills} spills, kept ${f2(bf.keep)}`);
  check('a front flip turns the pitch one whole turn, nose first', ff.turnP < -TURN * 0.9 && ff.turnP > -TURN * 1.15 && ff.tricks === 1, `${f2(ff.turnP / TURN)} turns, ${ff.tricks} trick`);
  check('a front flip lands', ff.spills === 0, `${ff.spills} spills, kept ${f2(ff.keep)}`);
  check('control: a barrel roll turns the roll, not the pitch', Math.abs(roll.turnP) < TURN * 0.2 && Math.abs(roll.turnR) > TURN * 0.9, `${f2(roll.turnP / TURN)} turns of pitch, ${f2(roll.turnR / TURN)} of roll`);
  // the kick: the foot as the rider is posed and drawn; the pictures of both rides are kept to look at (two romdev rides do not line up to the tick, so no pixel count)
  const kick = await ride(lv, big, (t) => (t >= FROM && t < FROM + PRESS ? { north: true } : {}), { shotAir: 0.6, shot: join(OUT, `${backend}-kick.png`), after: 30 });
  const noKick = await ride(lv, big, none, { shotAir: 0.6, shot: join(OUT, `${backend}-nokick.png`), after: 30 });
  check('the kick button puts a foot out to the side', kick.foot > 0.7 && kick.kicks === 1 && kick.spills === 0, `foot ${f2(kick.foot)} m out, ${kick.kicks} kick, ${kick.spills} spills`);
  check('control: no button, the foot stays in its well', noKick.foot < 0.5 && noKick.kicks === 0, `foot ${f2(noKick.foot)} m out, ${noKick.kicks} kicks`);
  // the ways down
  await load(backend);
  for (const [name, r] of [['a big ramp', big], ['a normal ramp', norm]]) {
    const d = await ride(lv, r, fwd);
    check(`nose down off ${name}: under the surface`, d.dives === 1 && d.min < -1 && d.depth > 1 && d.underS > 0.2, `${d.dives} dive, ${f2(d.min)} m at the lowest, ${f2(d.underS)} s more than 0.5 m under`);
    check(`nose down off ${name}: back up, no wipeout, keeps going`, d.spills === 0 && d.endH > -1 && d.spdEnd > 10 && d.camUnder === 0, `${d.spills} spills, ${f2(d.endH)} m at the end, ${f2(d.spdEnd)} m/s, camera under ${d.camUnder} ticks`);
  }
  const off = await ride(lv, big, fwd, { pre: 'sw dive 0' }); await cmd('sw dive 1');
  check('control: dive 0, the same fall stays on top', off.dives === 0 && off.min > -1, `${off.dives} dives, ${f2(off.min)} m at the lowest, ${off.spills} spills`);
  const tail = await ride(lv, big, back);
  check('tail first: a wipeout, and it never goes under', tail.spills === 1 && tail.dives === 0 && tail.min > -1, `${tail.spills} spills, ${tail.dives} dives, ${f2(tail.min)} m at the lowest`);
}
await pad({});
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
