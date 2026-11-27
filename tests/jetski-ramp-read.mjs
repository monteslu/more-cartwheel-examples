// Jumps that read from far down the course, on the cart through romdev, GL and WebGPU: a held camera 2 m over the water
// on the course 100 m and 150 m before ramp 0's low end, the world held still. Picture pairs: the ramps drawn
// and hidden (sw ramps_hide 1). What the ramp adds to the picture is the pixels that change by over a quarter of the
// luminance range: at least 500 at both distances on every course, and the jump's marks (masts at the lip, chequered
// boards, a lamp) at least 50 more on top. The control, sw ramp_marks 0 (the old ramp), adds none.
// Also the timber deck on the slipway courses (sw ramp_skin), and every frame kept for a look.
//   node tests/jetski-ramp-read.mjs [--gl] [--webgpu] [--courses 0,1,2,3,4]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/ramp-read');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const COURSES = arg('--courses', '0,1,2,3,4').split(',').map(Number);
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski-ramp-read', 'x-romdev-agent': 'jetski-ramp-read' };
const bye = async (code) => { try { await fetch(ROMDEV_URL + '/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {} process.exit(code); };
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
const tool = async (name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text(); let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (text) => tool('wasm', { op: 'command', text });
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
const lum = (o, i) => (0.2126 * o[i] + 0.7152 * o[i + 1] + 0.0722 * o[i + 2]) / 255;
/* pixels that differ by over a quarter of the luminance range between a picture with the ramps and one without */
function salient(a, b) {
  const A = png(a), B = png(b); let n = 0;
  for (let i = 0; i < A.w * A.h; i++) if (Math.abs(lum(A.out, i * A.ch) - lum(B.out, i * B.ch)) > 0.25) n++;
  return n;
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const shot = async (path) => { await tool('frame', { op: 'stepAndShot', frames: 2, path }); return path; };
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 120 });
  for (let k = 0; k < 120 && +(await rd('cw.loading')) !== 0; k++) await tool('frame', { op: 'step', frames: 30 });
  for (const c of COURSES) {
    await cmd(`course ${c}`); await cmd('mode 1'); await cmd('wxsw force 0'); await cmd('race'); await cmd('go 300'); await cmd('sw hud 0'); await tool('frame', { op: 'step', frames: 30 });
    const tide = +(await rd('race.tide'));
    const r = (await cmd('ramp_where 0')).reply;
    const seen = {};
    for (const d of [100, 150]) {
      /* on the course, d metres before the ramp's low end along the track (the way a rider comes at it), 2 m up, looking at its lip */
      await cmd(`go ${Math.max(1, r.s - 20 - d).toFixed(1)}`); await tool('frame', { op: 'step', frames: 1 });
      const at = (await rd('p0.pos')).map(Number), cx = at[0], cz = at[2], lx = r.x + r.sy * 20, lz = r.z + r.cy * 20;
      await cmd(`peek ${cx.toFixed(2)} ${(tide + 3).toFixed(2)} ${cz.toFixed(2)} ${lx.toFixed(2)} ${(tide + 4).toFixed(2)} ${lz.toFixed(2)}`);
      await cmd('sw freeze 1');
      const row = {};
      for (const marks of [1, 0]) {
        await cmd(`sw ramp_marks ${marks}`); await cmd('sw ramps_hide 0');
        const on = await shot(join(OUT, `${backend}-c${c}-${d}-m${marks}.png`));
        await cmd('sw ramps_hide 1');
        const off = await shot(join(OUT, `${backend}-c${c}-${d}-hidden.png`));
        row[marks] = salient(on, off);
      }
      await cmd('sw ramps_hide 0'); await cmd('sw ramp_marks 1'); await cmd('sw freeze 0');
      seen[d] = row;
      const add = row[1] - row[0];
      check(`${backend} course ${c}, ${d} m: the ramp reads, and its marks stand out over it`, row[0] >= 500 && add >= 50, `the ramp ${row[0]} pixels, its marks ${add} more (${(100 * add / Math.max(1, row[0])).toFixed(0)} %)`);
    }
    await cmd('peek');
  }
  await tool('host', { op: 'shutdown' });
}
await bye(fail ? 1 : 0);
