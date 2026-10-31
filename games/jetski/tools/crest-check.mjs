// Do wave crests turn black? Runs a cart through romdev on the 890M, at several moments of the attract loop and of a race
// from the race camera, and counts near-black pixels on the water (rows from the horizon down). A build from before the
// water's fix is the control: it must count thousands.
//   node games/jetski/tools/crest-check.mjs <cart> <out dir> [--gl]
import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { inflateSync } from 'node:zlib';

const URL_ = 'http://127.0.0.1:7331/mcp';
const CART = resolve(process.argv[2]), DIR = resolve(process.argv[3]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-crest';
mkdirSync(DIR, { recursive: true });
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text();
  const line = t.split('\n').find((l) => l.startsWith('data:'));
  return line || t ? JSON.parse(line ? line.slice(5) : t) : null;
}
const call = async (name, args) => {
  const r = await rpc('tools/call', { name, arguments: args });
  if (r?.error || r?.result?.isError) throw new Error(JSON.stringify(r.error || r.result).slice(0, 300));
  const c = r?.result?.content?.[0]?.text;
  try { return c ? JSON.parse(c) : null; } catch { return c; }
};
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-crest', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });

// the 8-bit RGB(A) non-interlaced PNG the frame tool writes
function png(path) {
  const b = readFileSync(path);
  const w = b.readUInt32BE(16), h = b.readUInt32BE(20), ch = b[25] === 6 ? 4 : 3;
  const parts = [];
  for (let o = 8; o < b.length;) {
    const n = b.readUInt32BE(o), t = b.toString('latin1', o + 4, o + 8);
    if (t === 'IDAT') parts.push(b.subarray(o + 8, o + 8 + n));
    o += 12 + n;
  }
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
function black(path, y0, y1) {
  const { w, ch, out } = png(path);
  let n = 0;
  for (let y = y0; y < y1; y++) for (let x = 0; x < w; x++) {
    const i = (y * w + x) * ch;
    if (out[i] + out[i + 1] + out[i + 2] < 24) n++;
  }
  return n;
}

await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const counts = [];
let shot = 0;
async function moment(label, frames) {
  const p = `${DIR}/${label}.png`;
  await call('frame', { op: 'stepAndShot', frames, path: p });
  const n = black(p, 420, 700);
  counts.push(n);
  console.log(label, n);
  shot++;
}
for (let i = 0; i < 4; i++) await moment(`attract${i}`, 200);
await call('wasm', { op: 'command', text: 'race' }).catch(() => {});
for (let i = 0; i < 4; i++) await moment(`race${i}`, 150);
console.log(`worst ${Math.max(...counts)} total ${counts.reduce((a, b) => a + b, 0)} over ${shot} moments`);
process.exit(0);
