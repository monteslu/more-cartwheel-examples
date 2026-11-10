// arenafps on the cart through romdev, GL and WebGPU, by the pad: in co-op waves a second seat that joined and is never
// played does not drain the shared lives or grow the waves. From the title player 1 and a second pad join, player 1
// picks CO-OP WAVES on the setup screen and fires to start, then turns and fires for 90 s; the second pad is never
// touched again. The bots find the idle fighter and down it; it must stay down showing "press any button to come back
// in" (coop.away_skips), spend no shared life, and the waves are sized for one person (coop.present 1).
// Control: sw idle_lives 1 (the idle seat comes back on shared lives and counts toward the waves, as it once did) must
// spend lives on it in the same frames.
//   node tests/arenafps-idle-seat.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.ARENA_SESSION || 'arenafps-idle-seat', OUT = join(ROOT, 'out/arenafps-idle-seat');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/arenafps.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
/* both pads are always sent, so the second stays connected (a pad that goes away frees its seat) */
const pads = (p0 = {}, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p0 = {}, p1 = {}) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, p1] }, frames }] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await hold(4, ...a); await hold(12); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const PLAY = 60 * 90;

async function run(backend, tag, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(120);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'south'); await step(20); /* the title: Play */
  await press(0, 'south');                 /* the join screen: P1 */
  await press(1, 'south');                 /* a second pad joins, and is never touched again */
  await press(0, 'start'); await step(40); /* the setup screen, on START */
  await press(0, 'down');                  /* wraps to the arena row */
  await press(0, 'down');                  /* the mode row */
  await press(0, 'right'); await press(0, 'right'); /* CO-OP WAVES */
  const mode = +(await rd('game.mode'));
  for (let i = 0; i < 8 && +(await rd('game.state')) !== 1; i++) { await hold(4, { r: true }); await hold(20); }
  const state = +(await rd('game.state')), players = +(await rd('game.players')), lives0 = +(await rd('match.lives'));
  let shot = false, r = null;
  /* the match as it stands; one that ends (overrun) is read as it stood last, before a press could start the next */
  const snap = async () => { const deaths = await rd('fighters.deaths'), alive = await rd('fighters.alive');
    return { ticks: +(await rd('game.ticks')), state, mode, players, lives0, lives: +(await rd('match.lives')), wave: +(await rd('match.wave')), present: +(await rd('coop.present')),
      skips: +(await rd('coop.away_skips')), d1: deaths[0], d2: deaths[1], alive2: alive[1] }; };
  for (let f = 0; f < PLAY && +(await rd('game.state')) === 1; f += 30) { /* player 1 turns and fires; the second pad stays still */
    r = await snap();
    await tool('input', { op: 'sequence', steps: [{ input: { ports: [{ r: true, axes: { rx: 0.35, rt: 1 } }, {}] }, frames: 20 }, { input: { ports: [{ axes: { lx: 0.5 } }, {}] }, frames: 10 }] });
    if (!shot && r.skips > 0) { shot = true; await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-away.png`) }); }
  }
  await pads();
  if (!r || +(await rd('game.state')) === 1) r = await snap();
  r.spent = r.lives0 - r.lives;
  r.p2back = r.d2 === 0 ? 0 : r.d2 - (r.alive2 ? 0 : 1); /* the times the idle fighter came back */
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const a = await run(backend, 'idle-seat');
  console.log(`    ${JSON.stringify(a)}`);
  check('co-op with a second seat nobody plays', a.state === 1 && a.mode === 2 && a.players === 2 && a.wave >= 1, `state ${a.state}, mode ${a.mode}, ${a.players} seats, wave ${a.wave}`);
  check('the bots downed the idle fighter and it stays down', a.d2 >= 1 && a.alive2 === 0 && a.skips >= 1, `its deaths ${a.d2}, alive ${a.alive2}, respawns held back ${a.skips}`);
  check('no shared life spent on the idle seat', a.p2back === 0 && a.spent <= a.d1, `lives ${a.lives0} -> ${a.lives} (player 1 died ${a.d1} times), the idle fighter came back ${a.p2back} times`);
  check('the waves are sized for the one person playing', a.present === 1, `coop.present ${a.present}`);
  const c = await run(backend, 'control', 'sw idle_lives 1');
  console.log(`    ${JSON.stringify(c)}`);
  check('control: sw idle_lives 1, the idle seat spends shared lives and grows the waves', c.state === 1 && c.players === 2 && c.p2back >= 1 && c.present === 2 && c.skips === 0, `the idle fighter came back ${c.p2back} times, coop.present ${c.present}, held back ${c.skips}`);
}
await pads();
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
