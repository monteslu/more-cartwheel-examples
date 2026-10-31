// Shader warm-up, measured through romdev: wall time of the load and first frame (the whole warm-up, on a deterministic load),
// then every frame of the attract loop and of the start of a race, naming the slow ones. Two loads in a row: does the second
// compile less?
//   node games/jetski/tools/warm-check.mjs <cart> [--gl]
import { resolve } from 'node:path';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-warm';
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
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-warm', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
const timed = async (f) => { const t = performance.now(); await f(); return performance.now() - t; };
const step = () => timed(() => call('frame', { op: 'step', frames: 1 }));
const report = (name, ms) => {
  const s = [...ms].sort((a, b) => a - b), med = s[s.length >> 1];
  const slow = ms.map((v, i) => [i, v]).filter(([, v]) => v > Math.max(med * 3, med + 25)).map(([i, v]) => `${i}:${v.toFixed(0)}`);
  console.log(`  ${name}: median ${med.toFixed(1)} ms, worst ${s[s.length - 1].toFixed(0)} ms, slow frames ${slow.join(' ') || 'none'}`);
};
for (let run = 1; run <= 2; run++) {
  console.log(`load ${run}`);
  const load = await timed(() => call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) }));
  const first = await step();
  console.log(`  load ${load.toFixed(0)} ms, first frame (warm-up) ${first.toFixed(0)} ms`);
  const a = [];
  for (let i = 0; i < 90; i++) a.push(await step());
  report('attract', a);
  await call('input', { op: 'press', button: 'south', frames: 3 });
  const r = [];
  for (let i = 0; i < 360; i++) r.push(await step());
  report('race start', r);
}
process.exit(0);
