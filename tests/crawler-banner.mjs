// crawler on the cart through romdev (crawler_auto: the bots play), GL and WebGPU: the FLOOR n banner at a floor's
// start never covers a hero. On the first floor of every tileset (1, 21, 41, 61, 81, 101) the banner is drawn in the
// band at the top or the bottom of the screen where no hero stands (banner.hit counts the frames it covered a hero's
// box, feet to the P tag; banner.shown the frames it was drawn), and a frame of each start is saved to look at.
// Control: sw banner_centre 1 (the banner at the screen's middle, as before) must cover a hero on some tileset.
//   node tests/crawler-banner.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.CRAWLER_SESSION || 'crawler-banner', OUT = join(ROOT, 'out/crawler-banner');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/crawler_auto.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return +(v.values ?? v.value); };
const step = (frames) => tool('frame', { op: 'step', frames });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const FLOORS = [1, 21, 41, 61, 81, 101];

async function run(backend, tag, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(20);
  const ticks = async () => { /* (the cart's fields appear once it has loaded) */
    const r = await fetch(ROMDEV_URL+'/tool/wasm', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify({ op: 'read', name: 'game.ticks' }) });
    const j = await r.json().catch(() => ({})); return j.error ? 0 : +(j.values ?? j.value);
  };
  for (let f = 0; f < 60 * 30 && !(await ticks()); f += 30) await step(30); /* the bots start on their own */
  if (pre) await tool('wasm', { op: 'command', text: pre });
  const out = [];
  for (const n of FLOORS) {
    await tool('wasm', { op: 'command', text: `floor ${n}` });
    const s0 = await rd('banner.shown'), h0 = await rd('banner.hit');
    await step(50);
    await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-${String(n).padStart(3, '0')}.png`) });
    await step(160);
    out.push({ n, floor: await rd('game.floor'), shown: (await rd('banner.shown')) - s0, hit: (await rd('banner.hit')) - h0 });
  }
  return out;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  for (const r of await run(backend, 'after'))
    check(`floor ${r.n}: the banner shown and over no hero`, r.floor === r.n && r.shown > 60 && r.hit === 0, `floor ${r.floor}, drawn ${r.shown} frames, over a hero ${r.hit}`);
  const c = await run(backend, 'control', 'sw banner_centre 1');
  const hits = c.reduce((a, r) => a + r.hit, 0);
  check('control: sw banner_centre 1 covers a hero', hits > 0 && c.every((r) => r.shown > 60), c.map((r) => `${r.n}: ${r.hit}/${r.shown}`).join(', '));
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
