// Fish you can see from the race camera, on the cart through romdev, GL and WebGPU: on the atoll, the fjord and the port (not
// the river, whose current carries the craft, nor the storm, whose whitecaps bury them): a school brought
// 12 m ahead of the craft at rest on open water (no spray over the picture) and 3.5 m to its side, the world held still, the race camera's picture with the fish and
// without them (sw fish_hide 1). The pixels that change by over 8 % of the luminance range, less those that change
// between two pictures of the same held moment (the spray and the wind keep moving): at least 3000, and at least 1.4 times
// what the old small dark school half a metre down gave (sw fish_old 1, the control that must fail the bound).
//   node tests/jetski-fish-see.mjs [--gl] [--webgpu] [--courses 0,1,2]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/fish-see');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const COURSES = arg('--courses', '0,1,3').split(',').map(Number);
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski-fish-see', 'x-romdev-agent': 'jetski-fish-see' };
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
const changed = (a, b) => { const A = png(a), B = png(b); let n = 0; for (let i = 0; i < A.w * A.h; i++) if (Math.abs(lum(A.out, i * A.ch) - lum(B.out, i * B.ch)) > 0.08) n++; return n; };
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
    const seen = {};
    for (const old of [0, 1]) {
      await cmd(`sw fish_old ${old}`); await cmd('sw life 1'); await cmd(`course ${c}`); await cmd('mode 1'); await cmd('wxsw force 0'); await cmd('trial 0.05'); await cmd('sw hud 0');
      await tool('frame', { op: 'step', frames: 40 });
      await cmd('life_here 0 12'); await tool('frame', { op: 'step', frames: 90 }); await cmd('sw freeze 1'); await tool('frame', { op: 'step', frames: 3 });
      const a = await shot(join(OUT, `${backend}-c${c}-old${old}-fish.png`)), a2 = await shot(join(OUT, `${backend}-c${c}-old${old}-fish2.png`));
      await cmd('sw fish_hide 1'); await tool('frame', { op: 'step', frames: 2 });
      const b = await shot(join(OUT, `${backend}-c${c}-old${old}-none.png`));
      await cmd('sw fish_hide 0'); await cmd('sw freeze 0'); await cmd('sw hud 1');
      seen[old] = Math.max(0, changed(a, b) - changed(a, a2)); /* less what changes between two pictures of the same held moment (spray, wind) */
    }
    await cmd('sw fish_old 0'); await cmd('sw life 1');
    check(`${backend} course ${c}: the school reads from the race camera`, seen[0] >= 3000 && seen[0] >= 1.4 * seen[1], `${seen[0]} pixels, the old school ${seen[1]}`);
  }
  await tool('host', { op: 'shutdown' });
}
await bye(fail ? 1 : 0);
