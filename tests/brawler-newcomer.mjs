// Street Toughs, a newcomer's first stage, on the cart through romdev, GL and WebGPU, by the pad and the screen alone. From
// power-on the title's Play, the join screen's join and Start, the select's "attack: ready"; then stage 1 (Back Alley)
// played from what the picture shows: the enemies standing inside it, the fighter's own place, the GO arrow when a wave is
// done, food lying in the street (nc.look, set from what is drawn). No route, no wave list, no enemy that is not on screen.
// The player is a person, not a perfect bot: it acts on what the screen showed 0.3 s before (reaction delay), judges an
// enemy's depth in the street wrong by up to 0.35 m and its distance by up to 0.25 m (the error wanders, as a person's
// read of a picture does), mashes attack at a person's pace with a kick now and then, turns late, and stops to look round
// now and then. A pause and a resume on the way.
//   alone: STAGE CLEAR (the Foreman beaten) inside 10 minutes of play, with a life still in hand (a newcomer's first
//   stage is not won on the last life).
//   idle seat: a second pad joins and never touches anything again; the select counts it out and the stage still clears.
// Controls, which must fail: the same person on the first stage as it shipped before (sw old_stage1 1: its crowd hitting
// as hard as the later stages' and its bruiser at full health) wins it only on the last life, if at all; in a harder
// game (sw dmg 300: every blow on a fighter does three times its damage) it runs out of lives.
//   node tests/brawler-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--no-control | --control-only] [--no-idle]
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/brawler-newcomer');
const SESSION = process.env.BRAWLER_SESSION || `brawler-newcomer-${process.pid}`, PORT = process.env.ROMDEV_PORT || '7331';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/brawler.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const MAX = 60 * Number(arg('--max-secs', '600'));
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': 'brawler-newcomer' };
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(`http://127.0.0.1:${PORT}/tool/host`, { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
for (const ev of ['unhandledRejection', 'uncaughtException']) process.on(ev, (e) => { console.error(e); bye(3); }); /* a script error still tears the host down */
let requests = 0; /* romdev requests this run (one decision is one input request and one read) */
const tool = async (name, args) => {
  requests++;
  const r = await fetch(`http://127.0.0.1:${PORT}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
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
const PLAY = 0, CLEAR = 1, OVER = 2, WIN = 3; /* game.phase */
const LIVES = 3; /* lives a fighter starts with besides the one in play */
const won = (r) => r.phase === CLEAR && r.lost < LIVES; /* the stage cleared with a life still in hand */
const S_DOWN = 9, S_FALL = 8, S_DEAD = 11, S_GETUP = 10; /* bstate: enemies lying or going down are not a target */
const EVERY = 9, DELAY = Number(arg('--delay', '2')); /* a decision every 9 frames (150 ms), on the look 2 back (0.3 s) */
/* one decision is one request: the input held for the frames until the next decision (a button tapped for its first 2
   frames, then let go while the sticks stay where they are), stepped in the same call */
const BUTTONS = ['north', 'south', 'east', 'west', 'l', 'r', 'start', 'select'];
const act = (p0, frames) => {
  const tap = BUTTONS.some((b) => p0[b]);
  const rest = Object.fromEntries(Object.entries(p0).filter(([k]) => !BUTTONS.includes(k)));
  return tool('input', { op: 'sequence', steps: tap ? [{ input: { ports: [p0, {}] }, frames: 2 }, { input: { ports: [rest, {}] }, frames: frames - 2 }] : [{ input: { ports: [p0, {}] }, frames }] });
};

const ERR = Number(arg('--err', '1')), REST = !process.argv.includes('--no-rest'); /* diagnostics only: scale the misread, never rest from mashing */

async function run(backend, tag0, { idle = false, cmds = [] } = {}) {
  const tag = tag0 + arg('--tag', ''); /* (diagnostic runs keep their own frames) */
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  for (const c of cmds) await tool('wasm', { op: 'command', text: c });
  await press(0, 'south'); await step(20);  /* the title: Play */
  await press(0, 'south');                  /* the join screen: P1 takes a seat */
  if (idle) await press(1, 'south');        /* a second pad takes one, and is never touched again */
  await press(0, 'start'); await step(30);  /* play: the character select */
  const r = { seats: await one('shell.players'), select: await one('pick.on'), paused: 0, resumed: 0, lost: 0, presses: 0, picked: 0 };
  await press(0, 'west');                   /* attack: ready (the card's prompt) */
  let seed = 0x5eed7, f = 0;
  const rand = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
  for (let w = 0; w < 60 * 20 && +(await rd('pick.on')) === 1; w += 30) await step(30); /* the select counts an idle seat out */
  r.selectLeft = await one('pick.on');
  const seen = [], trace = [];
  let errZ = 0, errX = 0, hold = 0, mash = 0, rest = 0, lastLives = -1, noFoe = 0, pauseDone = false, phase = PLAY, turnLate = 0;
  let nextTrace = 600, shot60 = false, nextSample = 0, hatchShot = false; /* (decisions are 9 frames apart: these go by thresholds) */
  while (f < MAX) {
    const look = (await rd('nc.look')).map(Number);
    seen.push(look);
    const o = seen.length > DELAY ? seen.shift() : seen[0];
    phase = look[0];
    if (phase === CLEAR || phase === OVER || phase === WIN) break;
    if (lastLives >= 0 && look[5] < lastLives) r.lost += lastLives - look[5];
    lastLives = look[5];
    if (!pauseDone && f > 60 * 40 && phase === PLAY) { /* a pause and back: Start, the pause menu, back out of it */
      await press(0, 'start'); await step(20);
      r.paused = await one('game.ticks');
      r.pauseScreen = await one('shell.screen');
      await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-pause.png`) });
      await press(0, 'east'); await step(30);
      r.resumed = (await one('game.ticks')) > r.paused ? 1 : 0;
      pauseDone = true;
    }
    const [, , px, pz, hp, , pst, face, go] = o;
    /* the error of a person's read of the picture: it wanders, a little each look */
    errZ = clamp(errZ + (rand() - 0.5) * 0.12 * ERR, -0.35 * ERR, 0.35 * ERR);
    errX = clamp(errX + (rand() - 0.5) * 0.1 * ERR, -0.25 * ERR, 0.25 * ERR);
    let tgt = null, bd = 1e9;
    for (let k = 0; k < 8; k++) {
      const ex = o[11 + k * 3], ez = o[12 + k * 3], st = o[13 + k * 3];
      if (st < 0 || st === S_DOWN || st === S_FALL || st === S_DEAD || st === S_GETUP) continue;
      const d = (ex - px) ** 2 + 3 * (ez - pz) ** 2;
      if (d < bd) bd = d, tgt = { x: ex + errX, z: ez + errZ };
    }
    let food = null;
    for (let k = 0; k < 3; k++) if (o[37 + k * 3] === 0) food = { x: o[35 + k * 3], z: o[36 + k * 3] }; /* an apple: food */
    const p0 = {};
    let lx = 0, ly = 0;
    if (hold > 0) hold--; /* a look round: hands off for a moment */
    else if (rand() < 0.025) hold = 4 + Math.floor(rand() * 4);
    else if (food && hp < 55) { /* hurt, and food on the street: go and get it */
      const dx = food.x - px, dz = food.z - pz;
      lx = Math.abs(dx) > 0.3 ? Math.sign(dx) : 0; ly = Math.abs(dz) > 0.25 ? Math.sign(dz) : 0;
    } else if (tgt) {
      noFoe = 0;
      const side = px < tgt.x ? -1 : 1, dx = tgt.x + side * 0.8 - px, dz = tgt.z - pz;
      lx = Math.abs(dx) > 0.3 ? Math.sign(dx) : 0; ly = Math.abs(dz) > 0.2 ? Math.sign(dz) : 0;
      const fx = (tgt.x - px) * face;
      if (fx < -0.1 && Math.abs(tgt.x - px) < 2) { /* turned away from it: turn round, a beat late */
        if (turnLate++ > 1) lx = Math.sign(tgt.x - px);
      } else turnLate = 0;
      /* a newcomer mashes as soon as an enemy is close in front, near enough in the street's depth (the blow's lane
         assist does the rest, or it whiffs) */
      const inReach = fx > -0.3 && fx < 1.8 && Math.abs(tgt.z - pz) < 0.7;
      if (inReach) {
        if (rest > 0) rest--;
        else { /* a tap a decision at most: a person's mashing pace */
          const r2 = rand();
          if (r2 < 0.14) p0.north = true; else if (r2 < 0.18) p0.south = true; else p0.west = true;
          if (REST && ++mash >= 4 + Math.floor(rand() * 3)) mash = 0, rest = 1 + Math.floor(rand() * 2); /* a combo, a breath */
        }
        if (Math.abs(dx) < 0.5) lx = 0;
      }
    } else { /* nobody to fight on screen: the GO arrow (or a quiet street) says walk on */
      noFoe++;
      if (go || noFoe > 8) { lx = 1; ly = Math.abs(pz) > 0.4 ? -Math.sign(pz) : 0; }
    }
    if (pst === S_DOWN || pst === S_FALL) lx = ly = 0;
    if (lx || ly) p0.axes = { lx, ly };
    if (p0.west || p0.north || p0.south) r.presses++;
    await act(p0, EVERY); f += EVERY;
    if (f >= nextTrace) nextTrace += 600, trace.push({ s: f / 60, phase, wave: await one('game.wave'), kills: await one('game.kills'), hp: +hp.toFixed(1), lives: look[5], cam: +look[9].toFixed(1), hits: await one('game.hits'), boss: await one('game.boss_phase') });
    if (f >= 60 * 60 && !shot60) shot60 = true, await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-t60.png`) });
  }
  await pads();
  r.phase = await one('game.phase');
  if (r.phase === CLEAR) await step(60);
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-end.png`) });
  Object.assign(r, { secs: f / 60, kills: await one('game.kills'), hits: await one('game.hits'), wave: await one('game.wave'), boss: await one('game.boss_phase'),
    absent: [...(await rd('seat.absent'))].map(Number), crew: await one('game.crew'), unfair: await one('game.unfair') });
  writeFileSync(join(OUT, `${backend}-${tag}.json`), JSON.stringify({ r, trace }, null, 1));
  console.log(`  ${backend} ${tag}: phase ${r.phase} after ${r.secs.toFixed(0)} s, ${r.kills} kills, ${r.hits} hits landed of ${r.presses} presses, lives lost ${r.lost}, boss phase ${r.boss}`);
  r.requests = requests; requests = 0;
  console.log(`  ${backend} ${tag}: ${r.requests} romdev requests for ${f} frames`);
  await tool('host', { op: 'shutdown' });
  return r;
}

