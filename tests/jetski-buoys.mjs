// A rider can see the next buoy from far off. On every course, from the real race camera on the cart through romdev, GL and WebGPU: the moment held still, a picture
// with the buoys and one without. What differs above the waterline is what of the buoy the eye gets: its height on screen, its area, how far it stands from what
// is behind it. Control: buoy_style 0, the small buoy this replaced, must fail the same numbers. Pass side is on the board and in the HUD; scoring is not touched.
//   node tests/jetski-buoys.mjs [--gl] [--webgpu] [--cart <path>] [--shots <dir>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { readFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';
import { readCourse, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), URL_ = ROMDEV_URL+'/mcp', SESSION = process.env.JETSKI_SESSION || 'jetski';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc'))), OUT = resolve(arg('--shots', join(ROOT, 'out/buoys')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const NEED = { 100: { h: 60, area: 700, contrast: 40 }, 150: { h: 40, area: 300, contrast: 40 } };
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
const call = async (name, args) => {
  const r = await rpc('tools/call', { name, arguments: args });
  if (r.error || r.result?.isError) throw new Error(name + ' ' + JSON.stringify(r.error || r.result).slice(0, 300));
  const c = r.result?.content?.[0]?.text;
  try { return c ? JSON.parse(c) : null; } catch { return c; }
};
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
/** what of the buoy shows: pixels that differ with it there, above the waterline, near its column */
function seen(pathA, pathB, cx, top, base) {
  const A = png(pathA), B = png(pathB);
  const halfW = Math.max(40, (base - top) * 0.5), x0 = Math.max(0, Math.round(cx - halfW)), x1 = Math.min(A.w, Math.round(cx + halfW)), y0 = Math.max(0, Math.round(top - 30)), y1 = Math.min(A.h, Math.round(base + 2));
  let area = 0, sum = 0, lo = 1e9, hi = -1;
  for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
    const a = (y * A.w + x) * A.ch, b = (y * B.w + x) * B.ch, d = Math.abs(A.out[a] - B.out[b]) + Math.abs(A.out[a + 1] - B.out[b + 1]) + Math.abs(A.out[a + 2] - B.out[b + 2]);
    if (d > 45) { area++; sum += d / 3; if (y < lo) lo = y; if (y > hi) hi = y; }
  }
  return { h: hi < 0 ? 0 : hi - lo + 1, area, contrast: area ? sum / area : 0 };
}
const median = (v) => [...v].sort((a, b) => a - b)[v.length >> 1];
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-buoys', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
let fail = 0, ctlFail = 0, ctlAll = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
for (const backend of BACKENDS) {
  await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  await call('frame', { op: 'step', frames: 30 });
  for (const cid of COURSE_IDS) {
    const c = readCourse(cid), level = COURSE_IDS.indexOf(cid);
    await cmd(`course ${level}`);
    // three buoys that count at normal difficulty, well apart, with room to run up to them, open water between and in the picture when the craft points along the course: a buoy behind a headland or round a bend is the HUD's work (tools/play-romdev.mjs)
    const at = (m) => c.track[Math.floor(((m + c.len) % c.len) / c.len * c.track.length)];
    const open = (b) => [100, 162].every((d) => { const p = at(b[3] - d), q = at(b[3] - d + 8); const tx = q[0] - p[0], tz = q[1] - p[1]; const cos = (tx * (b[0] - p[0]) + tz * (b[1] - p[1])) / (Math.hypot(tx, tz) * Math.hypot(b[0] - p[0], b[1] - p[1])); if (cos < 0.93) return false; for (let k = 0; k <= 40; k++) { const t = k / 40, x = p[0] + (b[0] - p[0]) * t, z = p[1] + (b[1] - p[1]) * t; if (c.ground(x, z) > 1 || c.placed.some((q) => q[5] > 0 && Math.hypot(x - q[1], z - q[2]) < q[5] + 6) || (t < 0.97 && c.ramps.some((r) => Math.hypot(x - r[0], z - r[1]) < 34))) return false; } return true; });
    const on = c.buoys.map((b, i) => [b, i]).filter(([b, i]) => (i & 3) !== 3 && b[3] > 300 && open(b));
    console.log(`${cid}: ${on.length} buoys with open water to ${162} m`);
    const picks = [on[0], on[on.length >> 1], on[on.length - 1]];
    if (on.length < 3) throw new Error(cid + ': too few buoys to measure');
    for (const style of [1, 0]) for (const D of [100, 150]) {
      const got = [];
      for (const [b, bi] of picks) {
        await cmd(`buoy_style ${style}`); await cmd('buoy_shot 0 1 0');
        await cmd('race'); await cmd(`go ${b[3] - D - 12}`); await cmd(`next ${bi}`);
        await call('input', { op: 'set', ports: [{ south: true }] });
        await call('frame', { op: 'step', frames: 30 });
        await cmd(`next ${bi}`);
        await cmd('buoy_shot 0 1 1');
        const base = `${OUT}/${backend}-${cid}-s${style}-d${D}-b${bi}`;
        await call('frame', { op: 'stepAndShot', frames: 2, path: base + '.png' });
        await cmd('buoy_shot 0 0 1');
        await call('frame', { op: 'stepAndShot', frames: 2, path: base + '-none.png' });
        const P = await rd('hud.buoy'), pos = await rd('p0.pos'), nb = +(await rd('p0.next_buoy'));
        await cmd('buoy_shot 1 1 0');
        await call('input', { op: 'set', ports: [{}] });
        if (+P[2] !== 1 || nb !== bi) { got.push({ h: 0, area: 0, contrast: 0, note: `not in front (${P[2]}) or next ${nb}` }); continue; }
        const m = seen(base + '.png', base + '-none.png', +P[0], +P[1], +P[3]);
        m.dist = Math.hypot(b[0] - pos[0], b[1] - pos[2]);
        got.push(m);
      }
      const h = median(got.map((g) => g.h)), area = median(got.map((g) => g.area)), con = median(got.map((g) => g.contrast)), n = NEED[D], pass = h >= n.h && area >= n.area && con >= n.contrast;
      const row = `${backend} ${cid} ${D} m: height ${h} px, area ${area} px, contrast ${con.toFixed(0)}; each ${got.map((g) => `${g.h}/${g.area}${g.note ? ' ' + g.note : ''}`).join(' ')}`;
      if (style) check(`mark ${row}`, pass, `needs ${n.h} px, ${n.area} px, ${n.contrast}`);
      else { ctlAll++; if (!pass) ctlFail++; console.log(`control, today's buoy ${row}: ${pass ? 'PASSES (bad)' : 'fails, as it must'}`); }
    }
  }
  await cmd('buoy_style 1');
}
check('control: the small buoy fails everywhere', ctlFail === ctlAll, `${ctlFail} of ${ctlAll} fail`);
process.exit(fail ? 1 : 0);
