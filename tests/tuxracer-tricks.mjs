// tuxracer's trick mode (the Practice card's Tricks row), on the cart through romdev, GL and WebGPU:
//   spin     holding the stick to one side through a jump spins Tux (the drawn yaw passes half a turn in the air)
//   clean    a landing within 50 degrees of whole turns pays (one spin 150, one flip 300), one per landing
//   bad      a landing part way round pays nothing and counts as a bad landing
//   score    the run's score is the original's formula plus the trick points
//   frames   a spin and a flip are saved mid-air to look at
// Controls, which must fail: trick mode off, the same jumps pay nothing and the score is the original's formula alone;
// in the Cup the row has no effect.
//   node tests/tuxracer-tricks.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/tuxracer-tricks');
const SESSION = process.env.TUXRACER_SESSION || 'tuxracer-tricks', AGENT = process.env.TUXRACER_AGENT || 'tuxracer-tricks';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/tuxracer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT };
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
// (node:http, not fetch: fetch gives up on a reply that takes five minutes, and a busy GPU can take that long)
const post = (name, body) => new Promise((ok, no) => {
  const q = request(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: H }, (res) => {
    let t = '';
    res.setEncoding('utf8');
    res.on('data', (c) => (t += c));
    res.on('end', () => ok({ ok: res.statusCode === 200, text: () => t }));
  });
  q.on('error', no);
  q.end(body);
});
const tool = async (name, args) => {
  let r;
  try { r = await post(name, JSON.stringify(args)); }
  catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  const t = r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (text) => tool('wasm', { op: 'command', text });
const step = (frames) => tool('frame', { op: 'step', frames });
const pad = (p) => tool('input', { op: 'set', ports: [p] });
const ST_RACING = 6, ST_RACE_OVER = 8, PAR = 40;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const trick = async () => (await rd('trick')).map(Number);

/** a charged jump (X held a third of a second, let go), holding `air` through the flight; the most yaw and pitch drawn */
async function jump(air, shot) {
  await pad({ west: true }); await step(20); await pad(air);
  let yaw = 0, pitch = 0;
  for (let f = 0; f < 90; f += 3) {
    await step(3);
    const t = await trick();
    yaw = Math.max(yaw, Math.abs(t[4])), pitch = Math.max(pitch, Math.abs(t[5]));
    if (shot && f === 18) await tool('frame', { op: 'stepAndShot', frames: 1, path: shot });
  }
  await pad({}); await step(30);
  return { yaw, pitch };
}

async function run(backend, tricks) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: process.env.CW_GPU_POWER || 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(120);
  await cmd(`opt tricks ${tricks ? 'on' : 'off'}`);
  await cmd('race bunny_hill');
  for (let f = 0; f < 1800 && +(await rd('game.state')) !== ST_RACING; f += 30) await step(30);
  await step(60);
  const tag = `${backend}-${tricks ? 'on' : 'off'}`;
  const r = {};
  r.spin = await jump({ right: true }, join(OUT, `${tag}-spin.png`));
  r.after_spin = await trick();
  await cmd('trick set 360 0'); r.clean_spin = await jump({}); r.t1 = await trick();
  await cmd('trick set 0 360'); r.flip = await jump({}, join(OUT, `${tag}-flip.png`)); r.t2 = await trick();
  await cmd('trick set 200 0'); await jump({}); r.t3 = await trick();
  /* on to the line (placed 3 m before it: holding paddle alone runs him into a tree short of it), then the score */
  await cmd('tp finish 3');
  for (let f = 0; f < 60 * 20 && +(await rd('game.state')) === ST_RACING; f += 60) { await pad({ south: true }); await step(60); }
  await pad({});
  r.state = +(await rd('game.state'));
  r.score = +(await rd('race.score')), r.time = +(await rd('race.time')), r.herring = +(await rd('race.herring'));
  r.final = await trick();
  await step(40);
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${tag}-results.png`) });
  await tool('host', { op: 'shutdown' });
  return r;
}
const formula = (r) => Math.max(0, Math.trunc(100 * (PAR - r.time) + 200 * r.herring));

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const on = await run(backend, true);
  check('spin: the stick held through a jump spins him', on.spin.yaw >= 180, `${on.spin.yaw} degrees drawn in the air`);
  check('clean: a whole spin lands for 150, a whole flip for 300', on.t1[3] === 150 && on.t2[3] === 300, `last landings ${on.t1[3]}, ${on.t2[3]}; clean ${on.t2[1]}`);
  check('bad: a landing part way round pays nothing', on.t3[2] >= 1 && on.t3[0] === on.t2[0], `bad landings ${on.t3[2]}, points ${on.t2[0]} then ${on.t3[0]}`);
  check('score: the original formula plus the trick points', on.state === ST_RACE_OVER && Math.abs(on.score - (formula(on) + on.final[0])) <= 100,
    `score ${on.score}, formula ${formula(on)} + tricks ${on.final[0]} (state ${on.state})`);
  const off = await run(backend, false);
  check('control: trick mode off, the same jumps pay nothing', off.final[0] === 0 && off.spin.yaw === 0, `points ${off.final[0]}, yaw drawn ${off.spin.yaw}`);
  check('control: trick mode off, the score is the original formula', off.state === ST_RACE_OVER && Math.abs(off.score - formula(off)) <= 100, `score ${off.score}, formula ${formula(off)}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
