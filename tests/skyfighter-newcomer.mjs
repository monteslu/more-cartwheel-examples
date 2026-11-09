// A newcomer's first sorties, on the cart through romdev, by the pad and the screen alone. From power-on the d-pad
// picks the mission on the title's grid and A (guns) starts it; then the pilot flies at the cue the HUD puts on
// screen (the reticle on the selected target, the objective ahead, or the objective marker at the screen's edge),
// holds the guns down when the sight is on it, taps the missile when the reticle says LOCK, pulls up on PULL UP and
// drops flares on MISSILE INBOUND. The pilot is a person, not a perfect bot: it acts on what the screen showed 0.3 s
// before (reaction delay), its aim wanders by up to 3 % of the screen and jitters every look (aim error), and it
// overshoots, as a stick held a beat too long does.
//   Country Fair (mission 1): all twelve balloons down and the mission won inside 7 minutes.
//   Coastal Scramble (mission 4): bandits downed, and all ten interceptors cleared (the mission won) inside 7 minutes.
// Controls, which must fail: the same pilot with the stick never moved (guns and missile only, flying straight on);
// and the same pilot under the old rules (sw ram_crash 1: flying into a balloon ends the sortie; sw late_pull_up 1:
// PULL UP only 45 m above the ground), the build a newcomer could not finish.
//   node tests/skyfighter-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--only fair|scramble]
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/skyfighter-newcomer');
const SESSION = process.env.SKY_SESSION || 'skyfighter-newcomer', AGENT = process.env.SKY_AGENT || 'skyfighter-newcomer';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/skyfighter.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const ONLY = arg('--only', '');
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT };
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pad = (p) => tool('input', { op: 'set', ports: [p] });
const PLAY = 1, WIN = 3, LOST = 4, EVERY = 6, DELAY = 3; /* a look every 6 frames; the pilot acts on the look 3 back (0.3 s) */
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

