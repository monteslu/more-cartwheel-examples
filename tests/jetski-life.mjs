// Life in the water and over it, on the CPU with a software GL: in the first minute of a race on every course (the
// computer steering the player's craft, throttle held), schools of fish lie beside the line in clear water and the
// player passes close to at least 3 of them (life[4], within 20 m) and scatters at least 2 (life[2], within 13 m).
// Gulls rest near the line (on a beach, or afloat where there is none) on at least 4 of the 5 courses and lift off as
// the racers pass, at least 3 times over the five (life[3]).
// Control, which must fail: sw life 0 (the open water as before) places, passes and lifts nothing.
//   node tests/jetski-life.mjs [course ...]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/life'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
const COURSES = process.argv.slice(2).length ? process.argv.slice(2).map(Number) : [0, 1, 2, 3, 4];
const TICKS = 216 + 3600, SIMX = 24;
function run(cmds, tag) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify([{ frame: 4, buttons: ['south'] }]));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski_auto', '--frames', String(Math.ceil(TICKS / (SIMX + 1)) + 4), '--input', input, '--state', state, '--shot', join(OUT, tag + '.png'), '--start-cmds', cmds, '--exe', join(ROOT, 'build/native/games/jetski/jetski_auto')],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '280000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-300), r.stderr.slice(-400)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0, flockCourses = 0, lifts = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
for (const c of COURSES) {
  const s = run(`course ${c};no_dq 1;race`, `c${c}`), L = s.life || [];
  const [schools, flocks, scatters, lifted, passed, deep] = L.map(Number);
  console.log(`  course ${c}: ${schools} schools (beds to ${(+deep).toFixed(1)} m), ${flocks} flocks; in ${(+s['race.clock']).toFixed(0)} s the player passed ${passed} schools, scattered ${scatters}, ${lifted} flocks lifted`);
  check(`course ${c}: schools beside the line, passed close and scattered`, schools >= 6 && passed >= 3 && scatters >= 2, `${schools} schools, ${passed} passed within 20 m, ${scatters} scattered`);
  if (flocks > 0) flockCourses++;
  lifts += lifted;
  const k = run(`sw life 0;course ${c};no_dq 1;race`, `c${c}-off`), K = (k.life || []).map(Number);
  check(`course ${c} control: sw life 0 places and passes none`, !(K[0] >= 6 && K[4] >= 3 && K[2] >= 2), `${K[0]} schools, ${K[4]} passed, ${K[2]} scattered`);
}
if (COURSES.length === 5) check('gulls near the line on at least 4 courses lift off as the racers pass', flockCourses >= 4 && lifts >= 3, `${flockCourses} courses with gulls, ${lifts} lift-offs`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
