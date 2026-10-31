// A picture of a ramp at the worst moment found: the biggest sea, watching every ramp's belly against the water, with a held camera
// beside the ramp at water level, shot again each time the record rises. The last picture is the worst moment of the run.
//   node games/jetski/tools/ramp-worst.mjs <cart> <course> <difficulty> <out png> [frames] [--gl]
import { copyFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), LEVEL = +process.argv[3], DIFF = +process.argv[4], OUT = resolve(process.argv[5]), FRAMES = +process.argv[6] || 2400, GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-ramp-worst';
mkdirSync(dirname(OUT), { recursive: true });
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
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-ramp-worst', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
await call('frame', { op: 'step', frames: 20 });
await cmd(`course ${LEVEL}`);
await cmd(`difficulty ${DIFF}`);
await cmd('swell 1');
await cmd('race');
await call('frame', { op: 'step', frames: 240 });
let worst = -9, at = -1, ramp = -1;
const tmp = `${OUT}.tmp.png`;
for (let f = 0; f < FRAMES; f += 6) {
  await call('frame', { op: 'step', frames: 6 });
  const now = await rd('ramp.now');
  for (let r = 0; r < 4; r++) if (now[r] > worst + 0.005) { // (both courses have four; a slot past the last holds 0)
    worst = now[r], at = f, ramp = r;
    const pos = await cmd(`ramp_where ${r}`);
    const p = JSON.parse(pos.content ? pos.content[0].text : JSON.stringify(pos)).reply ?? pos.reply;
    await cmd(`peek ${p.x + p.sy * -14 + p.cy * 9} ${p.h + 1.2} ${p.z + p.cy * -14 - p.sy * 9} ${p.x} ${p.h + 0.4} ${p.z}`);
    await call('frame', { op: 'stepAndShot', frames: 1, path: tmp });
    await cmd('peek');
    copyFileSync(tmp, OUT);
  }
}
console.log(JSON.stringify({ course: LEVEL, difficulty: DIFF, frames: FRAMES, worst_belly_m: +worst.toFixed(3), at_frame: at, ramp, belly_max_field: await rd('ramp.belly_max'), gap_points: await rd('ramp.gap_points'), points: await rd('ramp.points') }));
process.exit(0);
