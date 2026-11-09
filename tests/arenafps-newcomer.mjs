// arenafps on the cart through romdev, GL and WebGPU, by the pad: a newcomer goes from power-on to the end of a first match
// and back to the setup for the next one, by what the screen shows. From the title the player presses what each screen
// asks (Play, join, the setup panel's start), keeps the defaults (Foundry deathmatch with bots), and in the match acts only
// on cues: an enemy in view (the perception field ptest: on screen and in clear sight) is turned to and fired on; a hit
// from out of view turns the player toward the red bar on the ring round the crosshair (ptest 19/20, read from what the HUD
// draws); otherwise the player walks ahead and turns away when the view stops moving (a wall). No route, no map. A pause
// and a resume on the way. When the results show, the player waits for their continue prompt (cue.over, set as it is
// drawn) and presses it. Passes when the match ended, player 1 scored, turned to the bar at least once, and the setup came
// back by the prompt.
// The player is a person, not a bot: a reaction time before turning to a newly seen enemy (two looks, about a third of a
// second), an aim that settles on a point off the body by a seeded error that grows with distance and is drawn again
// for each new target and now and then while tracking, a wobble on the stick, a trigger pulled only when that (wrong)
// point is near the crosshair, and a fifth of the looks lost to hesitation. With that error it must still frag at least
// three times in the match: a game a newcomer cannot hit anything in fails here, however well it plays under a perfect aim.
// Control: sw cues 0 (no damage bar or edges, no continue prompt on the results) must leave the same player on the results
// screen with nothing to press.
//   node tests/arenafps-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--max-secs 600] [--no-control | --control-only]
// (a match is about 25 minutes of romdev time: one run per GPU job fits its hour)
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.ARENA_SESSION || 'arenafps-newcomer', OUT = join(ROOT, 'out/arenafps-newcomer');
const SERVER = process.env.ROMDEV_URL || 'http://127.0.0.1:7331';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/arenafps.wasc')));
const MAX = 60 * +arg('--max-secs', 600);
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const HDR = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': 'arenafps-newcomer' };
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(`${SERVER}/tool/host`, { method: 'POST', headers: HDR, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`${SERVER}/tool/${name}`, { method: 'POST', headers: HDR, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const one = async (n) => { const v = await rd(n); return Array.isArray(v) ? +v[0] : +v; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0 = {}) => tool('input', { op: 'set', ports: [p0] });
const press = async (b) => { await pads({ [b]: true }); await step(4); await pads(); await step(14); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const wrap = (a) => { while (a > Math.PI) a -= 2 * Math.PI; while (a < -Math.PI) a += 2 * Math.PI; return a; };
const PT_HEAD = 32;
/* the person's error (seeded, so a run is repeatable): mulberry32 and a normal from two uniforms */
let seed = 0x5eed1;
const rand = () => { seed |= 0; seed = (seed + 0x6d2b79f5) | 0; let t = Math.imul(seed ^ (seed >>> 15), 1 | seed); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
const gauss = () => Math.sqrt(-2 * Math.log(1 - rand())) * Math.cos(2 * Math.PI * rand());

async function run(backend, tag, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(120);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await tool('wasm', { op: 'command', text: 'ptest on' }); /* the perception field only: nothing drives the pad but this script */
  await press('south'); await step(20);  /* the title: Play */
  await press('south');                  /* the join screen: join */
  await press('start'); await step(40);  /* on to the setup panel */
  const r = { state0: await one('game.state'), mode: await one('game.mode'), arena: await one('game.arena'), turned: 0, fired: 0, stuck_turns: 0, paused: 0, resumed: 0 };
  /* the setup panel: its prompt says fire or Start to begin */
  for (let i = 0; i < 6 && await one('game.state') !== 1; i++) { await pads({ r: true, axes: { rt: 1 } }); await step(4); await pads(); await step(20); }
  r.started = await one('game.state') === 1;
  let f = 0, last = null, still = 0, turn = 0, pauseDone = false, ended = -1;
  let tgt = -1, seen = 0, offY = 0, offP = 0;
  seed = 0x5eed1; /* the same person in the run and its control */
  while (f < MAX) {
    const st = await one('game.state');
    if (st !== 1) { ended = f; break; }
    if (!pauseDone && f > 60 * 40) { /* a pause and back: Start, the pause menu, back out of it */
      await press('start'); await step(20);
      r.paused = await one('game.ticks');
      await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-pause.png`) });
      await press('east'); await step(30);
      r.resumed = (await one('game.ticks')) > r.paused ? 1 : 0;
      pauseDone = true;
    }
    const p = await rd('ptest');
    const alive = p[1] > 0.5, pos = [p[4], p[6]];
    const input = { axes: {} };
    if (alive) {
      /* the nearest enemy on screen */
      let best = -1, bd = 1e9;
      for (let i = 1; i < 8; i++) { const s = p.slice(PT_HEAD + i * 4, PT_HEAD + i * 4 + 4); if (s[0] > 0.5 && s[3] < bd) bd = s[3], best = i; }
      if (best !== tgt) { tgt = best, seen = 0; }
      if (best > 0) seen++;
      if (best > 0 && (seen === 1 || rand() < 0.08)) { /* a new target, or a fresh misjudgement while tracking: an aim point off the body */
        const k = 0.035 + Math.min(bd, 40) * 0.0016;
        offY = gauss() * k, offP = gauss() * k * 0.7;
      }
      if (best > 0 && seen >= 2 && rand() > 0.2) { /* after the reaction time; a fifth of the looks lost to hesitation */
        const s = p.slice(PT_HEAD + best * 4, PT_HEAD + best * 4 + 4), yawErr = s[1] + offY, pitchErr = s[2] + offP;
        /* yaw grows to the left (yaw 0 faces -Z): the right stick turns right for a target on the right (negative error) */
        input.axes.rx = Math.max(-1, Math.min(1, -yawErr * 2.2 + gauss() * 0.15));
        input.axes.ry = Math.max(-1, Math.min(1, -pitchErr * 2.2 + gauss() * 0.1));
        if (Math.abs(yawErr) < 0.12 && Math.abs(pitchErr) < 0.15) { input.axes.rt = 1; input.r = true; r.fired++; }
        input.axes.ly = bd > 12 ? -0.6 : 0;
      } else if (best > 0) {
        /* still reacting: the hands keep doing what they did */
      } else if (p[19] > 0.2) { /* hit from somewhere: turn toward the bar on the ring */
        const a = wrap(p[20]);
        input.axes.rx = Math.max(-1, Math.min(1, -a * 2.0));
        r.turned++;
      } else {
        /* walk ahead; a view that stops moving is a wall: turn away for a moment */
        if (last && Math.hypot(pos[0] - last[0], pos[1] - last[1]) < 0.4) still++; else still = 0;
        if (still >= 2 && turn <= 0) { turn = 4; r.stuck_turns++; }
        input.axes.ly = -1;
        if (turn > 0) { input.axes.rx = 0.9; turn--; }
        input.axes.ry = Math.max(-1, Math.min(1, p[3] * 2)); /* back to level */
      }
      last = pos;
    }
    await pads(input); await step(10); f += 10;
    if (f % 1800 === 0) console.log(`    ${backend} ${tag} ${f / 60}s frags ${p[11]} deaths ${p[18]} turned ${r.turned}`);
  }
  await pads();
  r.ended = ended;
  const sc = await rd('fighters.score'), dt = await rd('fighters.deaths');
  r.frags = sc[0], r.deaths = dt[0];
  await step(30);
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-results.png`) });
  /* the results: wait for their prompt, then press what it names; nothing else */
  r.prompt = 0;
  for (let w = 0; w < 60 * 12; w += 15) {
    if (await one('cue.over')) { r.prompt = 1; await pads({ r: true, axes: { rt: 1 } }); await step(6); await pads(); await step(20); break; }
    await step(15);
  }
  r.back = await one('game.state') === 0;
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-after.png`) });
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (!process.argv.includes('--control-only')) {
  const a = await run(backend, 'newcomer');
  console.log(`    ${JSON.stringify(a)}`);
  check('from the title to a match by the prompts', a.state0 === 0 && a.started, `setup reached (state ${a.state0}), match started ${a.started}`);
  check('pause and resume', a.paused > 0 && a.resumed === 1, `paused at tick ${a.paused}, resumed ${a.resumed}`);
  check('the first match played to its end', a.ended >= 0, `ended after ${(a.ended / 60).toFixed(0)} s of play`);
  check('the newcomer scored from what the screen showed, with a person\'s aim', a.frags >= 3 && a.fired > 0, `frags ${a.frags}, deaths ${a.deaths}, frames firing ${a.fired}`);
  check('the damage bar was followed', a.turned > 0, `${a.turned} turns toward the bar`);
  check('the results prompt led back to the setup', a.prompt === 1 && a.back, `prompt seen ${a.prompt}, back at the setup ${a.back}`);
  }
  if (process.argv.includes('--no-control')) continue;
  const c = await run(backend, 'control', 'sw cues 0');
  console.log(`    ${JSON.stringify(c)}`);
  check('control: sw cues 0 leaves the player on the results with nothing to follow', c.ended >= 0 && c.prompt === 0 && !c.back && c.turned === 0,
    `prompt seen ${c.prompt}, back ${c.back}, turns toward a bar ${c.turned}, frags ${c.frags}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
