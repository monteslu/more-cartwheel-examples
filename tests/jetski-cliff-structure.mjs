// A rock wall seen from a distance has big shapes, not only an even fine grain: for held cameras on a course, the contrast of 24 to 96 px forms against the contrast under 5 px,
// on rock pixels. The check cannot pass a repeated grain: a small tile cut from each picture and repeated over the rectangle must read low. It says nothing about beauty: look at the pictures.
//   node tests/jetski-cliff-structure.mjs [cart] [--expect-low: this cart is the control, it must fail]: GL and WebGPU on the one cart, pictures in out/cliff-structure
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { scaleShares } from '../games/jetski/tools/cliff-scale.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const URL_ = 'http://127.0.0.1:7331/mcp', args = process.argv.slice(2).filter((a) => !a.startsWith('--')), CART = resolve(args[0] || join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')), DIR = join(ROOT, 'out/cliff-structure'), LOW = process.argv.includes('--expect-low');
const SESSION = process.env.JETSKI_SESSION || 'jetski-cliff';
const FLOOR = 1.45; // released carts up to 0.15.0 measure 1.24 on Crater Atoll; the rock faces 1.68
// course index: [camera x y z, look x y z, rectangle x y w h]
const VIEWS = {
  0: [
    ['306 2.2 21 -300 14 200', [1000, 250, 860, 270]],
    ['306 2.2 21 -340 14 -60', [300, 230, 1000, 250]],
    ['120 2.2 110 -330 40 90', [60, 340, 860, 450]],
    ['-40 2.2 100 -300 40 330', [100, 100, 1700, 500]],
  ],
};
mkdirSync(DIR, { recursive: true });
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text(), line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
const call = async (name, args) => { const r = await rpc('tools/call', { name, arguments: args }); if (r.error || r.result?.isError) throw new Error(name + ' ' + JSON.stringify(r.error || r.result).slice(0, 300)); return r.result; };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-cliff-structure', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
let fail = 0;
for (const GL of [false, true]) {
console.log(GL ? 'GL' : 'WebGPU');
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const cmd = (text) => call('wasm', { op: 'command', text });
await call('frame', { op: 'step', frames: 20 });
await cmd('mode 1');
for (const [level, views] of Object.entries(VIEWS)) {
  await cmd(`course ${level}`); await cmd('race'); await call('frame', { op: 'step', frames: 30 });
  let sum = 0, tiled = 0;
  for (let v = 0; v < views.length; v++) {
    const [cam, rect] = views[v], path = join(DIR, `${GL ? 'gl' : 'wg'}-c${level}-v${v}.png`);
    await cmd('peek ' + cam);
    await call('frame', { op: 'stepAndShot', frames: 3, path });
    const im = readImage(path), s = scaleShares(im, ...rect);
    sum += s.ratio;
    // the same picture with a 40 px tile of its own rock repeated over the rectangle
    const [x, y, w, h] = rect, t = { w: im.w, h: im.h, rgba: Uint8Array.from(im.rgba) }, sx = x + (w >> 1), sy = y + (h >> 1);
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) for (let c = 0; c < 4; c++) t.rgba[4 * ((y + j) * im.w + x + i) + c] = im.rgba[4 * ((sy + j % 40) * im.w + sx + i % 40) + c];
    tiled += scaleShares(t, ...rect).ratio;
    console.log(`  course ${level} view ${v}: big ${s.big.toFixed(3)} fine ${s.fine.toFixed(3)} ratio ${s.ratio.toFixed(2)} (rock ${(s.rock * 100).toFixed(0)}%)`);
  }
  const mean = sum / views.length, tile = tiled / views.length, high = mean >= FLOOR;
  console.log(`${high !== LOW ? 'ok  ' : 'FAIL'} course ${level}: mean ratio ${mean.toFixed(2)}, floor ${FLOOR}${LOW ? ' (control: must be under)' : ''}; repeated tile reads ${tile.toFixed(2)}`);
  if (high === LOW) fail++;
  if (tile >= FLOOR) { console.log(`FAIL course ${level}: a repeated tile passes the check`); fail++; }
  await cmd('peek');
}
}
process.exit(fail ? 1 : 0);
