// The look of a ride, on the cart through romdev, GL and WebGPU.
//   The chase camera, low and close in the spray: at race speed it sits at least 15 % nearer the craft and 15 % lower over
//   the water than the old chase did at the same moment, and never under the water (cam.under_ticks stays 0). Control:
//   sw cam_old 1 (the old chase) fails the bound against itself by construction; the numbers are printed for both.
//   Thick spray: in the same frame, the bright white (luminance over 0.85) in the lower middle of the picture is at least
//   1.5x what the thin spray of before (sw spray_thick 0, the control) makes.
//   Water on the lens: a hard landing beads drops on the camera's glass that clear within 2.5 s and never cover more than
//   15 % of the middle third. Control: sw lens 0 makes none.
//   A long flight (a drop from 8 m): 0.9 s into it the camera has eased at least 0.5 m further back than without the
//   easing (sw cam_air 0, the control, which fails the bound against itself).
//   Pictures are kept in out/camera for a look.
//   node tests/jetski-camera.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/camera');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski-camera', 'x-romdev-agent': 'jetski-camera' };
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
/* bright white in the lower middle of the frame (the craft, its tail and its sheets) */
const bright = (path) => {
  const { w, h, ch, out } = png(path); let n = 0;
  for (let y = Math.round(h * 0.5); y < Math.round(h * 0.97); y++) for (let x = Math.round(w * 0.25); x < Math.round(w * 0.75); x++) {
    const i = (y * w + x) * ch, l = (0.2126 * out[i] + 0.7152 * out[i + 1] + 0.0722 * out[i + 2]) / 255;
    if (l > 0.85) n++;
  }
  return n;
};
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(2);
/* a fresh race on Crater Atoll at speed, the throttle held for a stretch of frames, then a picture */
async function ride(backend, pre, frames, shot, extra = []) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 120 });
  for (let k = 0; k < 120 && +(await rd('cw.loading')) !== 0; k++) await tool('frame', { op: 'step', frames: 30 });
  for (const c of [...pre, 'course 0', 'no_dq 1', 'race', 'go 300', ...extra]) await cmd(c);
  await tool('input', { op: 'sequence', steps: [{ input: { ports: [{ south: true }] }, frames }] });
  const path = join(OUT, `${backend}-${shot}.png`);
  await tool('frame', { op: 'stepAndShot', frames: 1, path });
  return { path, eye: (await rd('cam.eye')).map(Number), under: +(await rd('cam.under_ticks')), lens: (await rd('lens')).map(Number) };
}
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const now = await ride(backend, [], 240, 'cam'), old = await ride(backend, ['sw cam_old 1'], 240, 'cam-old');
  check(`${backend}: at race speed the camera sits nearer and lower`, now.eye[2] > 18 && now.eye[0] <= 0.85 * old.eye[0] && now.eye[1] <= 0.85 * old.eye[1] && now.under === 0,
    `${f2(now.eye[0])} m behind and ${f2(now.eye[1])} m over the water at ${f2(now.eye[2])} m/s; the old chase ${f2(old.eye[0])} m and ${f2(old.eye[1])} m; ${now.under} ticks under`);
  const thin = await ride(backend, ['sw spray_thick 0'], 240, 'spray-thin');
  const bt = bright(now.path), bn = bright(thin.path);
  check(`${backend}: thick white spray, at least 1.5x the thin spray's bright water`, bt >= 1.5 * bn && bn > 0, `${bt} bright pixels against ${bn} (${(bt / Math.max(1, bn)).toFixed(2)}x)`);
  const land = await ride(backend, [], 75, 'lens', ['drop 0.05 4 24']), after = await ride(backend, [], 225, 'lens-after', ['drop 0.05 4 24']), off = await ride(backend, ['sw lens 0'], 75, 'lens-off', ['drop 0.05 4 24']);
  check(`${backend}: a hard landing beads drops on the lens, clear of the middle`, land.lens[0] >= 4 && land.lens[1] <= 0.15, `${land.lens[0]} drops, ${(100 * land.lens[1]).toFixed(1)} % of the middle third`);
  check(`${backend}: the drops clear within 2.5 s`, after.lens[0] === 0, `${after.lens[0]} left`);
  check(`${backend}: control: lens 0 makes none`, off.lens[2] === 0, `${off.lens[2]} made`);
  const fly = await ride(backend, [], 54, 'air', ['drop 0.05 8 24']), fly0 = await ride(backend, ['sw cam_air 0'], 54, 'air-off', ['drop 0.05 8 24']);
  check(`${backend}: a long flight, the camera eases back to hold the arc`, fly.eye[0] - fly0.eye[0] >= 0.5 && fly.eye[3] > 0.3, `${f2(fly.eye[0])} m behind against ${f2(fly0.eye[0])} m without the easing (eased ${f2(fly.eye[3])})`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
