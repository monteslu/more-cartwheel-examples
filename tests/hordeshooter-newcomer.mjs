// hordeshooter, a newcomer's first floor, on the cart through romdev, GL and WebGPU, by the pad and the screen alone. From
// power-on the title's Play, the join screen's join and Start; then the Yard played from what the picture shows (nc.look,
// set from what is drawn, in screen positions): the enemies and the boss inside the picture, the gems and power-ups lying
// in it, the upgrade cards when they come up, the STEP IN marker once the way down is open. No map, no route, no wave list.
// The player is a person, not a perfect bot: it acts on what the screen showed 0.3 s before (reaction delay), aims with a
// wandering error of up to 12 degrees and a jitter on top (aim error), steers with a jitter, dashes away from a close
// enemy only some of the times it could, reads the cards for a second before picking one at random, and turns away when
// it walks into a wall. A pause and a resume on the way.
//   alone: the boss falls, the way down opens and the player steps in: floor 2 inside 15 minutes.
//   idle seat: a second pad joins and never touches anything again; the floor is still finished, the horde is sized for
//   the one person playing (game.crew 1) and the yard slows while that person's cards are up (game.slow).
// Controls, which must fail: the same person with no STEP IN marker over the open hatch (sw no_marker 1) does not find the
// way down in 90 s; under the old rules for an idle seat (sw idle_crew 1) the horde is sized for it too (game.crew 2),
// the yard never slows for the cards, and the idle soldier, standing where it stood, holds the team box and its
// tether: the person cannot reach the open hatch in 2 minutes (the shipped soft lock this gate found).
//   node tests/hordeshooter-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--no-control | --control-only] [--no-idle]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/hordeshooter-newcomer');
const SESSION = process.env.HORDE_SESSION || `hordeshooter-newcomer-${process.pid}`;
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/hordeshooter.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const MAX = 60 * Number(arg('--max-secs', '900'));
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': 'hordeshooter-newcomer' };
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(`${ROMDEV_URL}/tool/host`, { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
for (const ev of ['unhandledRejection', 'uncaughtException']) process.on(ev, (e) => { console.error(e); bye(3); }); /* a script error still tears the host down */
let requests = 0; /* romdev requests this run (one decision is one input request and one read) */
const tool = async (name, args) => {
  requests++;
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const one = async (n) => +(await rd(n));
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0 = {}, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
const press = (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; return tool('input', { op: 'sequence', steps: [{ input: { ports: a }, frames: 4 }, { input: { ports: [{}, {}] }, frames: 8 }] }); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const ST_PLAY = 0, ST_OVER = 1, ST_CLEAR = 3, ST_HATCH = 4, ST_DESCEND = 5;
const A = 16 / 9, NE = 16, NP = 4, E0 = 18, P0 = E0 + 2 * NE; /* nc.look layout (games/hordeshooter/main.c) */
const EVERY = 9, DELAY = 2; /* a decision every 9 frames (150 ms), on the look 2 back (0.3 s) */
/* one decision is one request: the input held for the frames until the next decision (a button tapped for its first 2
   frames, then let go while the sticks stay where they are), stepped in the same call */
const BUTTONS = ['north', 'south', 'east', 'west', 'l', 'r', 'start', 'select'];
const act = (p0, frames) => {
  const tap = BUTTONS.some((b) => p0[b]);
  const rest = Object.fromEntries(Object.entries(p0).filter(([k]) => !BUTTONS.includes(k)));
  return tool('input', { op: 'sequence', steps: tap ? [{ input: { ports: [p0, {}] }, frames: 2 }, { input: { ports: [rest, {}] }, frames: frames - 2 }] : [{ input: { ports: [p0, {}] }, frames }] });
};


async function run(backend, tag, { idle = false, cmds = [], hatchSecs = 0, limit = MAX } = {}) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  for (const c of cmds) await tool('wasm', { op: 'command', text: c });
  await press(0, 'south'); await step(20);  /* the title: Play */
  await press(0, 'south');                  /* the join screen: P1 takes a seat */
  if (idle) await press(1, 'south');        /* a second pad takes one, and is never touched again */
  await press(0, 'start'); await step(30);  /* play */
  const r = { seats: await one('shell.players'), screen0: await one('shell.screen'), paused: 0, resumed: 0, overruns: 0, picks: 0, dashes: 0, blasts: 0, stuck: 0,
    crewMax: 0, crewLate: 0, slowSeen: 0, cardsSeen: 0, hatchAt: -1, floor: 0 };
  let seed = 0x5eed3, f = 0;
  const rand = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
  const seen = [], trace = [];
  let aimErr = 0, moveErr = 0, cardWait = 0, unstick = 0, ux = 0, uy = 0, lastState = ST_PLAY, pauseDone = false, wx = 0, wy = 0, still = 0, wander = rand() * 6.28;
  let nextTrace = 600, shot60 = false, nextSample = 0, hatchShot = false; /* (decisions are 9 frames apart: these go by thresholds) */
  while (f < limit) {
    const look = (await rd('nc.look')).map(Number);
    seen.push(look);
    const o = seen.length > DELAY ? seen.shift() : seen[0];
    const state = look[0];
    r.floor = look[1];
    if (look[1] >= 1) break; /* floor 2: the first floor is done */
    if (state === ST_OVER && lastState !== ST_OVER) r.overruns++;
    if (state === ST_HATCH && r.hatchAt < 0) r.hatchAt = f;
    lastState = state;
    if (hatchSecs && r.hatchAt >= 0 && f - r.hatchAt > 60 * hatchSecs) break;
    if (!pauseDone && f > 60 * 40 && state === ST_PLAY) { /* a pause and back: Start, the pause menu, back out of it */
      await press(0, 'start'); await step(20);
      r.paused = await one('game.ticks');
      r.pauseScreen = await one('shell.screen');
      await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-pause.png`) });
      await press(0, 'east'); await step(30);
      r.resumed = (await one('game.ticks')) > r.paused ? 1 : 0;
      pauseDone = true;
    }
    if (f >= nextSample) {
      nextSample += 60;
      const crew = await one('game.crew');
      r.crewMax = Math.max(r.crewMax, crew);
      if (f > 60 * 30) r.crewLate = Math.max(r.crewLate, crew);
      if (look[7]) { r.cardsSeen++; if (await one('game.slow')) r.slowSeen++; }
    }
    const mu = o[3], mv = o[4], hp = o[5], down = o[6] > 0;
    const p0 = {};
    aimErr = clamp(aimErr + (rand() - 0.5) * 0.05, -0.21, 0.21); /* up to 12 degrees, wandering */
    moveErr = clamp(moveErr + (rand() - 0.5) * 0.12, -0.35, 0.35);
    if (!down) {
      /* move: away from the enemies close round, toward a gem or power-up when the way is clear, to the marker once it shows */
      let mx = 0, my = 0, nearest = 1e9, nu = 0, nv = 0, close = 0;
      for (let k = 0; k < NE; k++) {
        const eu = o[E0 + 2 * k], ev = o[E0 + 2 * k + 1];
        if (eu < 0) continue;
        const dx = (mu - eu) * A, dy = mv - ev, d = Math.hypot(dx, dy) + 1e-4;
        if (d < nearest) nearest = d, nu = eu, nv = ev;
        if (d < 0.12) close++;
        const R = 0.24;
        if (d < R) mx += (dx / d) * (R - d) / R, my += (dy / d) * (R - d) / R;
      }
      if (o[12] >= 0) { const dx = (mu - o[12]) * A, dy = mv - o[13], d = Math.hypot(dx, dy) + 1e-4; if (d < 0.32) mx += (dx / d) * 1.5 * (0.32 - d) / 0.32, my += (dy / d) * 1.5 * (0.32 - d) / 0.32; }
      const gu = o[P0], gv = o[P0 + 1];
      if (gu >= 0 && nearest > 0.1) { const dx = (gu - mu) * A, dy = gv - mv, d = Math.hypot(dx, dy) + 1e-4; if (d < 0.3) mx += (dx / d) * 0.5, my += (dy / d) * 0.5; }
      if (o[8]) { const dx = (o[9] - mu) * A, dy = o[10] - mv, d = Math.hypot(dx, dy) + 1e-4; mx += (dx / d) * 1.5, my += (dy / d) * 1.5; } /* STEP IN */
      if (Math.hypot(mx, my) < 0.15) { wander += (rand() - 0.5) * 0.4; mx += Math.cos(wander) * 0.4, my += Math.sin(wander) * 0.4; } /* nothing pressing: drift round */
      /* walked into a wall or a crate (the ground stops going by): turn away for a second */
      const moved = Math.hypot(o[15] - wx, o[16] - wy);
      wx = o[15], wy = o[16];
      if (unstick > 0) { unstick--; mx = ux, my = uy; }
      else if (Math.hypot(mx, my) > 0.3 && moved < 0.05) { if (++still > 4) { still = 0; const s = rand() < 0.5 ? 1 : -1; ux = -my * s + (rand() - 0.5) * 0.5, uy = mx * s + (rand() - 0.5) * 0.5; unstick = 10; r.stuck++; } }
      else still = 0;
      const ml = Math.hypot(mx, my);
      if (ml > 0.05) { const a = Math.atan2(my, mx) + moveErr + (rand() - 0.5) * 0.3; p0.axes = { lx: Math.cos(a), ly: Math.sin(a) }; }
      /* aim: the nearest enemy, or the boss when nothing is close; aiming fires */
      let tu = -1, tv = -1;
      if (nearest < 1e8) tu = nu, tv = nv;
      if (o[12] >= 0 && nearest > 0.15) tu = o[12], tv = o[13];
      if (tu >= 0) {
        const a = Math.atan2(tv - mv, (tu - mu) * A) + aimErr + (rand() - 0.5) * 0.17;
        p0.axes = { ...(p0.axes || {}), rx: Math.cos(a), ry: Math.sin(a) };
      }
      if (nearest < 0.05 && o[14] && rand() < 0.25 && ml > 0.05) { p0.r = true; r.dashes++; } /* a close one: dash away, some of the time */
      if (close >= 8 && rand() < 0.15) { p0.l = true; r.blasts++; }                           /* a thick crowd: a blast */
      if (o[7]) { /* the cards: read them a moment, then pick one */
        if (++cardWait > 7 + Math.floor(rand() * 4)) { p0[['west', 'north', 'east'][Math.floor(rand() * 3)]] = true; cardWait = 0; r.picks++; }
      } else cardWait = 0;
    }
    await act(p0, EVERY); f += EVERY;
    if (f >= nextTrace) nextTrace += 600, trace.push({ s: f / 60, state, floor: look[1], wave: look[2], hp: +hp.toFixed(0), onscreen: look[17], kills: await one('game.kills'), crew: await one('game.crew') });
    if (f >= 60 * 60 && !shot60) shot60 = true, await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-t60.png`) });
    if (state === ST_HATCH && r.hatchAt >= 0 && f - r.hatchAt >= 120 && !hatchShot) hatchShot = true, await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-hatch.png`) });
  }
  await pads();
  await step(90);
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-end.png`) });
  Object.assign(r, { secs: f / 60, state: await one('game.state'), floor: await one('floor.level'), kills: await one('game.kills'), level: [...(await rd('player.level'))].map(Number)[0] });
  writeFileSync(join(OUT, `${backend}-${tag}.json`), JSON.stringify({ r, trace }, null, 1));
  console.log(`  ${backend} ${tag}: floor ${r.floor + 1} after ${r.secs.toFixed(0)} s (hatch open at ${r.hatchAt < 0 ? '-' : (r.hatchAt / 60).toFixed(0) + ' s'}), ${r.kills} kills, level ${r.level}, overrun ${r.overruns} times, ${r.picks} cards picked, ${r.dashes} dashes, ${r.blasts} blasts, turned off a wall ${r.stuck} times`);
  r.requests = requests; requests = 0;
  console.log(`  ${backend} ${tag}: ${r.requests} romdev requests for ${f} frames`);
  await tool('host', { op: 'shutdown' });
  return r;
}

