// A newcomer mashing the trick button, by real input on a software GL. The computer steers the human seat (sw autosteer 1) round a whole lap of
// Crater Atoll on its biggest sea at Hard while the pad presses the trick button every other frame: every swell hop gets a press. A press on a hop too
// short to finish a trick is a style move (a hand off the bar), never a wipeout. Control: trick_guard 0, the old rule, the same lap and the same presses
// must come down mid-trick. A ramp still takes a full trick: the same mash up a ramp turns one, lands it and throws nobody.
//   node tests/jetski-trick-mash.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCourse } from '../games/jetski/tools/course-read.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/trick-mash'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), TH = 'south';
mkdirSync(OUT, { recursive: true });
const SIMX = 3;
function run(cmds, frames, script) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, 's.png'), '--start-cmds', cmds, '--exe', join(ROOT, 'build/native/games/jetski/jetski')],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '600000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-300), r.stderr.slice(-400)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
// throttle held from after the commands; the trick button down on every other frame from `from`
const mash = (from, frames) => { const s = [{ frame: 9, buttons: [TH] }]; for (let f = from; f < frames; f++) s.push({ frame: f, buttons: f % 2 ? [TH] : [TH, 'west'] }); return s; };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const LAP = 'course 0;no_dq 1;difficulty 2;swell 1;sw autosteer 1;race';
const LAP_FRAMES = 2100; // 3.6 s of countdown and a lap of Crater Atoll at (SIMX + 1) ticks a frame, with room
const lap = run(LAP, LAP_FRAMES, mash(12, LAP_FRAMES));
const presses = lap['p0.tricks'] + lap['p0.styles'];
check('a lap of mashing: the lap is done', lap['p0.lap'] >= 1, `lap ${lap['p0.lap']}, ${lap['p0.passes']} buoys, ${lap['p0.misses']} missed`);
check('a lap of mashing: presses landed in the air', presses >= 8, `${lap['p0.tricks']} tricks and ${lap['p0.styles']} style moves`);
check('a lap of mashing: no wipeout from a trick', lap['p0.trick_wipes'] === 0, `${lap['p0.trick_wipes']} landings mid-trick, ${lap['p0.spills']} spills in all`);
const old = run(`sw trick_guard 0;${LAP}`, LAP_FRAMES, mash(12, LAP_FRAMES));
check('control: trick_guard 0, the same mashing comes down mid-trick', old['p0.trick_wipes'] >= 1, `${old['p0.trick_wipes']} landings mid-trick, ${old['p0.spills']} spills, lap ${old['p0.lap']}`);
const rB = readCourse('atoll').ramps.findIndex((r) => r[5] > 0);
const ramp = run(`course 0;no_dq 1;swell 0.5;race;ramp ${rB} 50`, 200, mash(12, 200));
check('a ramp still takes a full trick under the same mashing', ramp['p0.tricks'] >= 1 && ramp['p0.trick_wipes'] === 0 && ramp['p0.spills'] === 0, `${ramp['p0.tricks']} tricks, ${ramp['p0.styles']} style moves, ${ramp['p0.spills']} spills, air ${(+ramp['p0.air_max']).toFixed(2)} s`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
