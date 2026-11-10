// Tux stands on the snow and slides on his belly, on the cart through romdev, GL and WebGPU. The game reports how far
// above the snow it draws his left sole, his right sole and his belly (debug tux.ground, mm: the lowest of each part's
// samples against the course height under it, as drawn). The walk-in on Bunny Hill: while he walks the lower sole is on
// the snow (-15 to +8 mm: the walk-in lifts him so a leg swung back never puts its toes into the snow), standing (keys 2.5 to 3.2 s) both soles are (-15 to
// +15 mm); racing, paddling down the first slope, his belly is on the snow (the median -40 to +40 mm, never below -80).
// Control: TUXRACER_TUX_LIFT=0.06 (the model drawn 6 cm up) must fail, or the check could not see a floating Tux.
//   node tests/tuxracer-grounding.mjs [--gl] [--webgpu] [--cart <path>] [--no-control]
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/tuxracer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const SESSION = process.env.TUXRACER_SESSION || 'tuxracer-grounding', AGENT = process.env.TUXRACER_AGENT || 'tuxracer-grounding';
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT };
let calls = 0;
const post = (name, body) => new Promise((ok, no) => {
  calls++;
  const q = request(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: H }, (res) => {
    let t = ''; res.setEncoding('utf8'); res.on('data', (c) => (t += c)); res.on('end', () => ok({ ok: res.statusCode === 200, t }));
  });
  q.on('error', no); q.end(body);
});
const bye = async (code) => { try { await post('host', '{"op":"shutdown"}'); } catch {} console.log(`${calls} requests`); process.exit(code); };
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
const tool = async (name, args) => {
  let r; try { r = await post(name, JSON.stringify(args)); } catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  let j; try { j = JSON.parse(r.t); } catch { j = r.t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${r.t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (t) => tool('wasm', { op: 'command', text: t });
const step = (n) => tool('frame', { op: 'step', frames: n });
const hold = (buttons, n) => tool('input', { op: 'sequence', steps: [{ input: { ports: [Object.fromEntries(buttons.map((b) => [b, true]))] }, frames: n }] });
const ST_INTRO = 5, ST_RACING = 6;
const median = (a) => { const s = [...a].sort((x, y) => x - y); return s.length ? s[s.length >> 1] : NaN; };

async function run(backend, lift) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: process.env.CW_GPU_POWER || 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (lift) await cmd(`env TUXRACER_TUX_LIFT=${lift}`);
  await cmd('race bunny_hill');
  for (let f = 0; f < 1200 && +(await rd('game.state')) !== ST_INTRO; f += 10) await step(10);
  /* the walk-in: a look every 5 frames (one step of 5, one read) from its first frame */
  const walk = [], stand = [];
  await step(1); /* the walk-in's first tick measured */
  for (let f = 0; f < 165; f += 5) {
    const g = await rd('tux.ground');
    if (+g[3] !== ST_INTRO) break;
    if (f <= 100) walk.push(Math.min(+g[0], +g[1]));
    if (f >= 125 && f <= 160) stand.push([+g[0], +g[1]]);
    await step(5);
  }
  for (let f = 0; f < 300 && +(await rd('game.state')) !== ST_RACING; f += 10) await step(10);
  /* racing: paddling down the first slope, a look every 10 frames, ten seconds */
  const belly = [];
  for (let f = 0; f < 600; f += 10) {
    await hold(['south'], 10);
    const g = await rd('tux.ground');
    if (+g[3] === ST_RACING) belly.push(+g[2]);
  }
  await tool('host', { op: 'shutdown' });
  return { walk, stand, belly };
}

function judge(r) {
  const out = [];
  const wlo = Math.min(...r.walk), whi = Math.max(...r.walk);
  out.push({ name: 'walking, the lower sole on the snow', ok: r.walk.length >= 15 && wlo >= -15 && whi <= 8, d: `${r.walk.length} looks, ${wlo} to ${whi} mm` });
  const s = r.stand.flat(), slo = Math.min(...s), shi = Math.max(...s);
  out.push({ name: 'standing, both soles on the snow', ok: r.stand.length >= 6 && slo >= -15 && shi <= 15, d: `${r.stand.length} looks, ${slo} to ${shi} mm` });
  const bm = median(r.belly), blo = Math.min(...r.belly);
  out.push({ name: 'sliding, the belly on the snow', ok: r.belly.length >= 40 && bm >= -40 && bm <= 40 && blo >= -80, d: `${r.belly.length} looks, median ${bm} mm, lowest ${blo} mm` });
  return out;
}

let fail = 0;
for (const backend of BACKENDS) {
  const r = await run(backend, 0);
  for (const c of judge(r)) { console.log(`${c.ok ? 'ok  ' : 'FAIL'} ${backend} ${c.name}: ${c.d}`); if (!c.ok) fail++; }
  if (!process.argv.includes('--no-control')) {
    const k = await run(backend, 0.06);
    const bad = judge(k).filter((c) => !c.ok);
    const caught = bad.length === 3;
    console.log(`${caught ? 'ok  ' : 'FAIL'} ${backend} control (drawn 6 cm up) caught by every check: ${judge(k).map((c) => `${c.ok ? 'passed' : 'failed'} ${c.d}`).join('; ')}`);
    if (!caught) fail++;
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
