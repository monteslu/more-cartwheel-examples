// Play a deathmatch on romdev with the scripted human on player 1's pad: the dual cart is loaded on the romdev server
// (WebGPU by default, --gl for the GL path, always the low-power GPU), the title, join and setup screens are passed with
// pad presses, then every 6 frames the script reads what the cart's scripted human (src/main.c, debug field `ptest`,
// command `ptest casual|skilled`) wants on the pad and holds exactly that on romdev's pad: sticks, trigger, jump, sprint,
// next weapon. Everything the fighter does comes in through the pad like a person's input.
//
//   node games/arenafps/tools/playtest-human.mjs [--human casual|skilled] [--arena 0|1|2] [--level 0|1|2|3] [--assist 0|1]
//        [--gl] [--shots dir] [--every 600] [--max-frames 30000] [--session name] [--server http://127.0.0.1:7331]
//
// Prints one line per 10 s of play (score, deaths, player 1's accuracy) and a final result line as JSON.
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const has = (k) => process.argv.includes(k);
const SERVER = arg('--server', 'http://127.0.0.1:7331');
const SESSION = arg('--session', `arenafps-human-${process.pid}`);
const human = arg('--human', 'casual'), arena = +arg('--arena', 0), level = +arg('--level', 1), assist = +arg('--assist', 1);
const cart = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/arenafps.wasc')));
const shots = arg('--shots', ''), every = +arg('--every', 600), maxFrames = +arg('--max-frames', 30000);
if (shots) mkdirSync(shots, { recursive: true });

async function tool(name, body) {
  for (let attempt = 0; ; attempt++) {
    const r = await fetch(`${SERVER}/tool/${name}`, { method: 'POST', headers: { 'content-type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(body) });
    const j = await r.json();
    if (j && j.error) {
      if (attempt < 2 && /busy|timeout/i.test(j.error)) continue;
      throw new Error(`${name}: ${j.error}`);
    }
    return j;
  }
}
const read = async (name) => (await tool('wasm', { op: 'read', name })).values ?? (await tool('wasm', { op: 'read', name })).value;
const readOne = async (name) => { const v = await read(name); return Array.isArray(v) ? v[0] : v; };
const press = async (button, frames = 4) => { await tool('input', { op: 'press', button, frames }); await tool('frame', { op: 'step', frames: 20 }); };

const load = await tool('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(has('--gl') ? { wgpu: false } : {}) });
console.error(`loaded ${load.gpu} on ${load.gpuAdapter?.device ?? 'GL'}`);
await tool('frame', { op: 'step', frames: 180 });
// title and join: South until the setup screen takes input (the shell's join), then the setup rows
await press('south'); await press('south'); await press('start'); await tool('frame', { op: 'step', frames: 60 });
// setup: row 0 arena, 1 mode, 2 bots, 3 level, 4 aim assist; the defaults are Foundry, deathmatch, AUTO, NORMAL, assist ON
const right = async (n) => { for (let i = 0; i < n; i++) await press('right'); };
const down = async () => press('down');
await down();                          /* the setup opens on START; down wraps to the arena row */
await right(arena); await down();      /* arena */
await down();                          /* mode: deathmatch */
await down();                          /* bots: AUTO */
await right((level - 1 + 4) % 4); await down(); /* bot level from NORMAL */
if (!assist) await right(1);
// start: fire
let state = await readOne('game.state');
for (let i = 0; i < 8 && state !== 1; i++) { await tool('input', { op: 'set', ports: [{ r: true }] }); await tool('frame', { op: 'step', frames: 4 }); await tool('input', { op: 'set', ports: [{}] }); await tool('frame', { op: 'step', frames: 20 }); state = await readOne('game.state'); }
if (state !== 1) throw new Error(`the match did not start (game.state ${state})`);
const gotArena = await readOne('game.arena');
if (gotArena !== arena) throw new Error(`the setup chose arena ${gotArena}, wanted ${arena}`);
console.error(`match started: arena ${gotArena}, ${await readOne('match.nf')} fighters`);
await tool('wasm', { op: 'command', text: `ptest ${human}` });

let frames = 0, lastLog = 0, n = 0;
const t0 = Date.now();
while (frames < maxFrames) {
  const p = await read('ptest');
  if (p[0] === 2) break; /* the results */
  const o = p.slice(24, 32);
  if (has('--trace') && frames % 30 === 0) {
    const vis = [];
    for (let i = 1; i < 16; i++) if (p[32 + i * 4] > 0.5) vis.push(`${i}:${p[33 + i * 4].toFixed(2)},${p[34 + i * 4].toFixed(2)},${p[35 + i * 4].toFixed(0)}m`);
    console.error(`t${p[17]} al${p[1]} yaw ${p[2].toFixed(2)} pit ${p[3].toFixed(2)} hp ${p[7]} rt ${p[13].toFixed(2)} ${p[14].toFixed(1)} vis [${vis.join(' ')}] out ${o.map((x) => x.toFixed(2)).join(' ')}`);
  }
  const pad = { axes: { lx: o[0], ly: o[1], rx: o[2], ry: o[3], rt: o[4] > 0.5 ? 1 : 0 }, r: o[4] > 0.5, south: o[5] > 0.5, l3: o[6] > 0.5, north: o[7] > 0.5 };
  await tool('input', { op: 'set', ports: [pad] });
  if (shots && frames % every === 0) await tool('frame', { op: 'stepAndShot', frames: 6, path: join(shots, `${human}-a${arena}-${String(n++).padStart(3, '0')}.png`) });
  else await tool('frame', { op: 'step', frames: 6 });
  frames += 6;
  if (frames - lastLog >= 600) {
    lastLog = frames;
    const st = await read('p1.stats');
    console.log(JSON.stringify({ t: +(p[17] / 60).toFixed(0), p1: p[11], best_bot: p[12], deaths: p[18], acc: st[0] ? Math.round(100 * st[1] / st[0]) : 0, wall_s: Math.round((Date.now() - t0) / 1000) }));
  }
}
await tool('input', { op: 'set', ports: [{}] });
const sc = await read('fighters.score'), nf = await readOne('match.nf'), dd = await read('fighters.deaths'), st = await read('p1.stats');
const scores = sc.slice(0, nf), best = Math.max(...scores.slice(1));
if (shots) await tool('frame', { op: 'stepAndShot', frames: 30, path: join(shots, `${human}-a${arena}-result.png`) });
console.log(JSON.stringify({ result: true, host: load.gpu, human, arena, level, assist, p1: scores[0], best_bot: best, deaths: dd[0], win: scores[0] > best,
  near: scores[0] >= Math.max(...scores) - 2, scores, mins: +(await readOne('game.ticks') / 3600).toFixed(1), shots: st[0], hits: st[1], dealt: st[2], taken: st[3],
  frames, wall_s: Math.round((Date.now() - t0) / 1000) }));
