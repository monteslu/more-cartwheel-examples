// Where the craft meets the water, on the cart through romdev, GL and WebGPU: the craft at rest on a swell (the
// sit-down, its rider's legs taken down into the water with sw sink 0.45 so the shins cross the surface), held still
// at four moments of the swell (the craft on the crest, in the trough and between), a held camera 3 m out to its side
// and 2.2 m up (36 degrees down) looking at its side at the waterline. Each moment is three pictures: all drawn, the water
// hidden (sw water_hide 1) and the craft and rider hidden (sw craft_hide 1). Down each column where the craft or the rider
// goes into the water, what still shows of it 2 cm under the line (v: 1 seen as it is, 0 cut off), the median over the
// columns: at least 0.6 in Crater Atoll's clear water, which barely changes an object in its first centimetres, and 0.25
// in the harbour's murky water at dusk (see_through 0.07, as murky water that hides a leg within 5 to 10 cm). A cut is 0.
// Checked on Crater Atoll (clear) and Harbour (murky). The fade under the line is the engine's (its water-contact gate
// holds see_through to the reference fade lengths); the course tables set see_through for their water.
// Control (must fail): the same cart with the engine's water as before v0.87 (CW_TEST_WATER_CONTACT=old and
// CW_TEST_WATER_REST_CLIP=1, sent with the load): under the bound at some moment.
//   node tests/jetski-waterline.mjs [--gl] [--webgpu] [--cart <path>] [--sink 0.45]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/waterline');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const SINK = +arg('--sink', 0.45), UP = 2.2, OUTM = 3;
const PX_PER_M = 1080 / (2 * Math.hypot(UP, OUTM) * Math.tan(31 * Math.PI / 180)), PX_CM = PX_PER_M * (OUTM / Math.hypot(UP, OUTM)) / 100;
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
// (course, name, the least that shows 2 cm under the line: clear water barely changes it; the harbour's murky water at dusk
// takes most of it within a few centimetres, as real murky water does, but a cut shows nothing)
const COURSES = [[0, 'atoll', 0.6], [3, 'harbour', 0.25]], MOMENTS = 4;
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski-waterline', 'x-romdev-agent': 'jetski-waterline' };
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
/* the visibility under the line from three pictures of one held moment: W (all drawn), D (the water hidden: the craft and
   rider as they are) and N (the craft and rider hidden: the water alone). Down each column (every 4th) the line is the first
   row after a run of 12 where W and D agree (above the water the water changes nothing) and where W and D then differ,
   with the craft or rider still there 2 cm lower in D (it stands out from the water alone by 0.1). There what shows of
   it is v = |W - N| / |D - N|: 1 seen as it is, 0 cut off at the line. The median over the columns */
function vis(wP, dP, nP) {
  const A = png(wP), B = png(dP), C = png(nP), w = A.w, h = A.h, vs = [];
  const L = (P, x, y) => { const i = (y * w + x) * P.ch; return (0.2126 * P.out[i] + 0.7152 * P.out[i + 1] + 0.0722 * P.out[i + 2]) / 255; };
  const d2 = Math.max(2, Math.round(2 * PX_CM));
  for (let x = 8; x < w - 8; x += 4) {
    let same = 0;
    for (let y = 1; y < h - d2 - 1; y++) {
      if (Math.abs(L(A, x, y) - L(B, x, y)) <= 0.012) { same++; continue; }
      if (same >= 12) {
        const dn = Math.abs(L(B, x, y + d2) - L(C, x, y + d2));
        if (dn >= 0.1) vs.push(Math.min(1.5, Math.abs(L(A, x, y + d2) - L(C, x, y + d2)) / dn));
      }
      same = 0;
    }
  }
  vs.sort((a, b) => a - b);
  return { v: vs.length ? vs[vs.length >> 1] : NaN, n: vs.length };
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
/* one course: MOMENTS held moments 37 frames apart, each a pair (the control's pair takes the dry picture of the same
   moment the first run made: a deterministic load, the same simulation) */
async function measure(backend, tag, course, dryFrom, env) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', deterministicSeed: 1, deterministicStepMs: 1000 / 60, wgpu: backend === 'webgpu', ...(env ? { firstCommand: env } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 120 });
  for (let k = 0; k < 120 && +(await rd('cw.loading')) !== 0; k++) await tool('frame', { op: 'step', frames: 30 });
  for (const c of ['sw life 0', `course ${course}`, 'craft 0', 'wxsw force 0', 'trial 1', `sw sink ${SINK}`]) await cmd(c);
  await tool('frame', { op: 'step', frames: 60 });
  const res = [];
  for (let m = 0; m < MOMENTS; m++) {
    await cmd('sw freeze 0');
    await tool('frame', { op: 'step', frames: 37 });
    await cmd('sw freeze 1');
    const p = (await rd('p0.pos')).map(Number), yaw = +(await rd('p0.yaw')), surf = +(await rd('p0.surf'));
    const rx = Math.cos(yaw), rz = -Math.sin(yaw); // the craft's side
    await cmd(`peek ${(p[0] + rx * OUTM).toFixed(2)} ${(surf + UP).toFixed(2)} ${(p[2] + rz * OUTM).toFixed(2)} ${p[0].toFixed(2)} ${surf.toFixed(2)} ${p[2].toFixed(2)}`);
    await cmd('sw hud 0');
    await tool('frame', { op: 'step', frames: 3 });
    const path = join(OUT, `${backend}-${tag}-${course}-${m}.png`), dry = join(OUT, `${backend}-now-${course}-${m}-dry.png`), sea = join(OUT, `${backend}-${tag}-${course}-${m}-sea.png`);
    await tool('frame', { op: 'stepAndShot', frames: 1, path });
    await cmd('sw craft_hide 1'); await tool('frame', { op: 'stepAndShot', frames: 1, path: sea }); await cmd('sw craft_hide 0');
    if (!dryFrom) { await cmd('sw water_hide 1'); await tool('frame', { op: 'stepAndShot', frames: 1, path: dry }); await cmd('sw water_hide 0'); }
    res.push(vis(path, dry, sea));
  }
  await tool('host', { op: 'shutdown' });
  return res;
}
for (const backend of BACKENDS) {
  for (const [course, name, least] of COURSES) {
    const inRange = (v) => v >= least;
    const now = await measure(backend, 'now', course), old = await measure(backend, 'old', course, true, 'env CW_TEST_WATER_CONTACT=old CW_TEST_WATER_REST_CLIP=1');
    const fmt = (r) => r.map((s) => `${s.v.toFixed(2)} (${s.n})`).join(', ');
    check(`${backend} ${name}: 2 cm under the line at least ${least} of the craft and rider shows, at every moment`, now.every((s) => s.n >= 5 && inRange(s.v)), fmt(now));
    check(`${backend} ${name}: control, the water before v0.87 misses it at some moment`, old.some((s) => s.n >= 5 && !inRange(s.v)), fmt(old));
  }
}
await bye(fail ? 1 : 0);
