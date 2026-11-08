// fighter on the cart through romdev, GL and WebGPU, by the pad, the way a first-time player would: from the title, by
// what the screen shows (Play, take a seat, Start, lock in), then a match against the computer played as a newcomer
// plays one (walk at the other fighter, mash punch and kick with whatever direction the thumb is on, try the fireball
// off the move list now and then, hold block a moment). The session opens on Easy: the select screen says so
// (game.difficulty 0), the newcomer lands a blow inside 10 seconds of FIGHT, and wins a match within three tries; after
// that win the computer steps up to Normal (game.difficulty 1).
// Then the arcade: from the select screen punch turns the mode to Arcade, lock in, and four fights follow (the three
// other fighters, then the mirror match), each result screen leading to the next. `sw cpu_hp 1` (the computer starts
// each round on one life) keeps that part short; the ladder must be cleared, every opponent met once.
// Controls that must fail: the same newcomer who only walks (no buttons) lands nothing in those 10 seconds; the arcade
// run without the mode press (Versus) clears no ladder.
//   node tests/fighter-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.FIGHTER_SESSION || 'fighter-newcomer', OUT = join(ROOT, 'out/fighter-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/fighter.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const HDR = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': 'fg-fighter' };
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: HDR, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: HDR, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const shot = (name) => tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, name + '.png') });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
const press = async (b, hold = 4, after = 8) => { await pads({ [b]: true }); await step(hold); await pads({}); await step(after); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

/* the newcomer's thumbs: a fixed-seed generator, so a run is the same run */
function newcomer(seed, buttons) {
  let r = seed >>> 0, q = [];
  const rnd = () => { r = (Math.imul(r, 1664525) + 1013904223) >>> 0; return r >>> 8; };
  return (toward) => { /* the pad state for the next 4 frames; toward: 'right' or 'left' */
    if (!q.length) {
      const k = rnd() % 100, away = toward === 'right' ? 'left' : 'right';
      if (k < 30) for (let i = 0; i < 2 + rnd() % 5; i++) q.push({ [toward]: true });
      else if (k < 70 && buttons) for (let i = 0; i < 2 + rnd() % 3; i++) { const d = [null, null, toward, 'down', 'up'][rnd() % 5], b = rnd() % 2 ? 'west' : 'south'; q.push({ ...(d ? { [d]: true } : {}), [b]: true }, d ? { [d]: true } : {}); }
      else if (k < 78 && buttons) q.push({ down: true }, { down: true, [toward]: true }, { [toward]: true, west: true }, {}); /* the fireball, sloppily */
      else if (k < 84 && buttons) q.push({ east: true }, { east: true }, { east: true });
      else if (k < 90) q.push({ [away]: true }, { [away]: true });
      else q.push({}, {});
    }
    return q.shift();
  };
}

/* a match played by the newcomer from FIGHT to the result screen: frames to the first blow landed, who won */
async function play(thumbs, limit = 60 * 150) {
  let frames = 0, first = -1, fightAt = -1;
  const hp0 = (await rd('sim.hp'))[1];
  while (frames < limit) {
    const panel = +(await rd('game.panel'));
    if (panel === 2) break;
    const pos = await rd('sim.pos'), phase = +(await rd('game.phase'));
    if (phase === 1 && fightAt < 0) fightAt = frames;
    const toward = pos[2] > pos[0] ? 'right' : 'left';
    await pads(thumbs(toward)); await step(4); frames += 4;
    if (first < 0 && fightAt >= 0 && (await rd('sim.hp'))[1] < hp0) first = frames - fightAt;
  }
  await pads({});
  return { first, frames, wins: await rd('sim.wins'), panel: +(await rd('game.panel')) };
}

async function load(backend) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: process.env.CW_GPU_POWER || 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  await press('south'); await step(20); /* the title: Play */
  await press('south');                 /* the join screen: a seat */
  await press('start'); await step(30); /* play: the select screen */
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  await load(backend);
  const diff0 = +(await rd('game.difficulty'));
  await shot(`${backend}-select`);
  check('the session opens on Easy', diff0 === 0, `game.difficulty ${diff0}`);
  await press('south'); await step(30); /* lock in */
  const thumbs = newcomer(0x5eed1234, true);
  let won = false, firstHit = -1, tries = 0;
  for (; tries < 3 && !won; tries++) {
    const m = await play(thumbs);
    if (tries === 0) { firstHit = m.first; }
    won = m.wins[0] === 2;
    await step(50);
    await shot(`${backend}-result-${tries}`);
    console.log(`     match ${tries + 1}: first blow ${m.first} frames after FIGHT, wins ${m.wins}, ${m.frames} frames`);
    if (!won) { await press('south'); await step(30); }
  }
  check('the newcomer lands a blow inside 10 s of FIGHT', firstHit >= 0 && firstHit <= 600, `${firstHit} frames`);
  check('the newcomer wins a match within three tries', won, `${tries} match(es)`);
  const diff1 = +(await rd('game.difficulty'));
  check('after that win the computer steps up to Normal', won && diff1 === 1, `game.difficulty ${diff1}`);
  /* control: the same newcomer with no buttons lands nothing */
  await load(backend);
  await press('south'); await step(30);
  const walker = await play(newcomer(0x5eed1234, false), 60 * 12);
  check('control: only walking lands no blow in 10 s', walker.first < 0 || walker.first > 600, `first blow ${walker.first}`);
  /* the arcade, and its control without the mode press */
  for (const [tag, mode] of [['arcade', true], ['arcade-control', false]]) {
    await load(backend);
    await tool('wasm', { op: 'command', text: 'sw cpu_hp 1' });
    if (mode) { await press('west'); await step(10); }
    await shot(`${backend}-${tag}-select`);
    await press('south'); await step(30);
    const th = newcomer(0xa4cade, true);
    for (let k = 0; k < 6; k++) {
      const m = await play(th, 60 * 60);
      await step(50);
      if (k === 0 || (await rd('arcade'))[2]) await shot(`${backend}-${tag}-result-${k}`);
      if ((await rd('arcade'))[2]) break;
      await press('south'); await step(30);
      if (m.panel !== 2) break;
    }
    const a = await rd('arcade');
    if (mode) check('arcade: four fights, the ladder cleared, every opponent met', a[0] === 1 && a[2] === 1 && a[1] === 4 && (a[3] & 16) && [0, 1, 2, 3].filter((d) => a[3] & (1 << d)).length === 3, `arcade ${a}`);
    else check('control: Versus clears no ladder', a[2] === 0, `arcade ${a}`);
  }
}
await pads({});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
