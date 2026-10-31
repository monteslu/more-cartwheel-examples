// Look at a course through romdev from the race camera: a run of pictures at several places on it, and the numbers of the walk.
// Every new course goes through this before anything else.
//   node games/jetski/tools/course-look.mjs <cart> <course index> <out dir> [--gl] [--at=100,600,...]
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), LEVEL = +process.argv[3], DIR = resolve(process.argv[4]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-look';
const AT = (process.argv.find((a) => a.startsWith('--at='))?.slice(5) || '100,500,900,1300,1700,2100,2500,2900').split(',').map(Number);
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
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-look', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const cmd = async (text) => { const r = await call('wasm', { op: 'command', text }); const t = r.content[0].text; if (t.includes('"error"')) console.log(text, t.slice(0, 200)); return t; };
await call('frame', { op: 'step', frames: 30 });
console.log((await cmd(`course ${LEVEL}`)).replace(/\s+/g, ' ').slice(0, 120));
for (const c of (process.env.LOOK_CMDS || '').split(';').filter(Boolean)) await cmd(c);
await call('frame', { op: 'stepAndShot', frames: 60, path: `${DIR}/attract.png` });
if (process.argv.includes('--arch')) {
  for (const [tide, tag] of [[2.6, 'high'], [-1.6, 'low']]) {
    await cmd('race'); await cmd(`tide ${tide}`); await cmd('arch 45');
    await call('input', { op: 'set', ports: [{ south: true }] });
    for (let k = 0; k < 6; k++) await call('frame', { op: 'stepAndShot', frames: 30, path: `${DIR}/arch-${tag}${k}.png` });
  }
  await cmd('tide');
}
for (const s of AT) {
  await cmd('race'); await cmd(`go ${s}`);
  await call('input', { op: 'set', ports: [{ south: true }] });
  await call('frame', { op: 'stepAndShot', frames: 90, path: `${DIR}/at${s}.png` });
}
process.exit(0);
