// One lap of a course from the race camera: a computer rider (the _auto cart) rides it with the real camera. Contact sheet pictures
// at even steps, and every frame's camera against the last: how hard it jumps, how often the hull leaves the picture low, how far under the water it gets.
//   node games/jetski/tools/lap-sweep.mjs <auto cart> <course index> <out dir> [frames] [--gl]
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), LEVEL = +process.argv[3], DIR = resolve(process.argv[4]), FRAMES = +process.argv[5] || 7200, GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-lap';
mkdirSync(DIR, { recursive: true });
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text();
  const line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
const call = async (name, args) => {
  const r = await rpc('tools/call', { name, arguments: args });
  if (r.error || r.result?.isError) throw new Error(name + ' ' + JSON.stringify(r.error || r.result).slice(0, 300));
  const c = r.result?.content?.[0]?.text;
  try { return c ? JSON.parse(c) : null; } catch { return c; }
};
const rd = async (n) => { const v = await call('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-lap', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
await call('frame', { op: 'step', frames: 10 });
await call('wasm', { op: 'command', text: `course ${LEVEL}` });
await call('wasm', { op: 'command', text: 'race' });
await call('frame', { op: 'step', frames: 240 }); // the countdown
const STEP = 6, shots = 36;
let prev = null, prevV = null, maxJump = 0, maxAcc = 0, big = 0, n = 0, maxSpeed = 0, sumSpeed = 0, air = 0, airMax = 0, stuck = 0, lastDist = -1e9, firstDist = null;
for (let f = 0; f < FRAMES; f += STEP) {
  const shot = Math.floor(f / (FRAMES / shots)) !== Math.floor((f - STEP) / (FRAMES / shots));
  if (shot) await call('frame', { op: 'stepAndShot', frames: STEP, path: `${DIR}/f${String(f).padStart(5, '0')}.png` });
  else await call('frame', { op: 'step', frames: STEP });
  const cam = await rd('cam.pos'), sp = +(await rd('p0.speed')), dist = +(await rd('p0.dist')), a = +(await rd('p0.air_time'));
  if (firstDist === null) firstDist = dist;
  if (prev) {
    const v = [(cam[0] - prev[0]) / (STEP / 60), (cam[1] - prev[1]) / (STEP / 60), (cam[2] - prev[2]) / (STEP / 60)];
    const jump = Math.hypot(cam[0] - prev[0], cam[1] - prev[1], cam[2] - prev[2]);
    if (jump > maxJump) maxJump = jump;
    if (prevV) { const acc = Math.hypot(v[0] - prevV[0], v[1] - prevV[1], v[2] - prevV[2]) / (STEP / 60); if (acc > maxAcc) maxAcc = acc; if (acc > 40) big++; }
    prevV = v;
  }
  prev = cam, n++;
  sumSpeed += sp; if (sp > maxSpeed) maxSpeed = sp;
  if (a > 0) air++;
  if (a > airMax) airMax = a;
  if (sp < 2 && f > 300) stuck++;
  lastDist = dist;
}
const rd2 = {};
for (const k of ['cam.under_ticks', 'p0.misses', 'p0.passes', 'p0.lap', 'p0.spills', 'p0.ramps', 'p0.shut_hits', 'p0.shortcuts', 'race.tide', 'race.swell', 'race.state', 'p0.sink_max', 'ramp.gap', 'p0.beached']) rd2[k] = await rd(k);
console.log(JSON.stringify({ frames: FRAMES, dist: +(lastDist - firstDist).toFixed(0), speed_mean: +(sumSpeed / n).toFixed(1), speed_max: +maxSpeed.toFixed(1), cam_jump_max_m_per_0_1s: +maxJump.toFixed(2), cam_acc_max: +maxAcc.toFixed(1), cam_acc_over_40: big, air_max: +airMax.toFixed(2), air_share: +(air / n).toFixed(3), slow_samples: stuck, ...rd2 }));
process.exit(0);
