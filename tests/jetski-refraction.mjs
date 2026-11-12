// The marks do not come through the water. Lime on the sea at one held moment of a championship race on Crater Atoll, and then along the start of the race
// through the marks, the floats and the rivals' wakes (a picture every 30 ticks from tick 300 to 660, the rivals racing through them),
// on the cart through romdev, GL and WebGPU. Everything in the game draws in the water's refraction (the see-through water), so the lit floats and
// marks are in it, and the engine's cap on the refraction's colour is what keeps them from showing as lime. On v0.14.0's WebGPU the marks' lit
// floats reached the refraction and showed through rivals' wakes as patches of saturated lime (17,802 pixels, GL about 100). Control: that release
// must fail on WebGPU, at the held moment and along the ride.
//   node tests/jetski-refraction.mjs
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/refraction'), U = ROMDEV_URL+'/mcp', MAX = 1000;
mkdirSync(OUT, { recursive: true });
const CONTROL = join(ROOT, 'build/control/v0.14.0/jetski.wasc'); mkdirSync(dirname(CONTROL), { recursive: true });
if (!existsSync(CONTROL)) { const r = spawnSync('gh', ['release', 'download', 'v0.14.0', '-R', 'monteslu/more-cartwheel-examples', '-p', 'jetski.wasc', '-D', dirname(CONTROL)], { encoding: 'utf8' }); if (r.status) { console.error(r.stderr); process.exit(3); } }
let sid, id;
const H = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski' };
async function rpc(m, p) { const h = { ...H }; if (sid) h['mcp-session-id'] = sid; const r = await fetch(U, { method: 'POST', headers: h, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method: m, params: p }) }); if (!sid) sid = r.headers.get('mcp-session-id'); const t = await r.text(), l = t.split('\n').find((x) => x.startsWith('data:')); return JSON.parse(l ? l.slice(5) : t); }
const call = async (n, a) => { const r = await rpc('tools/call', { name: n, arguments: a }); if (r.error || r.result?.isError) throw new Error(n + ' ' + JSON.stringify(r.error || r.result).slice(0, 300)); return r.result; };
function png(p) { const b = readFileSync(p), w = b.readUInt32BE(16), h = b.readUInt32BE(20), ch = b[25] === 6 ? 4 : 3, ps = []; for (let o = 8; o < b.length;) { const n = b.readUInt32BE(o); if (b.toString('latin1', o + 4, o + 8) === 'IDAT') ps.push(b.subarray(o + 8, o + 8 + n)); o += 12 + n; } const raw = inflateSync(Buffer.concat(ps)), out = Buffer.alloc(w * h * ch), s = w * ch; for (let y = 0; y < h; y++) { const f = raw[y * (s + 1)], r = y * (s + 1) + 1, d = y * s; for (let x = 0; x < s; x++) { const a = x >= ch ? out[d + x - ch] : 0, u = y ? out[d + x - s] : 0, c = y && x >= ch ? out[d + x - s - ch] : 0; let q = 0; if (f === 1) q = a; else if (f === 2) q = u; else if (f === 3) q = (a + u) >> 1; else if (f === 4) { const e = a + u - c, pa = Math.abs(e - a), pb = Math.abs(e - u), pc = Math.abs(e - c); q = pa <= pb && pa <= pc ? a : pb <= pc ? u : c; } out[d + x] = (raw[r + x] + q) & 255; } } return { w, h, ch, out }; }
const lime = (p) => { const { w, h, ch, out } = png(p); let n = 0; for (let y = 540; y < h; y++) for (let x = 0; x < w; x++) { const k = (y * w + x) * ch, r = out[k], g = out[k + 1], b = out[k + 2]; if (g > 170 && r > 150 && b < 170 && g - b > 45 && g >= r) n++; } return n; };
async function measure(cart, gl, tag) {
  sid = null, id = 1;
  await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-refraction', version: '1' } });
  await fetch(U, { method: 'POST', headers: { ...H, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
  await call('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(gl ? { wgpu: false } : {}) });
  const cmd = (t) => call('wasm', { op: 'command', text: t });
  await call('frame', { op: 'step', frames: 20 }); await cmd('mode 0'); await cmd('course 0'); await cmd('race');
  await call('frame', { op: 'step', frames: 360 }); await cmd('buoy_shot 1 1 1');
  const p = join(OUT, tag + '.png');
  await call('frame', { op: 'stepAndShot', frames: 3, path: p });
  await cmd('buoy_shot 1 1 0');
  return lime(p);
}
/* the ride: a picture every 30 ticks, the human at rest at the grid, the rivals racing past the floats */
async function ride(cart, gl, tag) {
  sid = null, id = 1;
  await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-refraction', version: '1' } });
  await fetch(U, { method: 'POST', headers: { ...H, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
  await call('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(gl ? { wgpu: false } : {}) });
  const cmd = (t) => call('wasm', { op: 'command', text: t });
  await call('frame', { op: 'step', frames: 20 }); await cmd('mode 0'); await cmd('course 0'); await cmd('race');
  await call('frame', { op: 'step', frames: 300 });
  const counts = [];
  for (let i = 0; i < 12; i++) {
    const p = join(OUT, `${tag}-ride-${i}.png`);
    await call('frame', { op: 'stepAndShot', frames: 30, path: p });
    counts.push(lime(p));
  }
  return counts;
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const CART = join(ROOT, 'build/cart-wgpu/carts/jetski.wasc');
for (const gl of [true, false]) { const n = await measure(CART, gl, gl ? 'gl' : 'webgpu'); check(`${gl ? 'GL' : 'WebGPU'}: no lime on the sea`, n < MAX, `${n} pixels, bound ${MAX}`); }
const c = await measure(CONTROL, false, 'control');
check('control: v0.14.0 on WebGPU has it', c >= MAX, `${c} pixels`);
for (const gl of [true, false]) { const n = await ride(CART, gl, gl ? 'gl' : 'webgpu'); check(`${gl ? 'GL' : 'WebGPU'}: no lime along the ride`, Math.max(...n) < MAX, `most ${Math.max(...n)} pixels in a picture of ${n.length} (${n.join(' ')}), bound ${MAX}`); }
const rc = await ride(CONTROL, false, 'control');
check('control: v0.14.0 on WebGPU has it along the ride', Math.max(...rc) >= MAX, `most ${Math.max(...rc)} pixels (${rc.join(' ')})`);
process.exit(fail ? 1 : 0);
