// A jump from the race camera, throttle held: from far off, on the approach, on the deck, in the air, the landing. Native-scale pictures.
//   node games/jetski/tools/ramp-look.mjs <cart> <course> <ramp> <out dir> [--gl] [--tide=x] [--back=m]
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), LEVEL = +process.argv[3], RAMP = +process.argv[4], DIR = resolve(process.argv[5]), GL = process.argv.includes('--gl');
const opt = (k, d) => { const a = process.argv.find((x) => x.startsWith(`--${k}=`)); return a ? a.split('=')[1] : d; };
const SESSION = process.env.JETSKI_SESSION || 'jetski-ramp-look';
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
const cmd = (text) => call('wasm', { op: 'command', text });
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-ramp-look', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
await call('frame', { op: 'step', frames: 20 });
await cmd(`course ${LEVEL}`);
await cmd(`tide ${opt('tide', '')}`);
await cmd('race');
await cmd(`ramp ${RAMP} ${opt('back', '170')}`);
await call('input', { op: 'set', ports: [{ south: true }] });
const log = [], EVERY = +opt('every', 30); await call('frame', { op: 'step', frames: +opt('skip', 1) });
for (let i = 0; i < +opt('n', 26); i++) {
  await call('frame', { op: 'stepAndShot', frames: EVERY, path: `${DIR}/j${String(i).padStart(2, '0')}.png` });
  const p = await rd('p0.pos');
  log.push(`${i}: y ${(+p[1]).toFixed(1)} spd ${(+(await rd('p0.speed'))).toFixed(1)} air ${(+(await rd('p0.air_time'))).toFixed(2)} ramps ${await rd('p0.ramps')} cam ${(await rd('cam.pos'))[1].toFixed(1)} surf ${(+(await rd('p0.surf'))).toFixed(1)} tide ${(+(await rd('race.tide'))).toFixed(1)} under ${await rd('cam.under_ticks')} keep ${(+(await rd('p0.land_keep'))).toFixed(2)}`);
}
console.log(log.join('\n'));
await call('input', { op: 'set', ports: [{}] });
process.exit(0);
