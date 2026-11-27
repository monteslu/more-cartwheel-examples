// Two players on one screen, on the cart through romdev, GL and WebGPU, by the pads alone: from power-on the first
// pad sets Players to 2 on the title menu (the row's own left / right) and starts the championship; the race draws two
// views side by side, each chased by its own camera with its own HUD, and each pad rides its own craft.
// The second seat idle-safe: a second pad that never presses anything does not hold the race up. After 6 s let go in
// a race the computer rides for it (its view says so, and its throttle takes it back); the first player finishes, the
// race closes, the podium and the results come up and the first pad goes on to the next day.
// Controls (must fail): sw p2_idle_cpu 0 leaves the idle seat on the grid; one player (Players 1) draws one view.
//   node tests/jetski-split.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/split');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski-split', 'x-romdev-agent': 'jetski-split' };
const bye = async (code) => { try { await fetch(ROMDEV_URL + '/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {} process.exit(code); };
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
const tool = async (name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text(); let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (text) => tool('wasm', { op: 'command', text });
/* a tap on one pad (port 0 or 1), then everything let go */
const tap = (button, port = 0) => { const p = [{}, {}]; p[port] = { [button]: true }; return tool('input', { op: 'sequence', steps: [{ input: { ports: p }, frames: 3 }, { input: { ports: [{}, {}] }, frames: 6 }] }); };
const hold = (frames, p0, p1 = {}) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, p1] }, frames }] });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f1 = (v) => (+v).toFixed(1);
const P_MAIN = 1, P_RESULTS = 6, P_FORECAST = 7, P_PODIUM = 8, GRID = 1, ON = 2, DONE = 3;
const state = async (...names) => { const o = {}; for (const n of names) { const v = await rd(n); o[n] = Array.isArray(v) ? v.map(Number) : +v; } return o; };

async function load(backend) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 120 });
  for (let k = 0; k < 120 && +(await rd('cw.loading')) !== 0; k++) await tool('frame', { op: 'step', frames: 30 });
  await tool('frame', { op: 'step', frames: 30 });
}
/* from the title by the first pad: Players to 2 (up from Start wraps to the Players row), back to Start, the forecast, the first course */
async function twoPlayerRace(players = 2) {
  await tap('up'); if (players === 2) await tap('right');
  await tap('down'); await tap('south'); await tool('frame', { op: 'step', frames: 20 });
  await tap('south'); await tool('frame', { op: 'step', frames: 30 });
}
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  await load(backend);
  await cmd('course 0'); await cmd('mode 0'); await cmd('sw players 1'); await cmd('no_dq 1');
  await tool('frame', { op: 'step', frames: 10 });
  const title = await state('menu.page', 'race.players');
  await twoPlayerRace();
  const grid = await state('race.state', 'race.players', 'view.views', 'menu.page');
  check(`${backend}: the first pad sets Players to 2 and starts the race, two views`, title['menu.page'] === P_MAIN && grid['race.players'] === 2 && (grid['race.state'] === GRID || grid['race.state'] === ON) && grid['view.views'] === 2, `title page ${title['menu.page']}, then players ${grid['race.players']}, state ${grid['race.state']}, views ${grid['view.views']}`);
  /* both pads ride: throttle on each, a little stick on the second */
  await hold(420, { south: true }, { south: true, right: false });
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-both.png`) });
  const both = await state('p0.speed', 'p1.speed', 'race.seat_cpu', 'p0.dist', 'p1.dist', 'cam.pos');
  check(`${backend}: each pad rides its own craft`, both['p0.speed'] > 10 && both['p1.speed'] > 10 && both['race.seat_cpu'][1] === 0, `speeds ${f1(both['p0.speed'])} and ${f1(both['p1.speed'])} m/s, the computer on seat 2 ${both['race.seat_cpu'][1]}`);
  /* the second pad lets go: within 6 s the computer rides for it, and its throttle takes it back */
  await hold(480, { south: true }, {});
  const idle = await state('p1.speed', 'race.seat_cpu', 'p1.idle');
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-idle.png`) });
  await hold(30, { south: true }, { south: true });
  const back = await state('race.seat_cpu');
  check(`${backend}: an idle second pad is ridden by the computer, and takes it back with its throttle`, idle['race.seat_cpu'][1] === 1 && idle['p1.speed'] > 8 && back['race.seat_cpu'][1] === 0, `after 8 s let go: computer ${idle['race.seat_cpu'][1]} at ${f1(idle['p1.speed'])} m/s (idle ${f1(idle['p1.idle'])} s); after a press ${back['race.seat_cpu'][1]}`);
  /* pause from the second pad, and on again from it */
  await tap('start', 1); const paused = await state('menu.page'); await tap('start', 1); const resumed = await state('menu.page');
  check(`${backend}: the second pad pauses and resumes`, paused['menu.page'] === 5 && resumed['menu.page'] === 0, `page ${paused['menu.page']} then ${resumed['menu.page']}`);
  /* the second seat never touched again: the first player finishes, the race closes within 30 s, the podium and results by the first pad */
  await cmd('finish');
  let s = await state('race.state', 'menu.page');
  for (let k = 0; k < 40 && s['race.state'] !== DONE; k++) { await hold(60, { south: true }, {}); s = await state('race.state', 'menu.page'); }
  await tool('frame', { op: 'step', frames: 6 }); /* the menu opens its page the tick after the race closes */
  const after = await state('race.state', 'menu.page', 'race.fin_t', 'p1.finish', 'podium.on');
  check(`${backend}: with the second seat idle the race still closes, on the podium`, after['race.state'] === DONE && after['menu.page'] === P_PODIUM && after['race.fin_t'] <= 31 && after['p1.finish'] > 0, `state ${after['race.state']}, page ${after['menu.page']}, ${f1(after['race.fin_t'])} s after the line, P2's time ${f1(after['p1.finish'])}`);
  await hold(60, {}, {}); /* both pads let go (the throttle was held) */
  await tap('south'); await tool('frame', { op: 'step', frames: 10 });
  const res = await state('menu.page');
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-results.png`) });
  await tap('south'); await tool('frame', { op: 'step', frames: 10 });
  const fc = await state('menu.page');
  check(`${backend}: the first pad goes on: results, the next day's forecast`, res['menu.page'] === P_RESULTS && fc['menu.page'] === P_FORECAST, `pages ${res['menu.page']}, ${fc['menu.page']}`);
  /* control: no takeover, the idle seat sits on the grid */
  await tap('east'); await tool('frame', { op: 'step', frames: 10 });
  await cmd('sw p2_idle_cpu 0'); await cmd('race');
  await hold(600, { south: true }, {});
  const ctl = await state('p1.speed', 'race.seat_cpu');
  await cmd('sw p2_idle_cpu 1');
  check(`${backend}: control, sw p2_idle_cpu 0 leaves the idle seat on the grid`, ctl['p1.speed'] < 2 && ctl['race.seat_cpu'][1] === 0, `${f1(ctl['p1.speed'])} m/s, computer ${ctl['race.seat_cpu'][1]}`);
  /* control: one player draws one view */
  await cmd('sw players 1'); await cmd('race'); await tool('frame', { op: 'step', frames: 30 });
  const one = await state('view.views');
  check(`${backend}: control, one player draws one view`, one['view.views'] <= 1, `views ${one['view.views']}`);
  await tool('host', { op: 'shutdown' });
}
await bye(fail ? 1 : 0);
