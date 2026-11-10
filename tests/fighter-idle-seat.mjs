// fighter on the cart through romdev, GL and WebGPU, by the pad: a second pad that joined and is never played does not
// keep player 1 on the select screen. From the title player 1 joins (and, in the idle run, a second pad joins and never
// touches its pad again), player 1 presses lock in and then waits, as the screen says. Alone, the match against the
// computer starts at once. With the idle seat, the screen counts down "P2 has not locked in: the computer plays P2 in N"
// and the match starts with the computer playing P2 (it moves or lands a blow on player 1 within 20 s).
// Control: sw seat_wait 1 (the select screen waits for ever for a seat that never locks in, as it once did) must leave
// player 1 on the select screen for the same frames.
//   node tests/fighter-idle-seat.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.FIGHTER_SESSION || 'fighter-idle-seat', OUT = join(ROOT, 'out/fighter-idle-seat');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/fighter.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p0, p1 = {}) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, p1] }, frames }] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await hold(4, ...a); await hold(8, {}, {}); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const WAIT = 60 * 20; /* player 1 waits this long after locking in (the seat's wait is 12 s) */

async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'south'); await step(20);  /* the title: Play */
  await press(0, 'south');                  /* the join screen: P1 takes a seat */
  if (idle) await press(1, 'south');        /* a second pad takes one, and is never touched again */
  await press(0, 'start'); await step(30);  /* play */
  const seats = +(await rd('shell.players'));
  await press(0, 'south');                  /* the select screen: lock in */
  let frames = 0, banner = 0, shot = false;
  while (frames < WAIT && +(await rd('game.panel')) === 0) {
    await step(30); frames += 30;
    const w = +(await rd('seat.wait'));
    if (w > 60 * 6) banner = Math.max(banner, w);
    if (w > 60 * 8 && !shot) { shot = true; await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-countdown.png`) }); }
  }
  const panel = +(await rd('game.panel')), cpu = await rd('seat.cpu');
  let acted = false;
  if (panel === 1) { /* the match: player 1 stands; the side across moves or lands a blow */
    const pos0 = (await rd('sim.pos'))[2], hp0 = (await rd('sim.hp'))[0];
    for (let f = 0; f < 60 * 20 && !acted; f += 30) {
      await step(30);
      acted = Math.abs((await rd('sim.pos'))[2] - pos0) > 100 || (await rd('sim.hp'))[0] < hp0;
    }
  }
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  return { seats, panel, frames, cpu: [...cpu], acted, banner };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('one seat: lock in, the match against the computer starts', one.seats === 1 && one.panel === 1 && one.frames <= 60 && one.acted, `${one.seats} seat(s), panel ${one.panel} after ${one.frames} frames, the computer acted ${one.acted}`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat nobody plays: the match still starts', two.seats === 2 && two.panel === 1 && two.frames < WAIT, `${two.seats} seats, panel ${two.panel} after ${two.frames} frames`);
  check('the countdown shows on the select screen', two.banner > 60 * 6, `seat.wait reached ${two.banner} ticks with the countdown up`);
  check('the computer plays the idle seat', two.cpu[0] === 0 && two.cpu[1] === 1 && two.acted, `seat.cpu ${two.cpu}, P2 acted ${two.acted}`);
  const ctl = await run(backend, 'control', true, 'sw seat_wait 1');
  check('control: sw seat_wait 1, the select screen waits for the idle seat', ctl.seats === 2 && ctl.panel === 0 && ctl.frames >= WAIT, `${ctl.seats} seats, panel ${ctl.panel} after ${ctl.frames} frames`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
