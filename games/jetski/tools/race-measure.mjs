// A whole race by held stick and throttle through romdev, reacting late like a modest person, no tricks: what the race gave. Works on any cart with the basic debug fields, so a released
// cart can be measured the same way: held place changes, speed kept over each hop and ramp, buoys, mean and top speed, and the feedback counters where the cart has them.
//   node games/jetski/tools/race-measure.mjs <cart> <course id> [--gl] [--turbo]
import { ROMDEV_URL } from '../../../tests/lib/romdev-url.mjs';
import { resolve } from 'node:path';
import { readCourse, COURSE_IDS } from './course-read.mjs';
const URL_ = ROMDEV_URL+'/mcp', CART = resolve(process.argv[2]), ID = process.argv[3], GL = process.argv.includes('--gl'), TURBO = process.argv.includes('--turbo'), C = readCourse(ID), STEP = 6;
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski' };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text(), line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
const call = async (name, args) => { const r = await rpc('tools/call', { name, arguments: args }); if (r.error || r.result?.isError) throw new Error(name + ' ' + JSON.stringify(r.error || r.result).slice(0, 300)); const c = r.result?.content?.[0]?.text; try { return JSON.parse(c); } catch { return c; } };
const rd = async (n) => { const v = await call('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const opt = async (n) => { try { return await rd(n); } catch { return null; } };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-measure', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski', 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
await call('frame', { op: 'step', frames: 30 });
await call('wasm', { op: 'command', text: `course ${COURSE_IDS.indexOf(ID)}` });
await call('wasm', { op: 'command', text: 'race' });
let lastAir = 0, held = 0, want = 0, wantT = 0, changes = 0, frames = 0, sum = 0, n = 0, top = 0, wasAir = false, before = 0, hops = [], ramps = [], stick = 0, delay = [], minutes = {}, near = 0, nearN = 0;
for (;;) {
  const p = await rd('p0.pos'), yaw = +(await rd('p0.yaw')), sp = +(await rd('p0.speed')), air = +(await rd('p0.air_time')), place = +(await rd('p0.place')), state = +(await rd('race.state')), nb = +(await rd('p0.next_buoy'));
  if (state === 3 || frames > 40000) break;
  const b = C.buoys[nb], T = C.track[Math.floor((b[3] / C.len) * C.track.length) % C.track.length], k = 1 - 4 / 8.5, ax = T[0] + (b[0] - T[0]) * k, az = T[1] + (b[1] - T[1]) * k;
  let err = Math.atan2(ax - p[0], az - p[2]) - yaw;
  while (err > Math.PI) err -= 2 * Math.PI;
  while (err < -Math.PI) err += 2 * Math.PI;
  delay.push(err); const late = delay.length > 2 ? delay.shift() : delay[0]; // 200 ms late, the stick in thirds
  stick = Math.max(-1, Math.min(1, Math.round(Math.max(-1, Math.min(1, -late * 2.4)) * 3) / 3));
  const ports = { south: true, axes: { lx: stick, ly: 0 }, right: stick > 0.2, left: stick < -0.2 };
  if (TURBO && frames % 300 === 0) ports.rshoulder = true;
  await call('input', { op: 'set', ports: [ports] });
  await call('frame', { op: 'step', frames: STEP });
  frames += STEP;
  if (state === 2) {
    if (!held) held = want = place;
    if (place !== want) want = place, wantT = frames;
    if (want !== held && frames - wantT >= 45) { held = want; changes++; const m = Math.floor(frames / 3600); minutes[m] = (minutes[m] || 0) + 1; }
    sum += sp; n++; top = Math.max(top, sp);
    const inAir = air > 0;
    if (inAir && !wasAir) before = sp;
    if (!inAir && wasAir && before > 8) (lastAir > 1.2 ? ramps : hops).push(sp / before);
    if (inAir) lastAir = air;
    wasAir = inAir;
  }
}

const mean = (v) => v.length ? v.reduce((a, b) => a + b, 0) / v.length : 0;
const out = { cart: CART.split('/').slice(-2).join('/'), course: ID, clock: +(await rd('race.clock')), place: +(await rd('p0.place')), passes: +(await rd('p0.passes')), misses: +(await rd('p0.misses')), dq: await opt('p0.dq'), held_place_changes: changes, by_minute: minutes, speed_mean_kmh: +(mean([sum / Math.max(n, 1)]) * 3.6).toFixed(1), speed_top_kmh: +(top * 3.6).toFixed(1), hops: hops.length, hop_keep_mean: +mean(hops).toFixed(3), hops_under_0_8: hops.filter((v) => v < 0.8).length, hop_keep_min: hops.length ? +Math.min(...hops).toFixed(3) : null, ramps: ramps.length, ramp_keep_mean: +mean(ramps).toFixed(3), meter: await opt('p0.meter'), stock: await opt('p0.stock'), turbos: await opt('p0.turbos') };
const fb = await opt('fb');
if (fb) { const K = ['pass', 'top', 'trick', 'land', 'miss', 'gain', 'lose', 'lap', 'final', 'wall', 'crash', 'charge', 'turbo']; out.answers = Object.fromEntries(K.map((k, i) => [k, fb.slice(i * 4, i * 4 + 4).join('/')])); out.sounds_per_min = +(fb.filter((_, i) => i % 4 === 1).reduce((a, b) => a + +b, 0) / (out.clock / 60)).toFixed(1); }
await call('input', { op: 'set', ports: [{}] });
console.log(JSON.stringify(out));
process.exit(0);
