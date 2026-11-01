// Play the loaded jetski cart through romdev like a person with a pad: reads where the craft is, steers with the stick,
// holds the throttle, aims for the correct side of each buoy, throws a trick in long air. Takes a shot at chosen moments.
//   node games/jetski/tools/play-romdev.mjs <frames> <shot dir> [--wrong-side] [--course=<id>] [--gl] [--tricks] [--shots=f,f] [--burst=f]
// --course: that course, by the menu's own start. --burst: eight frames in a row from there, to see it move. Reports how hard the camera moved
import { mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCourse, COURSE_IDS } from './course-read.mjs';
const HERE = dirname(fileURLToPath(import.meta.url)), URL_ = 'http://127.0.0.1:7331/mcp';
const FRAMES = +process.argv[2] || 1200, DIR = resolve(process.argv[3] || 'out/play'), WRONG = process.argv.includes('--wrong-side');
mkdirSync(DIR, { recursive: true });
const COURSE = process.argv.find((v) => v.startsWith('--course='))?.slice(9) || 'atoll', C = readCourse(COURSE), B = C.buoys, T = C.track;
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski' };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text();
  const line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
const call = async (name, args) => { const r = await rpc('tools/call', { name, arguments: args }); if (r.error) throw new Error(JSON.stringify(r.error)); const tx = r.result.content?.[0]?.text; try { return JSON.parse(tx); } catch { return tx; } };
const rd = async (name) => { const v = await call('wasm', { op: 'read', name }); return v.values ?? v.value; };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-play', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });

// loaded fresh by this connection, on the 890M: a server host belongs to its MCP session
const CART = resolve(HERE, '../../../build/cart-wgpu/carts/jetski.wasc');
const useGl = process.argv.includes('--gl');
const lm = await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(useGl ? { wgpu: false } : {}) });
console.log('load', String(JSON.stringify(lm ?? null)).slice(0, 300));
await call('frame', { op: 'step', frames: 30 });
const state = await rd('race.state');
console.log('state', JSON.stringify(state));
if (+state === 0 || state?.[0] === 0) { await call('input', { op: 'press', button: 'south', frames: 3 }); }
const log = [];
const CMD = process.argv.find((a) => a.startsWith('--cmd='))?.slice(6) || `course ${COURSE_IDS.indexOf(COURSE)};race`;
for (const c of CMD.split(';')) console.log('cmd', JSON.stringify(await call('wasm', { op: 'command', text: c })));
let frame = 0, nextShot = 0, shots = 0, maxAir = 0, lastLog = 0, trick = 0, wall = Date.now();
const STEP = 4;
const SHOTS = (process.argv.find((a) => a.startsWith('--shots='))?.slice(8) || '900,1800').split(',').map(Number);
const BURST = +process.argv.find((a) => a.startsWith('--burst='))?.slice(8) || -1;
let camPrev = null, camV = null, camAccMax = 0, camBig = 0, camJump = 0, hit = 0, lastPass = 0, lastMoved = 0, lastPos = null, stuckMax = 0;
while (frame < FRAMES) {
  const p = await rd('p0.pos'), yaw = +(await rd('p0.yaw')), speed = +(await rd('p0.speed')), air = +(await rd('p0.air_time'));
  const ti = +(await rd('p0.track_i')), passes = +(await rd('p0.passes')), misses = +(await rd('p0.misses')), meter = +(await rd('p0.meter'));
  // the buoy ahead: the next one along the course within reach, passed on its correct side
  const s = (ti / T.length) * C.len;
  const b = B[+(await rd('p0.next_buoy'))];
  const side = WRONG ? -1 : 1;
  // aim 4 m on the line's side of it (the wrong-side run aims 6 m past it)
  const k = WRONG ? 1 + 6 / 8.5 : 1 - 4 / 8.5, t0 = T[Math.floor((b[3] / C.len) * T.length) % T.length];
  const ax = t0[0] + (b[0] - t0[0]) * k, az = t0[1] + (b[1] - t0[1]) * k;
  const dx = ax - p[0], dz = az - p[2];
  let err = Math.atan2(dx, dz) - yaw;
  while (err > Math.PI) err -= 2 * Math.PI;
  while (err < -Math.PI) err += 2 * Math.PI;
  const lx = Math.max(-1, Math.min(1, -err * 2.4));
  const buttons = { south: true };
  if (process.argv.includes('--tricks') && air > 0.1 && air < 0.2) trick = 1;
  if (trick > 0) { buttons.west = true; trick = air > 0.3 ? 0 : trick; }
  await call('input', { op: 'set', ports: [{ ...buttons, axes: { lx, ly: Math.abs(err) > 0.7 ? 0.9 : 0 }, right: lx > 0.2, left: lx < -0.2, down: Math.abs(err) > 0.7 }] });
  const shoot = (air > 0.55 && frame >= nextShot) || SHOTS.includes(frame) || (BURST >= 0 && frame >= BURST && frame < BURST + 8 * STEP);
  if (shoot) { await call('frame', { op: 'stepAndShot', frames: STEP, path: join(DIR, `f${frame}-air${air.toFixed(1)}.png`) }); nextShot = frame + 240; shots++; } else await call('frame', { op: 'step', frames: STEP });
  frame += STEP;
  const cam = await rd('cam.pos');
  if (camPrev) {
    const dt = STEP / 60, v = [(cam[0] - camPrev[0]) / dt, (cam[1] - camPrev[1]) / dt, (cam[2] - camPrev[2]) / dt];
    camJump = Math.max(camJump, Math.hypot(cam[0] - camPrev[0], cam[1] - camPrev[1], cam[2] - camPrev[2]));
    if (camV) { const a = Math.hypot(v[0] - camV[0], v[1] - camV[1], v[2] - camV[2]) / dt; camAccMax = Math.max(camAccMax, a); if (a > 40) camBig++; }
    camV = v;
  }
  camPrev = cam;
  // stuck: no more than 3 m made good for this long, throttle held
  if (!lastPos || Math.hypot(p[0] - lastPos[0], p[2] - lastPos[2]) > 3) lastPos = p, lastMoved = frame;
  stuckMax = Math.max(stuckMax, frame - lastMoved);
  if (air > maxAir) maxAir = air;
  if (frame - lastLog >= 240) { lastLog = frame; console.log(`f${frame} s${s.toFixed(0)} pos ${p[0].toFixed(0)},${p[2].toFixed(0)} v ${speed.toFixed(1)} meter ${meter} pass ${passes} miss ${misses} maxAir ${maxAir.toFixed(2)} ms/frame ${((Date.now() - wall) / 240).toFixed(1)}`); wall = Date.now(); }
}
const end = await Promise.all(['race.state', 'race.clock', 'p0.place', 'p0.lap', 'p0.passes', 'p0.misses', 'p0.dq', 'menu.page', 'p0.meter', 'cam.under_ticks', 'race.swell'].map(async (n) => [n, await rd(n)]));
await call('input', { op: 'set', ports: [{}] });
console.log(JSON.stringify({ ...Object.fromEntries(end), cam_acc_max: +camAccMax.toFixed(1), cam_acc_over_40: camBig, cam_jump_max: +camJump.toFixed(2), longest_without_moving_frames: stuckMax, air_max: +maxAir.toFixed(2) }), 'shots', shots);
