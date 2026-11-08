// partypack on the cart through romdev, GL and WebGPU, by the pad: from the title the first round of a Party
// Tournament is played to its results and the board of round 2, by the on-screen cues only (the south button joins,
// Start plays, the highlighted Party Tournament, the board's next, the instructions card's "ready"), alone and with a
// second seat that joined and was never played. A seat nobody plays holds nobody on the card: once a person is
// ready, the seats still out see how long the card waits for them ("ready" and the seconds left) and the round
// starts with everyone after 10 s.
// Control: sw ready_forever 1 (the card waits for every person, as it once did) must keep the round from starting.
//   node tests/partypack-idle-seat.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.PARTYPACK_SESSION || 'partypack-idle-seat', OUT = join(ROOT, 'out/partypack-idle-seat');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/partypack.wasc')));
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
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await pads(...a); await step(4); await pads({}, {}); await step(8); };
const shot = (name) => tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, name) });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const S_BOARD = 3, S_INTRO = 4, S_COUNT = 5, S_PLAY = 6, S_RESULT = 8;
const waitState = async (want, max) => { let f = 0, s; for (; f < max; f += 10) { s = +(await rd('game.state')); if (want(s)) break; await step(10); } return { s, f }; };

// from the title: the seats join (P1, and P2 when idle), Start, Party Tournament, the board, ready on the card; P2
// never touches its pad. P1 runs about while the round plays (any direction, as someone finding the controls).
async function run(backend, tag, idle, pre, play = true) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); process.exit(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'south'); await step(20);            /* title: the south button goes to the join screen */
  await press(0, 'south'); if (idle) await press(1, 'south');
  await press(0, 'start'); await step(40);            /* join: Start plays, the hub */
  const humans = +(await rd('game.humans'));
  await press(0, 'south');                            /* the highlighted Party Tournament */
  const board = await waitState((s) => s === S_BOARD, 120);
  await step(160); await press(0, 'south');           /* the board's next, once the tokens have moved */
  const intro = await waitState((s) => s === S_INTRO, 240);
  await step(40); await press(0, 'south');            /* the card's "ready" */
  await step(60);
  const left = +(await rd('game.ready_left'));
  await shot(`${backend}-${tag}-card.png`);
  const go = await waitState((s) => s >= S_COUNT, 60 * 20);
  const r = { humans, board: board.s === S_BOARD, intro: intro.s === S_INTRO, left, started: go.s >= S_COUNT, wait: 60 + go.f, results: false, round: 0 };
  if (!r.started || !play) return r;
  await waitState((s) => s === S_PLAY, 400);
  const dirs = [[1, 0], [0, 1], [-1, 0], [0, -1]];
  let f = 0;
  for (; f < 60 * 120 && +(await rd('game.state')) < S_RESULT; f += 60) {
    const [x, y] = dirs[(f / 60) % 4];
    await pads({ south: (f / 60) % 3 === 0, axes: { lx: x, ly: y } }, {}); await step(30); await pads({}, {}); await step(30);
  }
  await pads({}, {});
  r.results = +(await rd('game.state')) === S_RESULT;
  await shot(`${backend}-${tag}-results.png`);
  await step(80); await press(0, 'south');            /* the results' next */
  const b2 = await waitState((s) => s === S_BOARD, 300);
  r.round = b2.s === S_BOARD ? +(await rd('game.round')) : -1;
  r.playFrames = f;
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('one seat: the card starts the round at once', one.humans === 1 && one.intro && one.started && one.wait <= 90, `${one.humans} person, started ${one.started} after ${one.wait} frames`);
  check('one seat: round 1 played to the results and the board of round 2', one.results && one.round === 1, `results ${one.results}, board round ${one.round} (played ${one.playFrames} frames)`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat nobody plays: the card shows how long it waits', two.humans === 2 && two.left >= 8 && two.left <= 10, `${two.humans} people, ${two.left} s shown under the seat still out`);
  check('a second seat nobody plays: the round starts within 10 s of the first ready', two.started && two.wait > 60 * 8 && two.wait <= 60 * 10 + 30, `started ${two.started} after ${two.wait} frames`);
  check('a second seat nobody plays: round 1 played to the results and the board of round 2', two.results && two.round === 1, `results ${two.results}, board round ${two.round} (played ${two.playFrames} frames)`);
  const ctl = await run(backend, 'control', true, 'sw ready_forever 1', false);
  check('control: sw ready_forever 1, the idle seat holds the card', ctl.humans === 2 && ctl.intro && !ctl.started && ctl.left === 0, `${ctl.humans} people, started ${ctl.started} in ${ctl.wait} frames, ${ctl.left} s shown`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
