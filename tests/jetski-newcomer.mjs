// A newcomer's first race, on the cart through romdev, GL and WebGPU, by the pad and the screen alone: from power-on A
// starts the race, the throttle (A) goes down for the green light, and the rider steers by the d-pad past the next mark
// on the side its chevron points to (the board and chevron over it; behind the camera, a hard turn). After
// 45 s of racing the newcomer has passed the course's marks in order and kept up a race speed. The newcomer is a
// person, not a perfect bot: it acts on what the screen showed 0.3 s before (reaction delay), its
// idea of where to aim wanders by up to 7 % of the screen's width and jitters every look (aim error), and it holds a
// turn a beat too long now and then (over-steer).
// Control, which must fail: the same throttle with the d-pad never touched (a player who does not read the marks).
// --craft stand picks the stand-up on the title menu first (Craft row, by the pad), --craft both races each; --finish
// races the whole of the first course, three laps, and asks that it is finished inside 8 minutes on the sit-down and 10 on
// the stand-up, and with both, the stand-up inside 25 % of the sit-down's time (the control runs until its race is over too).
// --weather 0|1|2 races in a calm, a chop or a storm.
//   node tests/jetski-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--craft sit|stand|both] [--finish] [--weather n]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/jetski-newcomer');
const SESSION = process.env.JETSKI_SESSION || 'jetski-newcomer', AGENT = process.env.JETSKI_AGENT || 'jetski-newcomer';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const CRAFTS = { sit: [false], stand: [true], both: [false, true] }[arg('--craft', 'sit')], FINISH = process.argv.includes('--finish'), WEATHER = arg('--weather', null);
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT };
// every exit tears this session's romdev host down, a failed run too
const bye = async (code) => {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
let requests = 0; /* romdev requests, printed at the end */
const tool = async (name, args) => {
  requests++;
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pad = (p) => tool('input', { op: 'set', ports: [p] });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p] }, frames }] });
/* --finish: the first course within 8 minutes on the sit-down; the stand-up is slower on the straights, so 25 % more */
const W = 1920, RACE_ON = 2, RACE_DONE = 3, LIMIT = (stand) => +arg('--seconds', 0) || (FINISH ? (stand ? 600 : 480) : 45), EVERY = 9; /* a decision every 9 frames (150 ms) */
/* one decision is one read (nc.look: the next mark on screen, its chevron, the speed) and one request that holds the pad
   for the frames to the next decision and steps them */
