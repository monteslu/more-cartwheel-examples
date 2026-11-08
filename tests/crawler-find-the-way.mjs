// crawler on the cart through romdev, GL and WebGPU, by the pad: a player who knows nothing of floor 1 finds the way
// down by what the screen shows. One seat from the title; the hero stands still until the arrow by it appears (after
// 20 s on a floor), then the stick follows only the arrow (guide.u/v, the screen point it points to, against the
// hero's view.p1u/p1v): to the key, through its door, onto the exit. Floor 2 must be reached, with the EXIT label drawn
// over the hatch on the way (guide.exits_marked) and the arrow first pointing to the key (floor 1's door is shut).
// Control: sw guide_off 1 (no EXIT label, no arrow) must leave the same player on floor 1 with nothing to follow.
//   node tests/crawler-find-the-way.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.CRAWLER_SESSION || 'crawler-find-the-way', OUT = join(ROOT, 'out/crawler-find-the-way');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/crawler.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pad = (p) => tool('input', { op: 'set', ports: [p] });
const press = async (b) => { await pad({ [b]: true }); await step(3); await pad({}); await step(6); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, tag, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(20);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press('start'); await step(10); await press('start'); await step(15); await press('south'); await step(30);
  let waited = 0, first = 0, frames = 0, floor = 1;
  while (waited < 60 * 30 && !(first = +(await rd('guide.kind')))) { await step(30); waited += 30; } /* standing: the arrow comes */
  if (first) {
    await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-arrow.png`) });
    for (; frames < 60 * 90 && floor === 1; frames += 4) {
      const k = +(await rd('guide.kind'));
      if (k) {
        const du = (+(await rd('guide.u')) - +(await rd('view.p1u'))) * 16, dv = (+(await rd('guide.v')) - +(await rd('view.p1v'))) * 9, d = Math.hypot(du, dv);
        await pad(d > 1e-3 ? { axes: { lx: du / d, ly: dv / d } } : {});
      } else await pad({});
      await step(4);
      if (+(await rd('game.state')) !== 0) { await pad({}); for (let f = 0; f < 240 && floor === 1; f += 10) { await step(10); floor = +(await rd('game.floor')); } }
    }
  }
  await pad({});
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-end.png`) });
  return { first, waited, frames, floor: +(await rd('game.floor')), marked: +(await rd('guide.exits_marked')), keys: +(await rd('game.keys_used')) };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const r = await run(backend, 'follow');
  check('the arrow appears while the player stands (to the key: the door is shut)', r.first === 2, `kind ${r.first} after ${(r.waited / 60).toFixed(1)} s`);
  check('following only the arrow reaches floor 2', r.floor === 2 && r.keys > 0, `floor ${r.floor} after ${(r.frames / 60).toFixed(1)} s of following (keys used ${r.keys})`);
  check('the EXIT label drawn over the hatch on the way', r.marked > 0, `${r.marked} frames`);
  const c = await run(backend, 'control', 'sw guide_off 1');
  check('control: sw guide_off 1, nothing to follow, floor 1', c.first === 0 && c.floor === 1 && c.marked === 0, `kind ${c.first}, floor ${c.floor}, EXIT labels ${c.marked}`);
}
await pad({});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
