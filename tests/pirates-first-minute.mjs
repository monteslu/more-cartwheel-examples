// pirates on the cart through romdev, GL and WebGPU, by the pad: a new captain's first minute and a half. Player one
// starts a Cooperative Voyage from the title and sails as someone who has not learned the controls yet: the stick held
// forward with a slow turn one way and then the other, no cannons fired, nothing banked. The patrol and the merchant
// leave a voyage captain alone until it has banked once or fired first, and its harbor keeps it safe out to 15 m, so
// its hull must stay at 100 the whole time.
// Controls, which must fail: sw voyage_hunt 1 (the patrol hunts from the start, as it once did) must cost the same
// captain hull in the same frames. (A Treasure Race leg was dropped: one captain sailing this way is not shot there in
// 90 s either, so it could not show the voyage-only rule; the race's 7 m anchorage rule is unchanged in can_hit.)
//   node tests/pirates-first-minute.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.PIRATES_SESSION || 'pirates-first-minute', OUT = join(ROOT, 'out/pirates-first-minute');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/pirates.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const HEADERS = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': process.env.ROMDEV_AGENT || 'pirates-gates' };
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: HEADERS, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: HEADERS, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0) => tool('input', { op: 'set', ports: [p0, {}] });
const press = async (b) => { await pads({ [b]: true }); await step(4); await pads({}); await step(8); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

const FRAMES = 60 * 90;
async function run(backend, tag, { pre }) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press('start'); await step(20);              /* "set sail" */
  const r = { mode: +(await rd('pirates.mode')), humans: +(await rd('pirates.humans')), minHp: 100, sunk: 0, shots: 0 };
  let was = 100;
  for (let f = 0; f < FRAMES; f += 30) {
    /* forward, with a slow turn one way and then the other: a newcomer finding the stick */
    await pads({ axes: { lx: 0.45 * Math.sin(f / 240), ly: -1 } });
    await step(30);
    const hp = (await rd('pirates.hp'))[0];
    r.minHp = Math.min(r.minHp, hp);
    if (hp === 0 && was > 0) r.sunk++;
    was = hp;
  }
  await pads({});
  r.bank = (await rd('pirates.bank'))[0];
  r.shots = +(await rd('pirates.shots'));
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const v = await run(backend, 'voyage', {});
  check('a newcomer\'s first 90 s of the voyage: never shot', v.mode === 0 && v.humans === 1 && v.minHp === 100 && v.sunk === 0 && v.bank === 0,
    `mode ${v.mode}, ${v.humans} captain, lowest hull ${v.minHp}, sunk ${v.sunk}, banked ${v.bank}, shots in the match ${v.shots}`);
  const c = await run(backend, 'control-hunt', { pre: 'sw voyage_hunt 1' });
  check('control: sw voyage_hunt 1, the patrol hunts the same captain', c.mode === 0 && c.minHp < 100, `lowest hull ${c.minHp}, sunk ${c.sunk}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