async function sortie(backend, mission, seconds, done, fly, cmds = [], tag = '') {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  for (const c of cmds) await tool('wasm', { op: 'command', text: c });
  await step(120);
  const trace = [];
  /* the title's grid: back to the first tile, then right and down to the mission, then A */
  for (let k = 0; k < 4; k++) { await pad({ up: true }); await step(4); await pad({}); await step(6); await pad({ left: true }); await step(4); await pad({}); await step(6); }
  for (let k = 0; k < mission % 4; k++) { await pad({ right: true }); await step(4); await pad({}); await step(6); }
  if (mission >= 4) { await pad({ down: true }); await step(4); await pad({}); await step(6); }
  const picked = +(await rd('mission.level'));
  await pad({ south: true }); await step(4); await pad({}); await step(30);
  const started = +(await rd('mission.state'));
  let seed = 0x9e3779b9 ^ mission, driftX = 0, driftY = 0, over = 0, overX = 0, overY = 0, lastMissile = -999, lastFlare = -999;
  const rand = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
  const seen = [];
  let frames = 0, state = started, result = null;
  while (frames < seconds * 60) {
    const c = (await rd('hud.cues')).map(Number);
    seen.push(c);
    const [sx, sy, kind, cx, cy, locked, pullup, incoming] = seen.length > DELAY ? seen.shift() : seen[0];
    driftX = clamp(driftX + (rand() - 0.5) * 0.006, -0.03, 0.03);
    driftY = clamp(driftY + (rand() - 0.5) * 0.006, -0.03, 0.03);
    const ax = cx + driftX + (rand() - 0.5) * 0.012, ay = cy + driftY + (rand() - 0.5) * 0.012;
    const p = {};
    let lx = 0, ly = 0;
    if (fly) {
      if (pullup) ly = -1;                                  /* PULL UP: climb */
      else if (kind === 3) lx = ax < 0.5 ? -1 : 1;          /* the objective is off screen: bank round to its marker */
      else if (kind) { lx = clamp((ax - sx) / 0.10, -1, 1); ly = clamp((ay - sy) / 0.10, -1, 1); }
      if (over > 0) { lx = overX; ly = overY; over--; }     /* a stick held a beat too long */
      else if (Math.abs(lx) > 0.6 && rand() < 0.06) { over = 2; overX = lx; overY = ly; }
      if (Math.abs(lx) < 0.08) lx = 0;
      if (Math.abs(ly) < 0.08) ly = 0;
      if (lx || ly) p.axes = { lx, ly };
    }
    if ((kind === 1 || kind === 2) && Math.abs(ax - sx) < 0.03 && Math.abs(ay - sy) < 0.045) p.south = true; /* sight on it: guns */
    if (locked && frames - lastMissile > 70) { p.west = true; lastMissile = frames; }                        /* LOCK: missile */
    if (incoming && frames - lastFlare > 90 && rand() < 0.7) { p.back = true; lastFlare = frames; }            /* MISSILE INBOUND: flares, most times */
    await pad(p); await step(EVERY); frames += EVERY;
    if (frames % 60 === 0) {
      state = +(await rd('mission.state'));
      result = await done();
      trace.push({ s: frames / 60, state, alt: +(await rd('jet.altitude')), cue: c.map((v) => +v.toFixed(3)), ...result });
      if (result.ok || state === WIN || state === LOST) break;
    }
  }
  const name = `${backend}-m${mission}-${fly ? 'newcomer' : 'control'}${tag}`;
  writeFileSync(join(OUT, `${name}.json`), JSON.stringify(trace));
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${name}.png`) });
  await pad({});
  result = await done();
  const r = { picked, started, state, seconds: frames / 60, ...result };
  await tool('host', { op: 'shutdown' });
  return r;
}

const fair = async () => { const b = +(await rd('mission.balloons')), s = +(await rd('mission.state')); return { ok: s === WIN, balloons: b, health: +(await rd('mission.health')) }; };
const scramble = async () => { const k = +(await rd('mission.kills')), s = +(await rd('mission.state')); return { ok: s === WIN, kills: k, health: +(await rd('mission.health')) }; };
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (ONLY !== 'scramble') {
    const n = await sortie(backend, 0, 420, fair, true);
    check('Country Fair: the d-pad and A start it', n.picked === 0 && n.started === PLAY, `mission ${n.picked}, state ${n.started}`);
    check('Country Fair: a newcomer with human aim clears all twelve balloons', n.ok, `${n.balloons} balloons, state ${n.state}, ${n.seconds.toFixed(0)} s, health ${n.health}`);
    const c = await sortie(backend, 0, 150, fair, false);
    check('Country Fair control: the stick never moved, the fair is not cleared', !c.ok && c.balloons < n.balloons, `${c.balloons} balloons in ${c.seconds.toFixed(0)} s`);
    const o = await sortie(backend, 0, 420, fair, true, ['sw ram_crash 1', 'sw late_pull_up 1'], '-old');
    check('Country Fair control: under the old rules the same newcomer does not finish', !o.ok, `${o.balloons} balloons, state ${o.state}, ${o.seconds.toFixed(0)} s`);
  }
  if (ONLY !== 'fair') {
    const n = await sortie(backend, 3, 420, scramble, true);
    check('Coastal Scramble: the d-pad and A start it', n.picked === 3 && n.started === PLAY, `mission ${n.picked}, state ${n.started}`);
    check('Coastal Scramble: a newcomer with human aim downs the bandits and clears the scramble', n.kills >= 1 && n.ok, `${n.kills} bandits down in ${n.seconds.toFixed(0)} s, health ${n.health}, state ${n.state}`);
    const c = await sortie(backend, 3, 240, scramble, false);
    check('Coastal Scramble control: the stick never moved, the scramble is not cleared', !c.ok, `${c.kills} bandits down in ${c.seconds.toFixed(0)} s, state ${c.state}`);
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
