// crawler on the cart through romdev, GL and WebGPU, by the pad: floor 1 is cleared from the title by one player
// walking with the stick (to the key, through its door, onto the exit), alone and with a second seat that joined and
// was never played. A seat nobody plays holds nobody back: the screen follows the player and carries the idle hero
// along, so the player reaches floor 2; the view glides off the idle hero (no camera jerk, view.cam_jerks, as alone).
// Control: sw idle_anchor 1 (the idle seat holds the team back, as the game once let it) must keep the player from
// the key, so floor 2 is never reached in the same frames.
//   node tests/crawler-left-behind.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.CRAWLER_SESSION || 'crawler-left-behind', OUT = join(ROOT, 'out/crawler-left-behind');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/crawler.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); process.exit(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await pads(...a); await step(3); await pads({}, {}); await step(6); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

// floor 1 (tools/levels/001.txt, 28 x 20 tiles): out of the start room, along the hall, up into the key's room, back
// down to the door, through it and onto the exit
const ROUTE = [[5, 4], [5, 8], [5, 10], [12, 10], [21, 9], [21, 7], [21, 5], [24, 3], [21, 5], [21, 7], [23, 10], [23.5, 11.5], [23.5, 13], [24, 15.4], [24, 16.2]];
const W = 28, H = 20;
const tile = async () => { const x = await rd('player.x'), y = await rd('player.y'); return [x[0] / 200 + W / 2 - 0.5, y[0] / 200 + H / 2 - 0.5]; };

// from the title: the seats join (P1, and P2 when idle), P1 picks its hero, then walks; P2 never touches its pad
async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); process.exit(3); }
  await step(20);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'start'); await step(10);
  await press(0, 'south'); if (idle) await press(1, 'south');
  await press(0, 'start'); await step(15);
  await press(0, 'south');
  for (let f = 0; f < 60 * 25 && (await rd('player.hp'))[0] === 0; f += 30) await step(30); /* the pick ends (all ready, or its 20 s) */
  const seats = (await rd('player.hp')).filter((h) => h > 0).length;
  let frames = 0, floor = 1;
  for (const [wx, wy] of ROUTE) {
    for (let f = 0; f < 600; f += 2) {
      const [tx, ty] = await tile(), dx = wx - tx, dy = wy - ty, d = Math.hypot(dx, dy);
      if (d < 0.3 || (await rd('game.state')) !== 0) break;
      await pads({ axes: { lx: dx / d, ly: dy / d } }, {});
      await step(2); frames += 2;
    }
    if ((await rd('game.state')) !== 0) break;
  }
  await pads({}, {});
  for (let f = 0; f < 240 && floor === 1; f += 10) { await step(10); floor = +(await rd('game.floor')); }
  const [tx, ty] = await tile();
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  return { seats, floor, frames, keys: +(await rd('game.keys_used')), carried: +(await rd('game.carried')), jerks: +(await rd('view.cam_jerks')), at: `${tx.toFixed(1)},${ty.toFixed(1)}` };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('one seat: floor 1 cleared by the stick', one.seats === 1 && one.floor === 2, `${one.seats} seat(s), floor ${one.floor} after ${one.frames} frames (keys used ${one.keys})`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat nobody plays: floor 1 still cleared', two.seats === 2 && two.floor === 2 && two.carried > 0, `${two.seats} seats, floor ${two.floor} after ${two.frames} frames (keys used ${two.keys}, the idle hero carried ${two.carried} times)`);
  check('the view glides off the idle hero', two.jerks <= one.jerks, `${two.jerks} camera jerks (alone ${one.jerks})`);
  const ctl = await run(backend, 'control', true, 'sw idle_anchor 1');
  check('control: sw idle_anchor 1, the idle seat holds the player short of the key', ctl.seats === 2 && ctl.floor === 1 && ctl.keys === 0, `${ctl.seats} seats, floor ${ctl.floor}, keys used ${ctl.keys}, the player stopped at ${ctl.at}`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
