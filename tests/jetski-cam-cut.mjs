// A race start never shows the camera under the sea. Race to the next course by the real flow through romdev on the cart: a race, the finish, the results page's own accept, the next course's first frames.
// Counts the frames whose drawn camera is under the water. Control: cam_cut 0, the camera drawn between a zeroed place and its first, must show it.
//   node tests/jetski-cam-cut.mjs [--gl]
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), U = 'http://127.0.0.1:7331/mcp', H = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski' };
let sid = null, id = 1;
async function rpc(m, p) { const h = { ...H }; if (sid) h['mcp-session-id'] = sid; const r = await fetch(U, { method: 'POST', headers: h, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method: m, params: p }) }); if (!sid) sid = r.headers.get('mcp-session-id'); const t = await r.text(), l = t.split('\n').find((x) => x.startsWith('data:')); return JSON.parse(l ? l.slice(5) : t); }
const call = async (n, a) => { const r = await rpc('tools/call', { name: n, arguments: a }); if (r.error || r.result?.isError) throw new Error(n + ' ' + JSON.stringify(r.error || r.result).slice(0, 300)); const c = r.result?.content?.[0]?.text; try { return JSON.parse(c); } catch { return c; } };
const rd = async (n) => { const v = await call('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (t) => call('wasm', { op: 'command', text: t });
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-cam', version: '1' } });
await fetch(U, { method: 'POST', headers: { ...H, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: join(ROOT, 'build/cart-wgpu/carts/jetski.wasc'), gpuPower: 'low-power', ...(process.argv.includes('--gl') ? { wgpu: false } : {}) });
await call('frame', { op: 'step', frames: 20 });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
async function nextRaces(cut) {
  await cmd(`sw cam_cut ${cut}`); await cmd('mode 0'); await cmd('course 0'); await cmd('race');
  const seen = [];
  for (let k = 0; k < 4; k++) {
    await call('frame', { op: 'step', frames: 90 });
    await cmd('finish');
    await call('frame', { op: 'step', frames: 30 });
    const before = +(await rd('cam.drawn_under')), lv0 = +(await rd('race.level'));
    // accept on the results page, by the pad
    for (let t = 0; t < 12 && +(await rd('race.level')) === lv0; t++) { await call('input', { op: 'press', button: 'south', frames: 3 }); await call('frame', { op: 'step', frames: 12 }); }
    await call('frame', { op: 'step', frames: 6 });
    seen.push({ level: +(await rd('race.level')), state: +(await rd('race.state')), under: +(await rd('cam.drawn_under')) - before });
  }
  return seen;
}
const on = await nextRaces(1);
check('from one race to the next, no frame under the sea', on.every((s) => s.under === 0) && new Set(on.map((s) => s.level)).size >= 3, JSON.stringify(on));
const off = await nextRaces(0);
console.log('     control, per race:', JSON.stringify(off));
await cmd('sw cam_cut 1');
await cmd('race'); await call('frame', { op: 'step', frames: 90 }); await cmd('course 1');
const a = +(await rd('cam.drawn_under')); await cmd('race'); await call('frame', { op: 'step', frames: 2 });
const b = +(await rd('cam.drawn_under')); await cmd('sw cam_cut 0'); await cmd('course 0'); await cmd('race'); await call('frame', { op: 'step', frames: 90 }); await cmd('course 1');
const c = +(await rd('cam.drawn_under')); await cmd('race'); await call('frame', { op: 'step', frames: 2 }); const d = +(await rd('cam.drawn_under'));
check('the debug path too', b - a === 0, `${b - a} frames`);
check('control: cam_cut 0 is under the sea on one or the other', off.some((s) => s.under > 0) || d - c > 0, `flow ${off.map((s) => s.under).join(' ')}, debug path ${d - c}`);
process.exit(fail ? 1 : 0);
