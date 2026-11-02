// A ride off a big ramp by the pad through romdev, pictures every few frames from the race camera: what the stick and buttons do in the air, and how a dive looks. Reads what the game counted.
//   node games/jetski/tools/air-look.mjs <cart> <course id> <out dir> [--gl] [--only=plain,backflip,frontflip,kick,dive,tail]
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { readCourse, COURSE_IDS } from './course-read.mjs';
const U = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), ID = process.argv[3], DIR = resolve(process.argv[4]), GL = process.argv.includes('--gl'), H = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski' };
mkdirSync(DIR, { recursive: true });
let sid = null, id = 1;
async function rpc(m, p) { const h = { ...H }; if (sid) h['mcp-session-id'] = sid; const r = await fetch(U, { method: 'POST', headers: h, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method: m, params: p }) }); if (!sid) sid = r.headers.get('mcp-session-id'); const t = await r.text(), l = t.split('\n').find((x) => x.startsWith('data:')); return JSON.parse(l ? l.slice(5) : t); }
const call = async (n, a) => { const r = await rpc('tools/call', { name: n, arguments: a }); if (r.error || r.result?.isError) throw new Error(n + ' ' + JSON.stringify(r.error || r.result).slice(0, 300)); const c = r.result?.content?.[0]?.text; try { return JSON.parse(c); } catch { return c; } };
const rd = async (n) => { const v = await call('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (t) => call('wasm', { op: 'command', text: t });
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-air', version: '1' } });
await fetch(U, { method: 'POST', headers: { ...H, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
await call('frame', { op: 'step', frames: 20 });
const C = readCourse(ID), r = C.ramps.findIndex((q) => q[5] > 0), lv = COURSE_IDS.indexOf(ID);
await cmd(`course ${lv}`);
// name, what the pad does once in the air
const RIDES = [['plain', () => ({})], ['backflip', (t) => (t < 0.2 ? { west: true, axes: { lx: 0, ly: 1 } } : t < 0.8 ? { axes: { lx: 0, ly: 1 } } : {})], ['frontflip', (t) => (t < 0.2 ? { west: true, axes: { lx: 0, ly: -1 } } : t < 0.8 ? { axes: { lx: 0, ly: -1 } } : {})], ['kick', (t) => (t < 0.2 ? { north: true } : {})], ['dive', () => ({ axes: { lx: 0, ly: -1 } })], ['tail', () => ({ axes: { lx: 0, ly: 1 } })]];
const ONLY = process.argv.find((a) => a.startsWith("--only="))?.slice(7);
for (const [name, pad] of RIDES.filter(([n]) => !ONLY || ONLY.split(",").includes(n))) {
  await cmd('race'); await call('frame', { op: 'step', frames: 4 }); await cmd(`ramp ${r} 50`);
  const shots = [];
  let flying = -1, f = 0, done = 0;
  for (; f < 900 && done < 270; f += 6) {
    // flying: off the ramp and well over the water, not a hop on the swell of the run-up
    const air = +(await rd('p0.air_time'));
    if (air > 0.05 && flying < 0 && (await rd('p0.pos'))[1] - +(await rd('p0.surf')) > 1.5) flying = f;
    const p = flying >= 0 ? pad((f - flying) / 60) : {};
    await call('input', { op: 'set', ports: [{ south: true, ...p }] });
    if (flying >= 0) { const path = `${DIR}/${name}-${String(done).padStart(3, '0')}.png`; await call('frame', { op: 'stepAndShot', frames: 6, path }); shots.push(path); done += 6; } else await call('frame', { op: 'step', frames: 6 });
  }
  await call('input', { op: 'set', ports: [{}] });
  console.log(GL ? 'gl' : 'webgpu', name, 'tricks', await rd('p0.tricks'), 'kicks', await rd('p0.kicks'), 'spills', await rd('p0.spills'), 'dives', await rd('p0.dives'), 'depth', (+(await rd('p0.dive_max'))).toFixed(2), 'under', (+(await rd('p0.dive_under'))).toFixed(2), 'keep', (+(await rd('p0.land_keep'))).toFixed(2), 'air_max', (+(await rd('p0.air_max'))).toFixed(2));
  const pick = shots.filter((_, i) => i % 5 === 0).slice(0, 9);
  spawnSync('montage', [...pick, '-geometry', '640x360+2+2', '-tile', '3x3', '-quality', '86', `${DIR}/${name}.jpg`]);
}
process.exit(0);
