// A newcomer's first course, on the cart through romdev, GL and WebGPU, by the pad and the screen alone. From power-on
// A takes the title's first row (the Cup), A on the Cup's card races its first course (Bunny Hill); the walk-in plays
// hands off. Racing, the newcomer steers by what the view draws ahead (cue.trees, cue.herring: the trees and herring
// in view, read from the debug fields the game draws them from), as a person does: a look every 100 ms, acted on 0.3 s
// late, aimed with a hand that wanders (seeded noise of about a twentieth of the screen), now and
// then a look away (no input for a moment). It heads for the nearest herring it sees, swerves from a tree it is about
// to hit, and paddles (A) while the speedometer reads under 25 km/h. It must cross the finish line within one and a
// half times par (60 s), take the results screen's prompt (A) back to the Cup's card, and on the way read the slope:
// at least GOAL_HERRING herring taken (the trees it hits are printed: chasing a fish it sees runs it into some).
// Control, which must fail: the same newcomer with the herring and the trees not drawn (hide items, hide trees: the
// physics is unchanged, nothing on screen to read) falls short of that.
//   node tests/tuxracer-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/tuxracer-newcomer');
const SESSION = process.env.TUXRACER_SESSION || 'tuxracer-newcomer', AGENT = process.env.TUXRACER_AGENT || 'tuxracer-newcomer';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/tuxracer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT };
// every exit tears this session's romdev host down, a failed run too
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
// (node:http, not fetch: fetch gives up on a reply that takes five minutes, and a busy GPU can take that long)
const post = (name, body) => new Promise((ok, no) => {
  const q = request(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: H }, (res) => {
    let t = '';
    res.setEncoding('utf8');
    res.on('data', (c) => (t += c));
    res.on('end', () => ok({ ok: res.statusCode === 200, text: () => t }));
  });
  q.on('error', no);
  q.end(body);
});
const tool = async (name, args) => {
  let r;
  try { r = await post(name, JSON.stringify(args)); }
  catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  const t = r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pad = (p) => tool('input', { op: 'set', ports: [p] });
const press = async (p) => { await pad(p); await step(4); await pad({}); await step(20); };
const goal = (r) => r.over === 8 && r.finished === 1 && r.time <= PAR * 1.5 && r.herring >= GOAL_HERRING;
const ST_TITLE = 0, ST_CUP = 2, ST_RACING = 6, ST_RACE_OVER = 8;
const LOOK = 6, LATE = 18, RACE_S = 180, HERRING = 23, PAR = 40, GOAL_HERRING = 4; // LATE a multiple of LOOK: the look acted on is three looks old
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

/** a seeded hand: noise in [-1, 1] summed (about normal), and a coin */
function hand(seed) {
  let s = seed >>> 0;
  const u = () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296);
  return { noise: () => (u() + u() + u() - 1.5) / 1.5, coin: (p) => u() < p };
}

/** the newcomer's choice from one look: where to aim (screen thousandths) */
function aim(look) {
  const t = look.trees, h = look.herring;
  /* a tree it is about to hit: big on screen, low in the view, near the middle where Tux is heading */
  for (let i = 0; i < 8; i++) {
    const x = t[i * 3], y = t[i * 3 + 1], w = t[i * 3 + 2];
    if (x < 0) break;
    if (y > 600 && Math.abs(x - 500) < w / 2 + 70) return x < 500 ? 500 + w / 2 + 120 : 500 - w / 2 - 120;
  }
  /* the nearest herring it sees, unless it is already off to the side and behind the turn */
  for (let i = 0; i < 8; i++) {
    const x = h[i * 3], y = h[i * 3 + 1];
    if (x < 0) break;
    if (Math.abs(x - 500) < 380 && y > 380) return x;
  }
  return 500;
}

async function run(backend, cues) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: process.env.CW_GPU_POWER || 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(240);
  const r = { title: +(await rd('game.state')) };
  await press({ south: true });                                              /* the title's first row: the Cup */
  r.card = +(await rd('game.state'));
  await press({ south: true });                                              /* the card's prompt: race */
  for (let f = 0; f < 1800 && +(await rd('game.state')) !== ST_RACING; f += 30) await step(30); /* the walk-in, hands off */
  r.racing = +(await rd('game.state'));
  if (!cues) { await tool('wasm', { op: 'command', text: 'hide items 1' }); await tool('wasm', { op: 'command', text: 'hide trees 1' }); }
  const H = hand(backend === 'gl' ? 0x7a11 : 0x7a12), seen = [];
  let frames = 0, state = r.racing, lapse = 0, slow = 0;
  while (frames < RACE_S * 60 && state === ST_RACING) {
    seen.push({ trees: await rd('cue.trees'), herring: await rd('cue.herring') });
    const look = seen.length > LATE / LOOK ? seen[seen.length - 1 - LATE / LOOK] : null; /* acted on 0.3 s late */
    const kmh = +(await rd('tux.speed')) * 3.6;
    const p = {};
    if (lapse > 0) lapse--;
    else if (H.coin(0.04)) lapse = 3;                                        /* looked away for a moment */
    else if (look) {
      const x = aim(look) + H.noise() * 50;
      if (x < 470) p.left = true; else if (x > 530) p.right = true;
    }
    if (kmh < 25) p.south = true;                                            /* the speedometer reads slow: paddle */
    slow = kmh < 3 ? slow + LOOK : 0;
    await pad(p); await step(LOOK); frames += LOOK;
    if (frames % 60 === 0) state = +(await rd('game.state'));
    if (frames === 1200) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${cues ? 'newcomer' : 'control'}-20s.png`) });
  }
  await pad({});
  r.over = +(await rd('game.state'));
  r.finished = +(await rd('race.finished'));
  r.time = +(await rd('race.time'));
  r.herring = +(await rd('race.herring'));
  r.trees = +(await rd('race.tree_hits'))[0];
  r.lives = +(await rd('cup.lives'));
  await step(60);
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${cues ? 'newcomer' : 'control'}-results.png`) });
  await press({ south: true });                                              /* the results' prompt: continue */
  r.back = +(await rd('game.state'));
  await tool('host', { op: 'shutdown' });
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const n = await run(backend, true);
  check('power-on: the title, A opens the Cup, A races its first course', n.title === ST_TITLE && n.card === ST_CUP && n.racing === ST_RACING,
    `states ${n.title}, ${n.card}, ${n.racing}`);
  check('steering by what the view shows, the newcomer finishes the first course', n.over === ST_RACE_OVER && n.finished === 1,
    `state ${n.over}, ${n.time.toFixed(1)} s, ${n.herring} of ${HERRING} herring, ${n.trees} trees hit, ${n.lives} lives left`);
  check(`reading the slope: within ${PAR * 1.5} s, at least ${GOAL_HERRING} herring`, goal(n), `${n.time.toFixed(1)} s, ${n.herring} herring, ${n.trees} trees`);
  check('A on the results goes back to the Cup', n.back === ST_CUP, `state ${n.back}`);
  const c = await run(backend, false);
  check('control: the herring and trees not drawn, the newcomer falls short', !goal(c),
    `${c.herring} of ${HERRING} herring, ${c.trees} trees hit, state ${c.over}, ${c.time.toFixed(1)} s`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
