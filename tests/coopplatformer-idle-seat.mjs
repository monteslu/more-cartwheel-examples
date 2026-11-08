// coopplatformer on the cart through romdev, GL and WebGPU, by the pad: from the title, player 1 runs the first level
// the way its hints say (hold Run, jump what is in the way), alone and with a second seat that joined and was never
// played. A seat nobody plays holds nobody back: the camera follows the player, the idle one is carried along in a
// bubble, and player 1 gets as far as alone (past column GOAL). Control: sw idle_anchor 1 (the idle seat holds the
// group back, as the game once let it: the player is bubbled back to the start whenever they get a screen and a half
// ahead) must keep player 1 short of GOAL in the same frames.
//   node tests/coopplatformer-idle-seat.mjs [--gl] [--webgpu] [--cart <path>] [--legs alone,idle,control]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.COOP_SESSION || 'coopplatformer-idle-seat', OUT = join(ROOT, 'out/coopplatformer-idle-seat');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/coopplatformer.wasc')));
const LEGS = arg('--legs', 'alone,idle,control').split(',');
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
/* the first level (COOP_MAP=0): flat to the spike pit at columns 80 to 86, the first flag at 92, steps up at 105 and 111.
 * Player 1 takes a running jump at the pit's lip, as its sign and the hint line say, and jumps a step it runs into. The
 * control is bubbled back to the idle seat at the start once a screen and a half ahead, short of the first flag. */
const GOAL = 108, FRAMES = 60 * 50, LIPS = [78.6];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  let r;
  try { r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) }); }
  catch (e) { console.error(`romdev ${name}: ${e.cause?.message || e.message} (the server is down)`); await bye(3); }
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await pads(...a); await step(3); await pads({}, {}); await step(8); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

// from the title: Play, the seats join (P1, and P2 when idle), Start; P2 never touches its pad again
async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'south'); await step(20);
  await press(0, 'south'); if (idle) await press(1, 'south');
  await press(0, 'start');
  for (let f = 0; f < 600 && (await rd('g.state')) !== 3; f += 30) await step(30); /* the level is up */
  await step(60);
  const players = await rd('g.players');
  // player 1 runs right holding Run; when a stretch of running gains nothing, a running jump held for height
  let frames = 0, best = -1e9, stall = 0, maxx = -1e9;
  while (frames < FRAMES) {
    const x = (await rd('g.ppos'))[0];
    maxx = Math.max(maxx, x);
    if (maxx >= GOAL) break; /* a fall sends player 1 back to the last flag: how far they got is what counts */
    if (x > best + 0.3) best = x, stall = 0; else stall++;
    if (LIPS.some((l) => x > l - 1.2 && x < l + 0.3)) { /* at a lip: a running jump held for height */
      await pads({ right: true, west: true, south: true }); await step(26);
      await pads({ right: true, west: true }); await step(20);
      frames += 46; stall = 0; continue;
    }
    if (stall >= 2) {
      await pads({ right: true, west: true }); await step(4);
      await pads({ right: true, west: true, south: true }); await step(22);
      await pads({ right: true, west: true }); await step(6);
      frames += 32; stall = 0;
    } else { /* run; in small steps coming up to a lip */
      const n = LIPS.some((l) => x > l - 8 && x < l) ? 2 : 15;
      await pads({ right: true, west: true }); await step(n); frames += n;
    }
  }
  await pads({}, {});
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  const pp = await rd('g.ppos');
  return { players, frames, x: pp[0], maxx, p2: pp[2], carried: +(await rd('g.carried')), idle: await rd('g.idle'), deaths: +(await rd('g.deaths')) };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (LEGS.includes('alone')) {
    const one = await run(backend, 'alone', false);
    check('one seat: player 1 runs the first level past column ' + GOAL, one.players === 1 && one.maxx >= GOAL, `${one.players} player(s), column ${one.maxx.toFixed(1)} after ${one.frames} frames (${one.deaths} falls)`);
  }
  if (LEGS.includes('idle')) {
    const two = await run(backend, 'idle-seat', true);
    check('a second seat nobody plays: player 1 still gets past column ' + GOAL, two.players === 2 && two.maxx >= GOAL && two.carried > 0, `${two.players} players, column ${two.maxx.toFixed(1)} after ${two.frames} frames (the idle seat carried ${two.carried} times, at column ${two.p2.toFixed(1)}, idle ${two.idle[1]} ticks, ${two.deaths} falls)`);
  }
  if (LEGS.includes('control')) {
    const ctl = await run(backend, 'control', true, 'sw idle_anchor 1');
    check('control: sw idle_anchor 1, the idle seat holds player 1 back', ctl.players === 2 && ctl.maxx < GOAL, `${ctl.players} players, furthest column ${ctl.maxx.toFixed(1)} after ${ctl.frames} frames (now at ${ctl.x.toFixed(1)}, the idle seat at ${ctl.p2.toFixed(1)}, ${ctl.deaths} falls)`);
  }
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
