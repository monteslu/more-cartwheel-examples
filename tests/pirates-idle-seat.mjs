// pirates on the cart through romdev, GL and WebGPU, by the pad: a Cooperative Voyage on The Gilded Shoals with a
// second captain that joined on the title (Start on its own controller) and was never played. Player one sails
// as a person reading the screen would: over the floating cargo near the P1 HARBOR label, back to the end of its
// pier and the south button to bank, again and again. A captain whose controller is left alone for 20 s sails on
// autopilot while another person sails (its panel and label say so) until any of its controls is used, so it banks cargo for the fleet
// instead of holding the voyage's quota (which counts it) back from its pier; a touch of its stick takes the
// helm back. Alone, a controller put down sails nobody: a voyage nobody plays does not play itself.
// Control: sw idle_stays 1 (a captain left alone stays where it is, as it once did) must leave the idle captain at
// its pier with nothing banked.
//   node tests/pirates-idle-seat.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.PIRATES_SESSION || 'pirates-idle-seat', OUT = join(ROOT, 'out/pirates-idle-seat');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/pirates.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p0, p1 = {}) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, p1] }, frames }] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await hold(4, ...a); await hold(8, {}, {}); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

// the cargo floating near P1 HARBOR on the voyage's first screen (metres; the screen is 17.5 px a metre around the
// centre), and the end of P1's pier
const CARGO = [[-34, -2], [-31, 7]];
const PIER = [-36.5, -8];
let frames = 0;
// P1's bow: where a person sees the ship pointing (pirates_ai reports the pose)
const pose = async () => (await tool('wasm', { op: 'command', text: 'pirates_ai 0' })).reply;
async function sail(tx, tz, near, max) {
  let p = await pose();
  for (let f = 0; f < max; f += 20) {
    const dx = tx - p.x, dz = tz - p.z, d = Math.hypot(dx, dz);
    if (d < near) return true;
    let diff = Math.atan2(dx, dz) - p.yaw;
    while (diff > Math.PI) diff -= 2 * Math.PI;
    while (diff < -Math.PI) diff += 2 * Math.PI;
    await pads({ axes: { lx: Math.abs(diff) < 0.08 ? 0 : Math.max(-1, Math.min(1, diff * 2.5)), ly: d < 5 ? -0.5 : -1 } }, {});
    await step(20); frames += 20;
    p = await pose();
  }
  return false;
}

// alone, and nobody touches the pad: a voyage nobody plays does not play itself
async function alone(backend) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  await press(0, 'start'); await step(20);
  const p0 = (await rd('pirates.pos')).slice(0, 2);
  let seen = 0;
  for (let f = 0; f < 60 * 30; f += 300) { await step(300); seen |= +(await rd('pirates.autopilot')); }
  const p1 = (await rd('pirates.pos')).slice(0, 2);
  return { humans: +(await rd('pirates.humans')), quota: +(await rd('pirates.quota')), seen, moved: Math.hypot(p1[0] - p0[0], p1[1] - p0[1]) / 100 };
}

async function run(backend, tag, pre, budget) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(1, 'start');                                  /* "other captains: Start joins" */
  await press(0, 'start'); await step(20);                  /* "Start set sail" */
  const r = { humans: +(await rd('pirates.humans')), quota: +(await rd('pirates.quota')), mode: +(await rd('pirates.mode')), autoSeen: 0, p1Auto: 0 };
  const p2Start = (await rd('pirates.pos')).slice(2, 4);
  frames = 0;
  let trips = 0;
  r.log = [];
  while (frames < budget) {
    for (const [x, z] of CARGO) { if (frames >= budget || (await rd('pirates.held'))[0] >= 6) break; await sail(x, z, 1.8, 420); }
    await sail(PIER[0], PIER[1], 2.5, 900);
    await pads({}, {}); await press(0, 'south'); frames += 12; trips++;  /* "press (south) to bank and repair" */
    const a = +(await rd('pirates.autopilot')); r.autoSeen |= a; r.p1Auto |= a & 1;
    r.log.push((await rd('pirates.bank')).slice(0, 2).join('+'));
  }
  await pads({}, {});
  r.bank = await rd('pirates.bank'); r.trips = trips; r.frames = frames;
  r.auto = +(await rd('pirates.autopilot'));
  const p2End = (await rd('pirates.pos')).slice(2, 4);
  r.p2Moved = Math.hypot(p2End[0] - p2Start[0], p2End[1] - p2Start[1]) / 100;
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  /* the idle captain's person comes back: a touch of the stick takes the helm */
  await hold(6, {}, { axes: { lx: 0.6 } }); await hold(4, {}, {});
  r.afterTouch = +(await rd('pirates.autopilot'));
  return r;
}

const BUDGET = 60 * 50;
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await alone(backend);
  check('alone with the controller put down for 30 s: nobody sails', one.humans === 1 && one.quota === 10 && one.seen === 0 && one.moved < 1, `${one.humans} captain, quota ${one.quota}, autopilot seen ${one.seen}, P1 moved ${one.moved.toFixed(1)} m`);
  const two = await run(backend, 'idle-seat', null, BUDGET);
  check('voyage with a second captain who joined: the quota counts it', two.mode === 0 && two.humans === 2 && two.quota === 17, `mode ${two.mode}, ${two.humans} captains, quota ${two.quota}`);
  check('player one plays: sails, banks, never on autopilot', two.bank[0] > 0 && !two.p1Auto, `P1 banked ${two.bank[0]} in ${two.trips} trips over ${two.frames} frames (P1+P2 after each: ${two.log.join(' ')}), autopilot seen ${two.autoSeen}`);
  check('the idle captain sails on autopilot and banks for the fleet', (two.autoSeen & 2) && two.bank[1] > 0 && two.p2Moved > 1, `P2 banked ${two.bank[1]}, moved ${two.p2Moved.toFixed(1)} m from its pier, autopilot ${two.auto}`);
  check('a touch of its stick takes the helm back', (two.auto & 2) && !(two.afterTouch & 2), `autopilot ${two.auto} before, ${two.afterTouch} after`);
  const ctl = await run(backend, 'control', 'sw idle_stays 1', 60 * 35); /* past the 20 s an idle captain waits, with P1 sailing */
  check('control: sw idle_stays 1, the idle captain stays at its pier with nothing banked', ctl.humans === 2 && ctl.bank[1] === 0 && ctl.autoSeen === 0 && ctl.p2Moved < 1 && ctl.bank[0] > 0, `P2 banked ${ctl.bank[1]}, moved ${ctl.p2Moved.toFixed(1)} m, autopilot seen ${ctl.autoSeen}; P1 banked ${ctl.bank[0]} (${ctl.log.join(' ')})`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
