// See-through water: what is under the surface shows through it, just under, blurred, and fades with depth (gone by about a
// metre); much weaker at night. On the cart through romdev, GL and WebGPU, both craft, day, dusk and night.
// The sim is held still (the same moment in every picture) on open water, the race camera on the craft.
//  (1) depth: a flat orange plate (self-lit, so it shows at night) 7 m ahead, 0.1, 0.4, 0.8 and 1.2 m under the surface (sw plate),
//      shot with the water as it ships (see_through 0.4, see_night 0.05), with the opaque water (see_through 0: the water's own tint and
//      shore foam, none of the effect) and, hidden, with no water over it. The plate's pixels are found with the water hidden.
//      visible(d) is how much of the plate's own contrast against the water alone reaches the eye: per pixel the colour distance of
//      the picture from the water alone over that of the bare plate (capped at 1), averaged over the plate's pixels. Day: more than
//      half at 0.1 m, and fading with depth faster than the plain water does (by 0.8 m under 65% of the 0.1 m value, by 1.2 m under
//      half); night and dusk well under day.
//  (2) the stern: the craft lowered by 0.1, 0.4, 0.8 and 1.2 m from where it rides (sw sink: render only; the part at the waterline
//      is that deep), shot the same two ways: the pixels the effect changes (the stern and the legs below the surface showing, the
//      opaque water's white foam slab over the hull gone) must be there at 0.1 and 0.4 m.
// Controls (each must fail the same assertions): the opaque water against itself shows nothing; the held moment is checked by the
// water alone being the same picture 20 ticks apart; at night with see_night 1 the water is not weak.
// Pictures are kept in out/jetski-water to look at.
//   node tests/jetski-water.mjs [--gl] [--webgpu] [--cart <path>] [--quick]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/jetski-water');
const { readImage } = await import(join(ROOT, 'deps/cartwheel-sdk/linux-x64/tools/lib/png.mjs'));
const SESSION = process.env.JETSKI_SESSION || 'jetski-water';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const QUICK = process.argv.includes('--quick');
/* the courses' own light: Cold Sound by day, Crater Atoll at dusk, Iron Quay at night */
const TODS = [['day', 1], ['dusk', 0], ['night', 3]];
const DEPTHS = [0.1, 0.4, 0.8, 1.2]; /* metres under the surface (the plate) or the craft lowered (the stern) */
const BOX = { x0: 480, x1: 1440, y0: 380, y1: 1000 }; /* round the craft, clear of the HUD */
const THR = { craft: 40 };
const STERN_MIN = 3000, STAND_MIN = 300; /* pixels */
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': 'jetski-water' };
async function bye(code) {
  try { await fetch(ROMDEV_URL + '/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
}
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
const tool = async (name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const cmd = (text) => tool('wasm', { op: 'command', text });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (x) => x.toFixed(2);

const WATERS = { shown: [], opaque: ['see_through 0'], hidden: ['visible false'], full: ['see_night 1'] };
/* one scene: the sim held still on open water, the craft where it rides, the plate and the lowered craft shot with each water */
async function scene(backend, craft, tod, course) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', deterministicSeed: 1, deterministicStepMs: 1000 / 60, ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 20 });
  await cmd(`course ${course}`); await tool('frame', { op: 'step', frames: 30 });
  await cmd(`craft ${craft}`); await cmd('trial 0.05');
  await tool('frame', { op: 'step', frames: 90 });
  await cmd('buoy_shot 1 1 1'); /* the sim held still: the same moment for every picture */
  const out = { plate: {}, stern: {} }, base = `${backend}-${craft ? 'stand' : 'sit'}-${tod}`;
  const water = async (w) => {
    await cmd('set Root/Water see_through 0.4'); await cmd('set Root/Water see_night 0.05'); await cmd('set Root/Water visible true');
    for (const s of WATERS[w]) await cmd(`set Root/Water ${s}`);
  };
  const shot = async (name) => { const path = join(OUT, `${base}-${name}.png`); await tool('frame', { op: 'stepAndShot', frames: 2, path }); return path; };
  await cmd('sw sink 0');
  for (const w of Object.keys(WATERS)) {
    await water(w); await cmd('sw plate -1');
    out.alone = out.alone || {}; out.alone[w] = await shot(`alone-${w}`);
    for (const d of DEPTHS) { await cmd(`sw plate ${d}`); (out.plate[w] ||= []).push(await shot(`plate-${w}-${d}`)); }
  }
  await cmd('sw plate -1');
  out.noise = [out.alone.opaque]; await tool('frame', { op: 'step', frames: 20 }); await water('opaque'); out.noise.push(await shot('alone-opaque-later'));
  for (const w of ['shown', 'opaque']) { await water(w); for (const d of DEPTHS) { await cmd(`sw sink ${d}`); (out.stern[w] ||= []).push(await shot(`stern-${w}-${d}`)); } }
  await cmd('sw sink 0'); await water('shown');
  return out;
}
const dist = (a, b, k) => Math.abs(a.rgba[k] - b.rgba[k]) + Math.abs(a.rgba[k + 1] - b.rgba[k + 1]) + Math.abs(a.rgba[k + 2] - b.rgba[k + 2]);
/* the plate: per depth, the share of the plate's contrast against the water alone that reaches the eye */
function plate(sc, which) {
  const HA = readImage(sc.alone.hidden), W = readImage(sc.alone[which === 'opaque' ? 'opaque' : 'shown']);
  return DEPTHS.map((d, i) => {
    const hidden = readImage(sc.plate.hidden[i]), shown = readImage(sc.plate[which][i]);
    let n = 0, sum = 0;
    for (let y = 0; y < hidden.h; y++) for (let x = 0; x < hidden.w; x++) {
      const k = (y * hidden.w + x) * 4;
      if (dist(hidden, HA, k) < THR.craft) continue;
      n++;
      sum += Math.min(1, dist(shown, W, k) / Math.max(dist(hidden, W, k), 1));
    }
    return { n, mean: n >= 500 ? sum / n : NaN };
  });
}
/* the stern: the pixels the see-through water changes against the opaque, per lowered depth */
const stern = (sc) => DEPTHS.map((d, i) => { const a = readImage(sc.stern.shown[i]), b = readImage(sc.stern.opaque[i]); let n = 0; for (let y = BOX.y0; y < BOX.y1; y++) for (let x = BOX.x0; x < BOX.x1; x++) if (dist(a, b, (y * a.w + x) * 4) > THR.craft) n++; return n; });
const noise = (sc) => { const a = readImage(sc.noise[0]), b = readImage(sc.noise[1]); let t = 0, c = 0; for (let y = BOX.y0; y < BOX.y1; y += 2) for (let x = BOX.x0; x < BOX.x1; x += 2) { t += dist(a, b, (y * a.w + x) * 4); c++; } return t / c; };
/* the shape asked of day: mostly there at the surface, fading faster than the plain water */
const dayOk = (v) => v[0].mean >= 0.5 && v[2].mean <= 0.65 * v[0].mean && v[3].mean <= 0.5 * v[0].mean && v[0].mean >= v[1].mean && v[1].mean >= v[2].mean && v[2].mean >= v[3].mean;
const row = (v) => v.map((x) => f2(x.mean)).join(' ');
const results = {};
for (const backend of BACKENDS) for (const craft of QUICK ? [0] : [0, 1]) for (const [tod, tv] of QUICK ? TODS.slice(0, 1) : TODS) {
  const sc = await scene(backend, craft, tod, tv);
  const name = `${backend} ${craft ? 'stand-up' : 'sit-down'} ${tod}`;
  const v = plate(sc, 'shown'), vo = plate(sc, 'opaque'), vf = plate(sc, 'full'), s = stern(sc);
  results[`${backend}-${craft}-${tod}`] = { visible: v.map((x) => +f2(x.mean)), opaque: vo.map((x) => +f2(x.mean)), seeNight1: vf.map((x) => +f2(x.mean)), stern: s };
  console.log(`  ${name}: the plate visible at ${DEPTHS.join(' ')} m: ${row(v)} (opaque water: ${row(vo)}; see_night 1: ${row(vf)}; ${v.map((x) => x.n).join(' ')} pixels); stern pixels changed at ${DEPTHS.join(' ')} m lowered: ${s.join(' ')}`);
  check(`${name}: the held moment is held`, noise(sc) < 1.5, `the water alone differs by ${f2(noise(sc))} between 20 ticks`);
  if (tod === 'day') {
    check(`${name}: mostly there at the surface, fading with depth`, dayOk(v), row(v));
    check(`control: the plain water (see_through 0) does not fade like that`, !dayOk(vo), row(vo));
    const min = craft ? STAND_MIN : STERN_MIN; /* the stand-up's hull is white: against white foam little changes */
    check(`${name}: the stern shows through at 0.1 and 0.4 m lowered`, s[craft ? 1 : 0] >= min && s[1] >= min, `${s[0]} and ${s[1]} pixels changed, at least ${min}${craft ? ' at 0.4 m' : ''}`);
  }
}
for (const backend of BACKENDS) for (const craft of QUICK ? [0] : [0, 1]) {
  const day = results[`${backend}-${craft}-day`], dusk = results[`${backend}-${craft}-dusk`], night = results[`${backend}-${craft}-night`];
  const nm = `${backend} ${craft ? 'stand-up' : 'sit-down'}`;
  if (day && night) {
    check(`${nm}: night much weaker than day`, night.visible[1] <= 0.6 * day.visible[1] && night.visible[2] <= 0.6 * day.visible[2], `visible at 0.4 and 0.8 m: ${night.visible[1]} ${night.visible[2]} at night against ${day.visible[1]} ${day.visible[2]} by day`);
    check(`control: night with see_night 1 is not that weak`, !(night.seeNight1[1] <= 0.6 * day.visible[1] && night.seeNight1[2] <= 0.6 * day.visible[2]), `${night.seeNight1[1]} ${night.seeNight1[2]} against ${day.visible[1]} ${day.visible[2]} by day`);
  }
  if (day && dusk) check(`${nm}: dusk well under day`, dusk.visible[1] <= 0.6 * day.visible[1], `visible at 0.4 m: ${dusk.visible[1]} at dusk against ${day.visible[1]} by day`);
}
writeFileSync(join(OUT, 'results.json'), JSON.stringify(results));
await bye(fail ? 1 : 0);