const act = (p, frames) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p] }, frames }] });
let fail = 0;
const times = {};
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, steer, STAND) {
  const SECONDS = LIMIT(STAND);
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(180);
  for (let k = 0; k < 120 && +(await rd('cw.loading')) !== 0; k++) await step(30); /* the loading screen, on a slow host */
  const page0 = +(await rd('menu.page'));
  /* --weather n: the day's weather, the one setup request (the course card puts it on the screen); the rest is the pad */
  if (WEATHER !== null) await tool('wasm', { op: 'command', text: `weather ${WEATHER}` });
  /* the stand-up: down to the Craft row, right to change it, back up to Start, all by the pad as the menu shows */
  if (STAND) for (const b of ['down', 'down', 'down', 'down', 'down', 'right', 'up', 'up', 'up', 'up', 'up']) { await hold(4, { [b]: true }); await hold(6, {}); }
  const craft = +(await rd('race.craft'));
  await hold(4, { south: true }); await hold(20, {});            /* the menu's first row: start, which shows the week's forecast */
  await hold(4, { south: true }); await hold(20, {});            /* its first row: the first course, today */
  for (let f = 0; f < 400 && +(await rd('race.state')) !== RACE_ON; f += 10) await step(10); /* the countdown, hands off */
  const state = +(await rd('race.state'));
  let topSpeed = 0, frames = 0, seed = 0x1234567, drift = 0, over = 0, held = null;
  const rand = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296);
  const seen = []; /* what the screen showed, oldest first */
  while (frames < SECONDS * 60) {
    if (FINISH && frames % 600 === 0 && +(await rd('race.state')) === RACE_DONE) break; /* over the line, or out */
    const look = (await rd('nc.look')).map(Number), b = look.slice(0, 4), way = look[4];
    topSpeed = Math.max(topSpeed, look[5]);
    seen.push({ b, way });
    const { b: ob, way: oway } = seen.length > 2 ? seen.shift() : seen[0]; /* the look 2 decisions (18 frames, 0.3 s) old */
    const front = +ob[2] === 1;
    /* aim beside the mark on the chevron's side, by as much as the mark stands tall on screen (wider as it comes close),
       off by a wandering error and a jitter */
    drift = Math.max(-0.07, Math.min(0.07, drift + (rand() - 0.5) * 0.02));
    const x = +ob[0] + oway * Math.min(W * 0.3, 1.3 * Math.max(0, +ob[3] - +ob[1])) + W * (drift + (rand() - 0.5) * 0.03);
    const p = { south: true };
    if (steer) {
      if (over > 0 && held) { Object.assign(p, held); over--; } /* a turn held a beat too long */
      else if (!front) p.right = true;                 /* the mark is behind: turn round */
      else if (x < W * 0.46) p.left = true;
      else if (x > W * 0.54) p.right = true;
      if ((p.left || p.right) && over === 0 && rand() < 0.08) { held = { left: p.left, right: p.right }; over = 2; }
    }
    await act(p, EVERY); frames += EVERY;
  }
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${STAND ? 'stand-' : ''}${steer ? 'newcomer' : 'control'}.png`) });
  await pad({});
  const r = { page0, state, craft, passes: +(await rd('p0.passes')), misses: +(await rd('p0.misses')), spills: +(await rd('p0.spills')), top: topSpeed, finish: +(await rd('p0.finish')), dq: +(await rd('p0.dq')), place: +(await rd('p0.place')), frames, ride: (await rd('p0.ride')).map(Number), seconds: SECONDS, dist: +(await rd('p0.dist')) };
  await tool('host', { op: 'shutdown' });
  return r;
}

for (const backend of BACKENDS) for (const STAND of CRAFTS) {
  console.log(`--- ${backend}, ${STAND ? 'stand-up' : 'sit-down'}`);
  const n = await run(backend, true, STAND);
  const rd2 = n.ride;
  console.log(`  the race (${n.dist.toFixed(0)} m in ${n.seconds} s): ${n.finish > 0 ? `finished in ${n.finish.toFixed(1)} s` : 'not finished'}, ${n.spills} falls, ${rd2[0].toFixed(1)} s off the plane, ${rd2[1].toFixed(1)} s sliding, 10 m/s ${rd2[2].toFixed(1)} s after the green light, ${n.misses} wrong-side marks`);
  times[backend + (STAND ? '-stand' : '-sit')] = n.finish;
  check('power-on opens the menu, A starts the race', n.page0 === 1 && n.state === RACE_ON, `menu page ${n.page0}, race state ${n.state} after the countdown`);
  check('the craft the menu shows is the one raced', n.craft === (STAND ? 1 : 0), `craft ${n.craft}`);
  if (FINISH) check('the newcomer finishes the first course, three laps', n.finish > 0 && !n.dq, `finished in ${n.finish.toFixed(1)} s, place ${n.place}, ${n.passes} marks passed, ${n.misses} missed, ${n.spills} spills`);
  /* (a whole race may miss a few marks, as a person does: five end it, and finishing is checked above) */
  check('steering by the marks on screen, the newcomer races the course', n.passes >= 6 && n.misses <= (FINISH ? 4 : 1) && n.top > 15,
    `${n.passes} marks passed, ${n.misses} missed, ${n.spills} spills, top ${n.top.toFixed(1)} m/s in ${n.seconds} s`);
  const c = await run(backend, false, STAND);
  check('control: the d-pad never touched, the marks are not raced', (c.passes < n.passes / 2 || c.misses >= 2) && (!FINISH || !(c.finish > 0 && !c.dq)),
    `${c.passes} marks passed, ${c.misses} missed${FINISH ? `, finish ${c.finish.toFixed(1)}, out ${c.dq}` : ''}`);
}
for (const backend of BACKENDS) { const a = times[backend + '-sit'], b = times[backend + '-stand'];
  if (FINISH && a > 0 && b > 0) check('the stand-up newcomer within 25 % of the sit-down one', b <= a * 1.25, `${b.toFixed(1)} s against ${a.toFixed(1)} s (${((b / a - 1) * 100).toFixed(0)} %)`); }
console.log(`romdev requests: ${requests}`);
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
