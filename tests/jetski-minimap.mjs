// The minimap tells the truth and stays small. On every course on the cart, GL and WebGPU through romdev, at moments of a race held still: each racer's dot and the next buoy's
// where the course's own line puts them, its rectangle a small share of the picture, and only a part of that rectangle changed by it, so the water reads through (spray keeps moving in a held moment, so nothing is judged outside the rectangle).
// Controls: dots drawn 60 m off and a map three times the size must fail.
//   node tests/jetski-minimap.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync, readFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';
import { readCourse, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), URL_ = 'http://127.0.0.1:7331/mcp', SESSION = process.env.JETSKI_SESSION || 'jetski', OUT = join(ROOT, 'out/minimap');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc'))), BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const MAX_AREA = 0.035, MAX_COVER = 0.4, MAX_DOT_PX = 1.5;
mkdirSync(OUT, { recursive: true });
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text(), line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
const call = async (name, args) => { const r = await rpc('tools/call', { name, arguments: args }); if (r.error || r.result?.isError) throw new Error(name + ' ' + JSON.stringify(r.error || r.result).slice(0, 300)); const c = r.result?.content?.[0]?.text; try { return c ? JSON.parse(c) : null; } catch { return c; } };
const cmd = (text) => call('wasm', { op: 'command', text }), rd = async (n) => { const v = await call('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
function png(path) {
  const b = readFileSync(path), w = b.readUInt32BE(16), h = b.readUInt32BE(20), ch = b[25] === 6 ? 4 : 3, parts = [];
  for (let o = 8; o < b.length;) { const n = b.readUInt32BE(o); if (b.toString('latin1', o + 4, o + 8) === 'IDAT') parts.push(b.subarray(o + 8, o + 8 + n)); o += 12 + n; }
  const raw = inflateSync(Buffer.concat(parts)), out = Buffer.alloc(w * h * ch), s = w * ch;
  for (let y = 0; y < h; y++) {
    const f = raw[y * (s + 1)], r = y * (s + 1) + 1, d = y * s;
    for (let x = 0; x < s; x++) {
      const a = x >= ch ? out[d + x - ch] : 0, u = y ? out[d + x - s] : 0, c = y && x >= ch ? out[d + x - s - ch] : 0;
      let p = 0;
      if (f === 1) p = a; else if (f === 2) p = u; else if (f === 3) p = (a + u) >> 1;
      else if (f === 4) { const q = a + u - c, pa = Math.abs(q - a), pb = Math.abs(q - u), pc = Math.abs(q - c); p = pa <= pb && pa <= pc ? a : pb <= pc ? u : c; }
      out[d + x] = (raw[r + x] + p) & 255;
    }
  }
  return { w, h, ch, out };
}
function coverage(pa, pb, R) {
  const A = png(pa), B = png(pb);
  let inside = 0, outside = 0;
  for (let y = 0; y < A.h; y++) for (let x = 0; x < A.w; x++) {
    const a = (y * A.w + x) * A.ch, b = (y * B.w + x) * B.ch, d = Math.abs(A.out[a] - B.out[b]) + Math.abs(A.out[a + 1] - B.out[b + 1]) + Math.abs(A.out[a + 2] - B.out[b + 2]);
    if (d <= 24) continue;
    if (x >= R[0] - 16 && x < R[0] + R[2] + 16 && y >= R[1] - 16 && y < R[1] + R[3] + 16) inside++; else outside++;
  }
  return { cover: inside / (R[2] * R[3]), outside, screen: A.w * A.h };
}
/** where the course's line puts a world point in the map's rectangle */
const fitOf = (c, R) => { let a = 1e9, b = -1e9, e = 1e9, f = -1e9; for (const t of c.track) { a = Math.min(a, t[0]); b = Math.max(b, t[0]); e = Math.min(e, t[1]); f = Math.max(f, t[1]); } const sc = Math.min(R[2] / (b - a + 1e-3), R[3] / (f - e + 1e-3)), ox = R[0] + (R[2] - (b - a) * sc) / 2, oy = R[1] + (R[3] - (f - e) * sc) / 2; return (x, z) => [ox + (x - a) * sc, oy + (z - e) * sc]; };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-minimap', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
async function moment(c, level, s, tag, map, mode = 0) {
  await cmd(`mode ${mode}`); await cmd(`course ${level}`); await cmd(`map ${map}`); await cmd('buoy_shot 1 1 0'); await cmd('race'); await cmd(`go ${s}`);
  await call('input', { op: 'set', ports: [{ south: true }] });
  await call('frame', { op: 'step', frames: 40 });
  await cmd('buoy_shot 1 1 1');
  await call('frame', { op: 'stepAndShot', frames: 2, path: `${OUT}/${tag}-on.png` });
  await cmd('map 0');
  await call('frame', { op: 'stepAndShot', frames: 2, path: `${OUT}/${tag}-off.png` });
  await cmd(`map ${map}`);
  await call('frame', { op: 'step', frames: 2 });
  const R = (await rd('hud.map')).map(Number), dots = (await rd('hud.map_dots')).map(Number), nx = (await rd('hud.map_next')).map(Number), pos = (await rd('p0.pos')).map(Number), nb = +(await rd('p0.next_buoy'));
  await cmd('buoy_shot 1 1 0');
  await call('input', { op: 'set', ports: [{}] });
  const P = fitOf(c, R), me = P(pos[0], pos[2]), bu = P(c.buoys[nb][0], c.buoys[nb][1]);
  return { R, err: Math.hypot(me[0] - dots[0], me[1] - dots[1]), errBuoy: Math.hypot(bu[0] - nx[0], bu[1] - nx[1]), ...coverage(`${OUT}/${tag}-on.png`, `${OUT}/${tag}-off.png`, R) };
}
for (const backend of BACKENDS) {
  await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  await call('frame', { op: 'step', frames: 30 });
  for (const cid of COURSE_IDS) {
    const c = readCourse(cid), level = COURSE_IDS.indexOf(cid);
    // a championship and a time trial, which has no rivals: the map is in both
    for (const [k, mode] of [[0.2, 0], [0.6, 1]]) {
      const m = await moment(c, level, c.len * k, `${backend}-${cid}-${k}`, '1', mode);
      const area = (m.R[2] * m.R[3]) / m.screen;
      check(`${backend} ${cid} ${mode ? 'time trial' : 'championship'}: dots true`, m.err < MAX_DOT_PX && m.errBuoy < MAX_DOT_PX && m.cover > 0.01, `you ${m.err.toFixed(2)} px, next buoy ${m.errBuoy.toFixed(2)} px off`);
      check(`${backend} ${cid} ${mode ? 'time trial' : 'championship'}: small, and the water reads through`, area < MAX_AREA && m.cover < MAX_COVER, `${(area * 100).toFixed(1)}% of the picture, ${(m.cover * 100).toFixed(0)}% of its rectangle changed`);
    }
  }
  const c = readCourse('atoll'), skew = await moment(c, 0, c.len * 0.2, `${backend}-skew`, '1 1'), big = await moment(c, 0, c.len * 0.2, `${backend}-big`, '1 0 3');
  check(`${backend} control: dots 60 m off fail`, skew.err >= MAX_DOT_PX, `${skew.err.toFixed(1)} px off`);
  check(`${backend} control: three times the size fails`, (big.R[2] * big.R[3]) / big.screen >= MAX_AREA, `${((big.R[2] * big.R[3]) / big.screen * 100).toFixed(1)}% of the picture`);
  await cmd('map 1'); await cmd('mode 0');
}
process.exit(fail ? 1 : 0);
