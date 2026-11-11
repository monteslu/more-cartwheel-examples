// partypack on the cart through romdev, GL and WebGPU, by the pad: from the title the first round of a Party
// Tournament is played to its results and the board of round 2, by the on-screen cues only (the south button joins,
// Start plays, the highlighted Party Tournament, the board's next, the instructions card's "ready"), alone and with a
// second seat that joined and was never played. A seat nobody plays holds nobody on the card: once a person is
// ready, the seats still out see how long the card waits for them ("ready" and the seconds left) and the round
// starts with everyone after 10 s.
// The board of round 2 opens on its shop: once the person has picked, the seat still out gets 10 s, then everyone moves.
// A fork: with P1's token put on a fork (sw place 0 8) during round 1, the round-2 shop shows the route ({ui_up|ui_down});
// the person presses down for the shortcut, picks, and the token crosses the bridge (game.cut 8, past cell 11).
// Controls: sw no_forks 1 (no forks: the route row never shows and the walk goes the long way);
// sw ready_forever 1 (the card waits for every person, as it once did) must keep the round from starting;
// sw shop_forever 1 (the shop waits for every person) must keep the board from moving.
//   node tests/partypack-idle-seat.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.PARTYPACK_SESSION || 'partypack-idle-seat', OUT = join(ROOT, 'out/partypack-idle-seat');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/partypack.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': process.env.ROMDEV_AGENT || 'partypack-idle-seat' }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
let requests = 0; /* romdev requests, printed at the end */
const tool = async (name, args) => {
  requests++;
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': process.env.ROMDEV_AGENT || 'partypack-idle-seat' }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: a press is one request (held 4 frames, let go for 8), a stretch of play one request per second */
const seq = (...steps) => tool('input', { op: 'sequence', steps: steps.map(([p0, frames]) => ({ input: { ports: [p0, {}] }, frames })) });
const press = (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; return tool('input', { op: 'sequence', steps: [{ input: { ports: a }, frames: 4 }, { input: { ports: [{}, {}] }, frames: 8 }] }); };
const shot = (name) => tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, name) });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const S_BOARD = 3, S_INTRO = 4, S_COUNT = 5, S_PLAY = 6, S_RESULT = 8;
const waitState = async (want, max) => { let f = 0, s; for (; f < max; f += 30) { s = +(await rd('game.state')); if (want(s)) break; await step(30); } return { s, f }; };

