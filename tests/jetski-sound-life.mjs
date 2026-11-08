// Does the race sound alive? On the CPU with a software GL (the mixer runs on a dummy device; the game counts what it starts).
//   one-shots: a minute of racing from the green light, the computer in the seat on Crater Atoll at Hard, starts at least 90 sounds (55 before this pass)
//   (the hull on the chop, tricks, buoys, places, laps). Control: audio_old 1, only the sounds the game had before, must fall short.
//   music: every course plays its own loop. rivals: the three nearest rivals' engines are placed in the world, and one is within 60 m at the start.
//   node tests/jetski-sound-life.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/sound-life'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
function run(cmds, frames, simx = 0) {
  const state = join(OUT, 's.json');
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski_auto', '--frames', String(frames), '--state', state, '--shot', join(OUT, 's.png'), '--start-cmds', cmds, '--exe', join(ROOT, 'build/native/games/jetski/jetski_auto')],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(simx), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
// (one tick a frame: the sounds are started from the frame's own tick. The grid is 216 ticks; its countdown beeps are left out)
const MIN = 'course 0;no_dq 1;difficulty 2;swell 1;race';
const grid = run(MIN, 216), full = run(MIN, 216 + 3600);
const shots = full['audio.shots'] - grid['audio.shots'];
check('a minute of racing starts at least 90 sounds', shots >= 90, `${shots} in the minute after the green light (${grid['audio.shots']} on the grid)`);
const og = run(`sw audio_old 1;${MIN}`, 216), of = run(`sw audio_old 1;${MIN}`, 216 + 3600);
const old = of['audio.shots'] - og['audio.shots'];
check('control: audio_old 1, the sounds before this pass fall short', old < 90, `${old} in the same minute`);
const seen = new Set();
for (let lv = 0; lv < 5; lv++) { const s = run(`course ${lv};race`, 30); seen.add(s['audio.music']); }
check('every course plays its own music', seen.size === 5 && ![...seen].includes(-1), `music loops ${[...seen].join(', ')}`);
check('rival engines are heard from where they are', grid['audio.rivals_near'] >= 1, `${grid['audio.rivals_near']} rivals within 60 m on the grid`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