const ONLY = arg('--only', ''); /* alone | idle | control: one leg (each is long) */
const CTL_ONLY = process.argv.includes('--control-only') || ONLY === 'control', NO_CTL = process.argv.includes('--no-control') || ONLY === 'alone' || ONLY === 'idle', NO_IDLE = process.argv.includes('--no-idle') || ONLY === 'alone';
const NO_ALONE = ONLY === 'idle';
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (!CTL_ONLY) {
    if (!NO_ALONE) {
      const a = await run(backend, 'alone');
      check('power-on to the stage: Play, join, Start, the select, attack to be ready', a.seats === 1 && a.select === 1 && a.selectLeft === 0, `${a.seats} seat, select up ${a.select}, left ${a.selectLeft === 0}`);
      check('pause and resume', a.paused > 0 && a.pauseScreen !== 3 && a.resumed === 1, `paused at tick ${a.paused} (screen ${a.pauseScreen}), resumed ${a.resumed}`);
      check('a newcomer with human error clears stage 1 with a life in hand', won(a), `phase ${a.phase} after ${a.secs.toFixed(0)} s, ${a.kills} kills, lives lost ${a.lost}, boss phase ${a.boss}`);
      check('no blow from off screen', a.unfair === 0, `game.unfair ${a.unfair}`);
    }
    if (!NO_IDLE) {
      const b = await run(backend, 'idle-seat', { idle: true });
      check('idle seat: counted out at the select', b.seats === 2 && b.selectLeft === 0 && b.absent[1] === 1 && b.crew === 1, `${b.seats} seats, seat.absent ${b.absent}, game.crew ${b.crew}`);
      check('idle seat: the same newcomer still clears stage 1 with a life in hand', won(b), `phase ${b.phase} after ${b.secs.toFixed(0)} s, ${b.kills} kills, lives lost ${b.lost}`);
    }
  }
  if (!NO_CTL) {
    const c = await run(backend, 'control-dmg300', { cmds: ['sw dmg 300'] });
    check('control: sw dmg 300, the same newcomer runs out of lives', c.phase === OVER, `phase ${c.phase} after ${c.secs.toFixed(0)} s, ${c.kills} kills, lives lost ${c.lost}`);
    const o = await run(backend, 'control-old-stage1', { cmds: ['sw old_stage1 1'] });
    check('control: sw old_stage1 1 (the first stage as it shipped), the same newcomer has no life in hand at the end', !won(o), `phase ${o.phase} after ${o.secs.toFixed(0)} s, ${o.kills} kills, lives lost ${o.lost}, boss phase ${o.boss}`);
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
