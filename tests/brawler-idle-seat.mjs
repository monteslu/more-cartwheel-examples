// Street Toughs on the cart through romdev, GL and WebGPU, by the pad: a second pad that joined and is never played does
// not keep player 1 on the character select, and nothing is sized for it. From the title player 1 joins (and, in the idle
// run, a second pad joins and never touches its pad again), plays, and on the character select presses attack to be
// ready, as its card says. Alone, the stage starts at once. With the idle seat, the select counts down ("P2 not ready:
// starting in N") and the stage starts without it: the seat is out (no fighter, the waves sized for one), and player 1
// walks on and lands blows.
// Control: sw pick_wait 1 (the select waits for every seat for ever) must leave player 1 on the select for the same frames.
//   node tests/brawler-idle-seat.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.BRAWLER_SESSION || 'brawler-idle-seat', OUT = join(ROOT, 'out/brawler-idle-seat');
const PORT = process.env.ROMDEV_PORT || '7331';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/brawler.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const HDR = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': 'brawler-idle-seat' };
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(`http://127.0.0.1:${PORT}/tool/host`, { method: 'POST', headers: HDR, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:${PORT}/tool/${name}`, { method: 'POST', headers: HDR, body: JSON.stringify(args) });
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
const WAIT = 60 * 20; /* player 1 waits this long after being ready (the idle seat's countdown is 6 s) */

async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'south'); await step(20);  /* the title: Play */
  await press(0, 'south');                  /* the join screen: P1 takes a seat */
  if (idle) await press(1, 'south');        /* a second pad takes one, and is never touched again */
  await press(0, 'start'); await step(30);  /* play: the character select */
  const seats = +(await rd('shell.players')), opened = +(await rd('pick.on'));
  await press(0, 'west');                   /* attack: ready (the card's prompt) */
  let frames = 0, left = 0, shot = false;
  while (frames < WAIT && +(await rd('pick.on')) === 1) {
    await step(30); frames += 30;
    const l2 = +(await rd('pick.left'));
    if (l2 > left) left = l2;
    if (l2 > 0 && l2 < 60 * 4 && !shot) { shot = true; await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-countdown.png`) }); }
  }
  const pick = +(await rd('pick.on')), absent = [...(await rd('seat.absent'))], crew = +(await rd('game.crew'));
  let hits = 0, px0 = 0, px1 = 0;
  if (pick === 0) { /* the stage: walk on into the first wave and swing */
    px0 = (await rd('game.px'))[0];
    for (let f = 0; f < 60 * 20 && hits === 0; f += 12) {
      await pads({ right: f < 60 * 3, west: (f / 12) % 2 === 1 }, {});
      await step(12);
      hits = +(await rd('game.hits'));
    }
    px1 = (await rd('game.px'))[0];
    await pads({}, {});
  }
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  return { seats, opened, pick, frames, absent, crew, hits, moved: px1 - px0, left };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('one seat: ready, the stage starts at once', one.seats === 1 && one.opened === 1 && one.pick === 0 && one.frames <= 60 && one.hits > 0, `${one.seats} seat(s), select up ${one.opened}, left after ${one.frames} frames, P1 moved ${one.moved.toFixed(2)} m and landed ${one.hits}`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat nobody plays: the stage still starts', two.seats === 2 && two.pick === 0 && two.frames < WAIT, `${two.seats} seats, select up ${two.pick} after ${two.frames} frames`);
  check('the countdown shows on the select', two.left > 60 * 3, `pick.left reached ${two.left} ticks`);
  check('the idle seat is out and nothing is sized for it', two.absent[0] === 0 && two.absent[1] === 1 && two.crew === 1, `seat.absent ${two.absent}, game.crew ${two.crew}`);
  check('player 1 plays on', two.hits > 0 && two.moved > 0.5, `moved ${two.moved.toFixed(2)} m, landed ${two.hits}`);
  const ctl = await run(backend, 'control', true, 'sw pick_wait 1');
  check('control: sw pick_wait 1, the select waits for the idle seat', ctl.seats === 2 && ctl.pick === 1 && ctl.frames >= WAIT, `${ctl.seats} seats, select up ${ctl.pick} after ${ctl.frames} frames`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
