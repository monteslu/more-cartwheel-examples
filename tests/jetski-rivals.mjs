// The computer riders ride on their own ratings and in deep water, on the CPU with a software GL: a championship race on
// Crater Atoll, the computer in the player's seat too (sw autosteer 1). Each rival's pump, top speed, turn and trick speed
// (rider.k) are the multipliers its rider's bars give (the cast: game.h RIDER_STATS), not the even 3s; and off the plane
// a rival settles into the water: the deepest a rival under 4 m/s has sat is at least 0.3 m (on top of the water it sits about 0.22 m in).
// Control: sw ai_rated 0 (the computer riders on even ratings and on top of the water, as before) must fail both.
//   node tests/jetski-rivals.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/rivals'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
function run(cmds, frames, tag) {
  const state = join(OUT, tag + '.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify([{ frame: 4, buttons: [] }]));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, tag + '.png'), '--start-cmds', cmds, '--exe', EXE],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
// the bars (game.h RIDER_STATS) and the multipliers they give (sim.c craft_tick): pump 7 %, top 3 %, turn 4 % (a player 7 %), tricks 8 % a bar off 3
const BARS = [[3, 3, 3, 3, 3], [2, 5, 2, 3, 3], [5, 2, 4, 2, 2], [2, 4, 2, 2, 5], [3, 2, 3, 5, 2], [4, 2, 5, 2, 2]];
const want = (r) => [1 + 0.07 * (BARS[r][0] - 3), 1 + 0.03 * (BARS[r][1] - 3), 1 + 0.04 * (BARS[r][2] - 3), 1 + 0.08 * (BARS[r][3] - 3)]; // (a rival turns on its handling at 4 % a bar)
const race = (pre, tag) => run(`${pre}mode 0;course 0;no_dq 1;race;sw autosteer 1`, 1500, tag);
const judge = (s) => {
  const k = s['rider.k'].map(Number), rated = [1, 2, 3, 4, 5].every((seat) => want(seat).every((v, j) => Math.abs(k[seat * 4 + j] - v) < 1e-3)); // (rider 0 picked: seat n rides rider n)
  return { rated, k: [1, 2, 3, 4, 5].map((seat) => k.slice(seat * 4, seat * 4 + 4).map((v) => v.toFixed(2)).join('/')).join(' '), draft: +s['ai.draft_max'] };
};
const on = judge(race('sw rider 0;', 'on')), off = judge(race('sw rider 0;sw ai_rated 0;', 'off'));
check('every rival rides on its own ratings', on.rated, on.k);
check('a slow rival settles into the water', on.draft >= 0.3, `deepest ${on.draft.toFixed(3)} m under 4 m/s`);
check('control: sw ai_rated 0, the rivals on even ratings and on top (fails both)', !off.rated && off.draft < 0.3, `${off.k}; deepest ${off.draft.toFixed(3)} m`);
process.exit(fail ? 1 : 0);
