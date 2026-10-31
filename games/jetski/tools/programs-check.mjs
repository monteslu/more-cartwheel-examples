// Does play compile programs the warm-up left out? The count of compiled programs (debug field gpu.programs) right after the
// warm-up and after each thing a race does for the first time. Anything over the first number is a stall in the player's hands.
//   node games/jetski/tools/programs-check.mjs <cart> [--gl]
import { resolve } from 'node:path';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-programs';
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
const read = async (name) => { const v = await call('wasm', { op: 'read', name }); return +(v.values ?? v.value); };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-programs', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const cmd = (text) => call('wasm', { op: 'command', text });
const step = (frames) => call('frame', { op: 'step', frames });
let base = 0, last = -1, frame = 0;
const mark = async (what) => { const p = await read('gpu.programs'); console.log(`${String(p).padStart(4)} ${p > base ? '+' + (p - base) : '  '} ${what}`); };
// frame by frame through the warm-up, then in small steps: the frame each new program arrives
for (let i = 0; i < +(process.env.WATCH || 700); i++) {
  const n = i < 40 ? 1 : 10;
  await step(n), frame += n;
  const p = await read('gpu.programs');
  if (p !== last) { console.log(`frame ${frame}: ${p} programs`); last = p; }
  if (i === 39) base = p;
}
base = base || last;
console.log('at frame 40:', base);
await mark('attract, every shot');
await cmd('race'); await step(240); await mark('race start and countdown');
await call('input', { op: 'press', button: 'south', frames: 300 }); await step(300); await mark('under way');
await cmd('drop 0.1 4 24'); await step(120); await mark('a big landing');
await cmd('drop -2.6 3 22'); await step(180); await mark('a spill');
await cmd('beach 300'); await step(120); await mark('on the sand');
await cmd('go 1500'); await call('input', { op: 'press', button: 'south', frames: 300 }); await step(300); await mark('the far side of the course');
await cmd('finish'); await step(60); await mark('results');
process.exit(0);
