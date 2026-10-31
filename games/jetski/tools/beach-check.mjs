// Beach the human with the debug command and drive off with a pad through romdev, like a person: throttle held, stick turned
// for the water. Prints where the craft went. node beach-check.mjs [metres] [--idle]
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const HERE = dirname(fileURLToPath(import.meta.url)), URL_ = 'http://127.0.0.1:7331/mcp';
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': 'jetski' };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text(), line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
const call = async (name, args) => { const r = await rpc('tools/call', { name, arguments: args }); const tx = r.result?.content?.[0]?.text; try { return JSON.parse(tx); } catch { return tx; } };
const rd = async (n) => { const v = await call('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-beach', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: resolve(HERE, '../../../build/cart-wgpu/carts/jetski.wasc'), gpuPower: 'low-power' });
await call('frame', { op: 'step', frames: 30 });
await call('input', { op: 'press', button: 'south', frames: 3 });
await call('frame', { op: 'step', frames: 10 });
const place = await call('wasm', { op: 'command', text: 'beach ' + (process.argv[2] || 300) });
console.log('placed', JSON.stringify(place.reply));
const idle = process.argv.includes('--idle');
await call('input', { op: 'set', ports: [idle ? {} : { south: true, right: true, axes: { lx: 1 } }] });
for (let f = 0; f < 8; f++) {
  await call('frame', { op: 'step', frames: 60 });
  const p = await rd('p0.pos'), yaw = await rd('p0.yaw'), sp = await rd('p0.speed'), b = await rd('p0.beached'), e = await rd('p0.escapes');
  console.log(`t ${f + 1}s pos ${p[0].toFixed(1)},${p[2].toFixed(1)} y ${p[1].toFixed(2)} yaw ${(+yaw).toFixed(2)} v ${(+sp).toFixed(1)} beached ${b} escapes ${e}`);
}
await call('input', { op: 'set', ports: [{}] });
