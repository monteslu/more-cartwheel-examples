// A newcomer's Cup, on the cart through romdev, GL and WebGPU, by the pad and the screen alone. From power-on A takes
// the title's first row (the Cup), A on the Cup's card races its first course (Bunny Hill); the walk-in plays hands off.
// Racing, the newcomer steers by what the view draws ahead (cue.trees, cue.herring: the trees and herring in view, read
// from the debug fields the game draws them from), as a person does: a look every 100 ms, acted on 0.3 s late, aimed with
// a hand that wanders (seeded noise of about a twentieth of the screen), now and then a look away (no input for a
// moment). It heads for the nearest herring it sees, swerves from a tree it is about to hit, and paddles (A) while the
// speedometer reads under 25 km/h. The results' prompt (A) goes back to the Cup's card, and the next attempt or race.
// The goal: pass the Cup's first race (the game's own verdict, cup.won, against the requirements the card and the HUD
// show) and find the second race open; then each later race passed within the Cup's lives. The card's requirements and
// the HUD's count toward them are read too (cup.need_herring, cup.need_time).
// Controls, which must fail: the original's rule for the Cup (opt cuprule original: every herring, near par), and the
// same newcomer with the herring and trees not drawn (hide items, hide trees: nothing on screen to read).
//   node tests/tuxracer-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--no-controls | --controls-only] [--measure]   (--measure: each Cup race once, the
//   numbers a requirement is set from, under the original's rule so nothing advances)
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
const ST_TITLE = 0, ST_CUP = 2, ST_RACING = 6, ST_RACE_OVER = 8;
const LOOK = 6, LATE = 18, RACE_S = 180, LIVES = 4; // LATE a multiple of LOOK: the look acted on is three looks old
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

