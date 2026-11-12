// A ramp's deck under the water shows no shoal foam edge. Water foams where something lies within its shore-foam width
// (1.6 m) below the surface; a ramp deck in the refraction made, in the first releases of this game, a hard-edged white band of
// it over the low end (which is why the decks were kept out of the refraction). On Crater Atoll, a held camera 28 m straight
// above the ramp's low end (the ramp's own place from debug command ramp_where), the sim held still,
// the cart through romdev, GL and WebGPU: the pixels that are bright foam (all three channels over 150) outside the HUD.
// Control: v0.12.3's cart, whose ramp decks were in the refraction, must show the band: several times the pixels.
// Pictures are kept in out/jetski-water-ramp to look at.
//   node tests/jetski-water-ramp.mjs [--gl] [--webgpu] [--cart <path>] [--control <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, writeFileSync, existsSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/jetski-water-ramp');
const { readImage } = await import(join(ROOT, 'deps/cartwheel-sdk/linux-x64/tools/lib/png.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const CONTROL = resolve(arg('--control', join(ROOT, 'build/control/v0.12.3/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const BOX = { x0: 100, x1: 1700, y0: 260, y1: 900 }, GO = { x0: 800, x1: 1120, y0: 330, y1: 520 }; /* clear of the HUD */
const MAX_NEW = 4000, RATIO = 3; /* pixels, and how many times the control must show */
mkdirSync(OUT, { recursive: true });
if (!existsSync(CONTROL)) { mkdirSync(dirname(CONTROL), { recursive: true }); const r = spawnSync('gh', ['release', 'download', 'v0.12.3', '-R', 'monteslu/more-cartwheel-examples', '-p', 'jetski.wasc', '-D', dirname(CONTROL)], { encoding: 'utf8' }); if (r.status) { console.error(r.stderr); process.exit(3); } }
const H = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski-water-ramp', 'x-romdev-agent': 'jetski-water-ramp' };
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
const dist = (a, b, k) => Math.abs(a.rgba[k] - b.rgba[k]) + Math.abs(a.rgba[k + 1] - b.rgba[k + 1]) + Math.abs(a.rgba[k + 2] - b.rgba[k + 2]);
/* the foam pixels, per view */
async function foam(cart, backend, tag) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', deterministicSeed: 1, deterministicStepMs: 1000 / 60, ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 20 }); await cmd('course 0'); await tool('frame', { op: 'step', frames: 30 });
  await cmd('race'); await tool('frame', { op: 'step', frames: 240 });
  const w = (await cmd('ramp_where 0')).reply, { x, z, sy, cy } = w;
  const views = { top: [x, 28, z, x + 0.01, 0, z + 0.01] };
  await cmd('buoy_shot 1 1 1');
  const out = {};
  for (const [name, p] of Object.entries(views)) {
    await cmd(`peek ${p.join(' ')}`);
    const path = join(OUT, `${backend}-${tag}-${name}.png`);
    await tool('frame', { op: 'stepAndShot', frames: 2, path });
    const im = readImage(path);
    let n = 0;
    for (let y = BOX.y0; y < BOX.y1; y++) for (let xx = BOX.x0; xx < BOX.x1; xx++) { if (xx >= GO.x0 && xx < GO.x1 && y >= GO.y0 && y < GO.y1) continue; const k = (y * im.w + xx) * 4; if (im.rgba[k] > 150 && im.rgba[k + 1] > 150 && im.rgba[k + 2] > 150) n++; }
    out[name] = n;
  }
  return out;
}
const res = {};
for (const backend of BACKENDS) {
  const a = await foam(CART, backend, 'new'), c = await foam(CONTROL, backend, 'old');
  res[backend] = { new: a, control: c };
  for (const v of Object.keys(a)) {
    check(`${backend} ${v}: the deck under the water makes no foam band`, a[v] <= MAX_NEW, `${a[v]} foam pixels, at most ${MAX_NEW}`);
    check(`control (v0.12.3, deck in the refraction) ${backend} ${v}: the band is there`, c[v] >= RATIO * Math.max(a[v], 1) && c[v] > MAX_NEW, `${c[v]} pixels against ${a[v]}`);
  }
}
writeFileSync(join(OUT, 'results.json'), JSON.stringify(res));
await bye(fail ? 1 : 0);
