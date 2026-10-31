// The attract loop through romdev: a picture from each of its shots, in motion, and what the game counted about its camera
// over a whole cycle: ticks under the water, in land, with land between it and the craft (rubric J-PT4: all zero).
//   node games/jetski/tools/attract-check.mjs <cart> <out dir> [--gl]
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), DIR = resolve(process.argv[3]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-attract';
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
  if (r.error || r.result?.isError) throw new Error(JSON.stringify(r.error || r.result).slice(0, 300));
  const c = r.result?.content?.[0]?.text;
  try { return c ? JSON.parse(c) : null; } catch { return c; }
};
const read = async (name) => { const v = await call('wasm', { op: 'read', name }); return v.values ?? v.value; };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-attract', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
// --control: the camera ignores the swell, and the count of ticks with a crest over the hull must not stay zero
if (process.argv.includes('--control')) await call('wasm', { op: 'command', text: 'cam_clear 0' });
// one cycle is four 8 s shots; two pictures in each, 1 s apart, to see it move
for (let s = 0; s < 4; s++) {
  await call('frame', { op: 'stepAndShot', frames: s ? 420 : 240, path: `${DIR}/s${s}a.png` });
  await call('frame', { op: 'stepAndShot', frames: 60, path: `${DIR}/s${s}b.png` });
}
await call('frame', { op: 'step', frames: 480 });
const c = { under: await read('cam.attract_under'), land: await read('cam.attract_land'), blocked: await read('cam.attract_blocked'), hidden: await read('cam.attract_hidden'), tick: await read('race.tick'), state: await read('race.state') };
console.log(JSON.stringify(c));
process.exit(0);