const cmd = (t) => tool('wasm', { op: 'command', text: t });
/** the cart's whole debug state in one call */
const snap = async () => { const d = await tool('wasm', { op: 'debugState' }); return Object.fromEntries((d.fields || []).map((f) => [f.name, f.values ?? f.value])); };
/** these buttons held for n frames, stepped in one call (none held: a plain step) */
const hold = (buttons, n) => tool('input', { op: 'sequence', steps: [{ input: { ports: [Object.fromEntries(buttons.map((b) => [b, true]))] }, frames: n }] });
async function load(backend) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: process.env.CW_GPU_POWER || 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(240);
  await cmd('cup 0');                                                        /* a first Cup: race 1, full lives (a fresh save) */
}
/** one race from the Cup's card: A, the walk-in, the newcomer at the wheel, the results, A back to the card */
async function race(backend, cues, seed, tag) {
  const r = { card: +(await rd('game.state')), race: +(await rd('cup.race')), need: +(await rd('cup.need_herring')) };
  await press({ south: true });                                              /* the card's prompt: race */
  for (let f = 0; f < 1800 && +(await rd('game.state')) !== ST_RACING; f += 30) await step(30); /* the walk-in, hands off */
  r.racing = +(await rd('game.state'));
  r.need = +(await rd('cup.need_herring')), r.limit = +(await rd('cup.need_time'));
  await cmd(`hide items ${cues ? 0 : 1}`); await cmd(`hide trees ${cues ? 0 : 1}`);
  const H = hand(seed), seen = [];
  let frames = 0, state = r.racing, lapse = 0;
  /* one decision every LOOK frames: one read of the cart's whole debug state, then the frames to the next decision stepped
   * in one call with the buttons held over them (romdev gets ranges, never a request per frame) */
  while (frames < RACE_S * 60 && state === ST_RACING) {
    const s = await snap();
    state = +s['game.state'];
    if (state !== ST_RACING) break;
    seen.push({ trees: s['cue.trees'], herring: s['cue.herring'] });
    const look = seen.length > LATE / LOOK ? seen[seen.length - 1 - LATE / LOOK] : null; /* acted on 0.3 s late */
    const kmh = +s['tux.speed'] * 3.6;
    const b = [];
    if (lapse > 0) lapse--;
    else if (H.coin(0.04)) lapse = 3;                                        /* looked away for a moment */
    else if (look) {
      const x = aim(look) + H.noise() * 50;
      if (x < 470) b.push('left'); else if (x > 530) b.push('right');
    }
    if (kmh < 25) b.push('south');                                           /* the speedometer reads slow: paddle */
    await hold(b, LOOK); frames += LOOK;
    if (frames === 1200 && tag) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-20s.png`) });
  }
  await pad({});
  r.over = +(await rd('game.state'));
  r.finished = +(await rd('race.finished'));
  r.time = +(await rd('race.time'));
  r.herring = +(await rd('race.herring'));
  r.trees = +(await rd('race.tree_hits'))[0];
  r.won = +(await rd('cup.won'));
  r.lives = +(await rd('cup.lives'));
  await step(60);
  if (tag) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-results.png`) });
  await press({ south: true });                                              /* the results' prompt: continue */
  for (let f = 0; f < 120 && +(await rd("game.state")) !== ST_CUP; f += 30) await step(30);
  r.back = +(await rd('game.state'));
  r.next = +(await rd('cup.race'));
  const d = `race ${r.race + 1}: ${r.time.toFixed(1)} s (limit ${r.limit.toFixed(0)}), ${r.herring} herring (${r.need} needed), ${r.trees} trees, ${r.won ? 'passed' : 'not passed'}, ${r.lives} lives, next race ${r.next + 1}`;
  console.log(`     ${backend} ${tag || ''} ${d}`);
  return { ...r, d };
}
const seedOf = (backend, race, attempt) => (backend === 'gl' ? 0x7a11 : 0x7a12) + race * 977 + attempt * 131;
const MEASURE = process.argv.includes('--measure');
const FROM = +arg('--from', '0'); // --from 1: start the Cup at its second race (cup 1), for a job split in halves
const PATH = !process.argv.includes('--controls-only'), CONTROLS = !process.argv.includes('--no-controls'); // a job's time limit: the two halves can run apart

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (MEASURE) {
    await load(backend);
    await cmd('opt cuprule original');
    await press({ south: true });
    for (let i = 0; i < 3; i++) for (let a = 0; a < 2; a++) { await cmd(`cup ${i}`); await step(10); await race(backend, true, seedOf(backend, i, a + 7), ''); }
    await tool('host', { op: 'shutdown' });
    continue;
  }
  if (PATH) {
  await load(backend);
  if (FROM) await cmd(`cup ${FROM}`);                                      /* a half job: the Cup from this race */
  const title = +(await rd('game.state'));
  await press({ south: true });                                              /* the title's first row: the Cup */
  const card = +(await rd('game.state'));
  check('power-on: the title, A opens the Cup', title === ST_TITLE && card === ST_CUP, `states ${title}, ${card}`);
  const tries = [[], [], []];
  for (let i = FROM, a = 0; i < 3 && a < LIVES; a++) {
    const r = await race(backend, true, seedOf(backend, i, a), `cup${i + 1}-try${a + 1}`);
    tries[i].push(r);
    if (r.over !== ST_RACE_OVER || r.back !== ST_CUP) break;
    if (r.won) { i++; a = -1; if (i < 3 && r.next !== i) break; }
    if (r.lives <= 0) break;
  }
  const one = tries[0];
  if (!FROM) check('the card and the HUD show what passes: herring needed and a time limit', one.length && one[0].need > 0 && one[0].limit > 0, one.length ? `${one[0].need} herring, ${one[0].limit} s` : 'no race');
  if (!FROM) check('the Cup\'s first race is passed by a first-timer, and the second race opens', one.some((r) => r.won === 1 && r.next === 1), one.map((r) => r.d).join('; '));
  for (const i of [1, 2].filter((i) => i >= FROM)) check(`Cup race ${i + 1} is passed within the Cup's lives`, tries[i].some((r) => r.won === 1), tries[i].map((r) => r.d).join('; ') || 'not reached');
  await tool('host', { op: 'shutdown' });
  }
  /* controls: the original's rule; the cues not drawn */
  if (CONTROLS) for (const [name, setup, cues] of [['the original rule (every herring, near par)', 'opt cuprule original', true], ['the herring and trees not drawn', '', false]]) {
    await load(backend);
    if (setup) await cmd(setup);
    await press({ south: true });
    const r = await race(backend, cues, seedOf(backend, 0, 0), cues ? 'control-rule' : 'control-blind');
    check(`control: ${name}, the first race is not passed`, r.won !== 1 && r.next === 0, r.d);
    await tool('host', { op: 'shutdown' });
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