// from the title: the seats join (P1, and P2 when idle), Start, Party Tournament, the board, ready on the card; P2
// never touches its pad. P1 runs about while the round plays (any direction, as someone finding the controls).
async function run(backend, tag, idle, pre, play = true, fork = false) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'south'); await step(20);            /* title: the south button goes to the join screen */
  await press(0, 'south'); if (idle) await press(1, 'south');
  await press(0, 'start'); await step(40);            /* join: Start plays, the hub */
  const humans = +(await rd('game.humans'));
  const unlocked = +(await rd('game.unlocked'));     /* a fresh save: every mini-game open (party points unlock hats) */
  await press(0, 'south');                            /* the highlighted Party Tournament */
  const board = await waitState((s) => s === S_BOARD, 120);
  await step(160); await press(0, 'south');           /* the board's next, once the tokens have moved */
  const intro = await waitState((s) => s === S_INTRO, 240);
  await step(40); await press(0, 'south');            /* the card's "ready" */
  await step(60);
  const left = +(await rd('game.ready_left'));
  await shot(`${backend}-${tag}-card.png`);
  const go = await waitState((s) => s >= S_COUNT, 60 * 20);
  const r = { unlocked, humans, board: board.s === S_BOARD, intro: intro.s === S_INTRO, left, started: go.s >= S_COUNT, wait: 60 + go.f, results: false, round: 0 };
  if (!r.started || !play) return r;
  await waitState((s) => s === S_PLAY, 400);
  if (fork) await tool('wasm', { op: 'command', text: 'sw place 0 8' }); /* P1 on the first fork before the board's rolls */
  const dirs = [[1, 0], [0, 1], [-1, 0], [0, -1]];
  let f = 0;
  for (; f < 60 * 120 && +(await rd('game.state')) < S_RESULT; f += 60) {
    const [x, y] = dirs[(f / 60) % 4];
    await seq([{ south: (f / 60) % 3 === 0, axes: { lx: x, ly: y } }, 30], [{}, 30]);
  }
  await pads({}, {});
  r.results = +(await rd('game.state')) === S_RESULT;
  await shot(`${backend}-${tag}-results.png`);
  await step(80); await press(0, 'south');            /* the results' next */
  const b2 = await waitState((s) => s === S_BOARD, 300);
  r.round = b2.s === S_BOARD ? +(await rd('game.round')) : -1;
  r.playFrames = f;
  /* the board's shop: the seat's roll and its pick over its chip, "pick" under it; P1 picks (south), the idle seat never does */
  await step(60);
  r.shop = +(await rd('game.phase')) === 0;
  r.fork = (await rd('game.fork'))[0];
  if (fork) { await press(0, 'down'); r.route = (await rd('game.route'))[0]; }
  await shot(`${backend}-${tag}-shop.png`);
  await press(0, 'south');
  let g = 0;
  for (; g < 60 * 14 && +(await rd('game.phase')) === 0; g += 30) await step(30);
  r.shopMoved = +(await rd('game.phase')) === 1;
  r.shopWait = g;
  r.cut = (await rd('game.cut'))[0];
  r.cell = (await rd('game.points'))[0];
  if (fork) await step(110);
  await shot(`${backend}-${tag}-moved.png`);
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('a fresh save has all eleven mini-games open', one.unlocked === 0x7ff, `game.unlocked ${one.unlocked.toString(16)}`);
  const locks = await run(backend, 'locks-control', false, 'sw old_locks 1', false);
  check('control: sw old_locks 1, a fresh save has only six open', locks.unlocked !== 0x7ff, `game.unlocked ${locks.unlocked.toString(16)}`);
  check('one seat: the card starts the round at once', one.humans === 1 && one.intro && one.started && one.wait <= 90, `${one.humans} person, started ${one.started} after ${one.wait} frames`);
  check('one seat: round 1 played to the results and the board of round 2', one.results && one.round === 1, `results ${one.results}, board round ${one.round} (played ${one.playFrames} frames)`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat nobody plays: the card shows how long it waits', two.humans === 2 && two.left >= 8 && two.left <= 10, `${two.humans} people, ${two.left} s shown under the seat still out`);
  check('a second seat nobody plays: the round starts within 10 s of the first ready', two.started && two.wait > 60 * 8 && two.wait <= 60 * 10 + 30, `started ${two.started} after ${two.wait} frames`);
  check('a second seat nobody plays: round 1 played to the results and the board of round 2', two.results && two.round === 1, `results ${two.results}, board round ${two.round} (played ${two.playFrames} frames)`);
  check('one seat: the shop opens and moves on as soon as the person picks', one.shop && one.shopMoved && one.shopWait <= 30, `shop ${one.shop}, moved ${one.shopMoved} after ${one.shopWait} frames`);
  check('a second seat nobody plays: the shop moves on within 10 s of the first pick', two.shop && two.shopMoved && two.shopWait > 60 * 8 && two.shopWait <= 60 * 10 + 30, `shop ${two.shop}, moved ${two.shopMoved} after ${two.shopWait} frames`);
  const ctl2 = await run(backend, 'shop-control', true, 'sw shop_forever 1');
  check('control: sw shop_forever 1, the idle seat holds the shop', ctl2.shop && !ctl2.shopMoved, `shop ${ctl2.shop}, moved ${ctl2.shopMoved} in ${ctl2.shopWait} frames`);
  const fk = await run(backend, 'fork', false, null, true, true);
  check('a fork: the shop shows the route, down picks the shortcut, the token crosses the bridge', fk.fork === 8 && fk.route === 1 && fk.cut === 8 && fk.cell >= 11, `fork ${fk.fork}, route ${fk.route}, cut ${fk.cut}, cell ${fk.cell}`);
  const fkc = await run(backend, 'fork-control', false, 'sw no_forks 1', true, true);
  check('control: sw no_forks 1, no route to pick and the walk goes the long way', !(fkc.fork === 8 && fkc.cut === 8), `fork ${fkc.fork}, route ${fkc.route}, cut ${fkc.cut}, cell ${fkc.cell}`);
  const ctl = await run(backend, 'control', true, 'sw ready_forever 1', false);
  check('control: sw ready_forever 1, the idle seat holds the card', ctl.humans === 2 && ctl.intro && !ctl.started && ctl.left === 0, `${ctl.humans} people, started ${ctl.started} in ${ctl.wait} frames, ${ctl.left} s shown`);
}
await pads({}, {});
console.log(`romdev requests: ${requests}`);
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
