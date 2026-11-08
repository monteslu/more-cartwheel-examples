// The two ends of a race, on the CPU with a software GL.
//   The start: the throttle down in the last beat of the countdown and held is a rocket start, faster off the line than a throttle held from
//   the first beat (control: held from the first beat, no rocket).
//   The finish: over the line the results wait at least 2.5 s while the camera comes round and the rivals race on for their own times; every
//   rival has a time on the results. Control: finish_hold 0 puts the results up at once.
//   node tests/jetski-finish.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/finish'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
function run(app, cmds, frames, { simx = 0, script = [], shot = 's.png' } = {}) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', app, '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, shot), '--start-cmds', cmds, '--exe', join(ROOT, `build/native/games/jetski/${app}`)],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(simx), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(2);
// the start: the grid is 3.6 s (216 ticks) from the race command; a press 20 ticks before the green light, against one held from the first beat
const GO = 2 + 216, END = GO + 90;
const late = run('jetski', 'no_dq 1;race', END, { script: [{ frame: GO - 20, buttons: ['south'] }], shot: 'rocket.png' });
const early = run('jetski', 'no_dq 1;race', END, { script: [{ frame: 6, buttons: ['south'] }], shot: 'plain.png' });
check('the throttle down in the last beat: a rocket start', late['race.rocket'] === 1, `rocket ${late['race.rocket']}, ${f2(late['p0.speed'])} m/s 1.5 s after the green light`);
check('control: held from the first beat, a plain start', early['race.rocket'] === 0, `rocket ${early['race.rocket']}, ${f2(early['p0.speed'])} m/s`);
check('a rocket start is ahead off the line', late['p0.dist'] > early['p0.dist'] + 3, `${f2(late['p0.dist'])} m against ${f2(early['p0.dist'])} m`);
// the finish: a whole race by the computer in the seat
const SIMX = 24, TICKS = 36000, frames = Math.ceil(TICKS / (SIMX + 1)) + 4;
const race = run('jetski_auto', 'course 0;no_dq 1;race', frames, { simx: SIMX, shot: 'finish.png' });
const times = race['race.finish_times'];
check('the race is run', race['race.state'] === 3, `state ${race['race.state']}, the seat finished ${race['p0.place']} in ${f2(times[0])} s`);
check('the results wait for the finish', race['race.done_after'] >= 2.5, `results ${f2(race['race.done_after'])} s after the line`);
check('every rival has a time', times.slice(1).every((t) => t > times[0] - 1e-3 || race['p0.place'] > 1) && times.every((t) => t > 0), `${times.map(f2).join(', ')}; ${race['race.real_finishes']} rivals over the line under their own power`);
const cut = run('jetski_auto', 'sw finish_hold 0;course 0;no_dq 1;race', frames, { simx: SIMX, shot: 'finish-cut.png' });
check('control: finish_hold 0, the results at once', cut['race.done_after'] < 2.5 && cut['race.state'] === 3, `results ${f2(cut['race.done_after'])} s after the line`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