const ONLY = arg('--only', ''); /* alone | idle | control | crew (the idle_crew control alone): one leg (each is long) */
const CTL_ONLY = process.argv.includes('--control-only') || ONLY === 'control' || ONLY === 'crew', NO_CTL = process.argv.includes('--no-control') || ONLY === 'alone' || ONLY === 'idle', NO_IDLE = process.argv.includes('--no-idle') || ONLY === 'alone';
const NO_ALONE = ONLY === 'idle';
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (!CTL_ONLY) {
    if (!NO_ALONE) {
      const a = await run(backend, 'alone');
      check('power-on to the yard: Play, join, Start', a.seats === 1 && a.screen0 === 3, `${a.seats} seat, screen ${a.screen0}`);
      check('pause and resume', a.paused > 0 && a.pauseScreen !== 3 && a.resumed === 1, `paused at tick ${a.paused} (screen ${a.pauseScreen}), resumed ${a.resumed}`);
      check('a newcomer with human error finishes the Yard and steps down to floor 2', a.floor >= 1, `floor ${a.floor + 1} after ${a.secs.toFixed(0)} s, overrun ${a.overruns} times`);
      check('alone: the yard slows while the cards are up', a.cardsSeen > 0 && a.slowSeen > 0, `slow on ${a.slowSeen} of ${a.cardsSeen} card looks`);
    }
    if (!NO_IDLE) {
      const b = await run(backend, 'idle-seat', { idle: true });
      check('idle seat: the same newcomer still finishes the Yard (nothing holds the way down)', b.seats === 2 && b.floor >= 1, `${b.seats} seats, floor ${b.floor + 1} after ${b.secs.toFixed(0)} s, overrun ${b.overruns} times`);
      check('idle seat: the horde is sized for the one person playing', b.crewLate === 1, `game.crew ${b.crewLate} after 30 s (2 at most before: ${b.crewMax})`);
      check('idle seat: the yard slows while that person\'s cards are up', b.cardsSeen > 0 && b.slowSeen > 0, `slow on ${b.slowSeen} of ${b.cardsSeen} card looks`);
    }
  }
  if (!NO_CTL) {
    const c = ONLY === 'crew' ? null : await run(backend, 'control-no-marker', { cmds: ['sw no_marker 1'], hatchSecs: 90 });
    if (c) check('control: sw no_marker 1, the way down is open but not found in 90 s', c.hatchAt >= 0 && c.floor === 0, `hatch open at ${c.hatchAt < 0 ? '-' : (c.hatchAt / 60).toFixed(0) + ' s'}, floor ${c.floor + 1} at ${c.secs.toFixed(0)} s`);
    if (!NO_IDLE) {
      const d = await run(backend, 'control-idle-crew', { idle: true, cmds: ['sw idle_crew 1'], hatchSecs: 120 });
      check('control: sw idle_crew 1, the horde is sized for the idle seat too and never slows', d.crewLate === 2 && d.slowSeen === 0, `game.crew ${d.crewLate}, slow on ${d.slowSeen} of ${d.cardsSeen} card looks`);
      check('control: sw idle_crew 1, the idle soldier holds the person back from the open hatch', d.hatchAt >= 0 && d.floor === 0, `hatch open at ${d.hatchAt < 0 ? '-' : (d.hatchAt / 60).toFixed(0) + ' s'}, floor ${d.floor + 1} at ${d.secs.toFixed(0)} s`);
    }
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
