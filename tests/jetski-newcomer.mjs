// A newcomer's first race, on the cart through romdev, GL and WebGPU, by the pad and the screen alone: from power-on A
// starts the race, the throttle (A) goes down for the green light, and the rider steers by the d-pad past the next mark
// on the side its chevron points to (the board and chevron over it; behind the camera, a hard turn). After
// 45 s of racing the newcomer has passed the course's marks in order and kept up a race speed.
// Control, which must fail: the same throttle with the d-pad never touched (a player who does not read the marks).
//   node tests/jetski-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/jetski-newcomer');
const SESSION = process.env.JETSKI_SESSION || 'jetski-newcomer', AGENT = process.env.JETSKI_AGENT || 'jetski-newcomer';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT };
// every exit tears this session's romdev host down, a failed run too
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pad = (p) => tool('input', { op: 'set', ports: [p] });
const W = 1920, RACE_ON = 2, SECONDS = 45, EVERY = 6;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, steer) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(180);
  const page0 = +(await rd('menu.page'));
  await pad({ south: true }); await step(4); await pad({}); await step(20);            /* the menu's first row: race */
  for (let f = 0; f < 400 && +(await rd('race.state')) !== RACE_ON; f += 10) await step(10); /* the countdown, hands off */
  const state = +(await rd('race.state'));
  let topSpeed = 0, frames = 0;
  while (frames < SECONDS * 60) {
    const b = await rd('hud.buoy'), front = +b[2] === 1, way = +(await rd('hud.chevron'));
    /* aim beside the mark on the chevron's side, by as much as the mark stands tall on screen (wider as it comes close) */
    const x = +b[0] + way * Math.min(W * 0.3, 1.3 * Math.max(0, +b[3] - +b[1]));
    const p = { south: true };
    if (steer) {
      if (!front) p.right = true;                 /* the mark is behind: turn round */
      else if (x < W * 0.46) p.left = true;
      else if (x > W * 0.54) p.right = true;
    }
    await pad(p); await step(EVERY); frames += EVERY;
    if (frames % 60 === 0) topSpeed = Math.max(topSpeed, +(await rd('p0.speed')));
  }
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${steer ? 'newcomer' : 'control'}.png`) });
  await pad({});
  const r = { page0, state, passes: +(await rd('p0.passes')), misses: +(await rd('p0.misses')), spills: +(await rd('p0.spills')), top: topSpeed };
  await tool('host', { op: 'shutdown' });
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const n = await run(backend, true);
  check('power-on opens the menu, A starts the race', n.page0 === 1 && n.state === RACE_ON, `menu page ${n.page0}, race state ${n.state} after the countdown`);
  check('steering by the marks on screen, the newcomer races the course', n.passes >= 6 && n.misses <= 1 && n.top > 15,
    `${n.passes} marks passed, ${n.misses} missed, ${n.spills} spills, top ${n.top.toFixed(1)} m/s in ${SECONDS} s`);
  const c = await run(backend, false);
  check('control: the d-pad never touched, the marks are not raced', c.passes < n.passes / 2 || c.misses >= 2,
    `${c.passes} marks passed, ${c.misses} missed`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
