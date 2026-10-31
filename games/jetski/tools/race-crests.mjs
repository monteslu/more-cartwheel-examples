// The race camera in the biggest sea of each difficulty, on the open water of the course, throttle held: pictures every second
// and a count of near-black pixels on the water, outside the rider's box. Same moments on any cart, so two carts compare.
//   node games/jetski/tools/race-crests.mjs <cart> <out dir> [--gl]
import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { inflateSync } from 'node:zlib';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), DIR = resolve(process.argv[3]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-crests';
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
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-crests', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
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
function black(path) {
  const { w, h, ch, out } = png(path);
  let n = 0;
  for (let y = 300; y < h - 120; y++) for (let x = 0; x < w; x++) {
    if (x > w * 0.36 && x < w * 0.64 && y > h * 0.45) continue; // the rider and hull
    const i = (y * w + x) * ch;
    if (out[i] + out[i + 1] + out[i + 2] < 24) n++;
  }
  return n;
}
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const cmd = (text) => call('wasm', { op: 'command', text });
let worst = 0, total = 0;
for (const [d, name] of [[0, 'easy'], [1, 'normal'], [2, 'hard']]) {
  await cmd(`difficulty ${d}`);
  await cmd('swell 1');
  for (const s of [700, 1100, 1500, 1900]) {
    await cmd('race');
    await cmd(`go ${s}`);
    const row = [];
    for (let i = 0; i < 4; i++) {
      await call('input', { op: 'press', button: 'south', frames: 60 });
      const p = `${DIR}/${name}-${s}-${i}.png`;
      await call('frame', { op: 'stepAndShot', frames: 60, path: p });
      const n = black(p);
      row.push(n), total += n;
      if (n > worst) worst = n;
    }
    console.log(name, s, row.join(' '));
  }
}
console.log(`worst ${worst} total ${total}`);
process.exit(0);
