// A picture of each menu page from the cart through romdev, reached with the pad, to look at.
//   node games/jetski/tools/menu-shots.mjs <cart> <out dir> [--gl]
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), DIR = resolve(process.argv[3]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-menu';
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
  return r.result;
};
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-menu', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const tap = async (b) => { await call('input', { op: 'press', button: b, frames: 3 }); await call('frame', { op: 'step', frames: 4 }); };
const shot = (n, frames = 6) => call('frame', { op: 'stepAndShot', frames, path: `${DIR}/${n}.png` });
await shot('main', 200);
for (const b of ['down', 'down', 'down', 'down', 'south']) await tap(b);
await shot('controls');
await tap('east');
await tap('down');
await tap('south');
await shot('audio');
await tap('east');
for (const b of ['up', 'up', 'up', 'up', 'right']) await tap(b);
await shot('timetrial');
await tap('left');
await tap('up');
await tap('south');
await shot('countdown', 60);
await call('frame', { op: 'step', frames: 300 });
await tap('start');
await shot('pause');
await tap('east');
await call('wasm', { op: 'command', text: 'finish' });
await shot('results', 20);
process.exit(0);
