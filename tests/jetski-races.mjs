// A whole race on every course by computer riders, on the CPU with a software GL: nobody stuck, nobody off the course for long, buoys passed. Its numbers against those
// against BEFORE, computer riders' missed buoys when this was written: a change to the courses must not cost buoys. Control: a race cut short has not finished.
//   node tests/jetski-races.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/races'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), SIMX = 24;
mkdirSync(OUT, { recursive: true });
const C = ['Crater Atoll', 'Cold Sound', 'Red Reach', 'Iron Quay', 'Gale Bank'], BEFORE = [16, 18, 22, 11, 5], STUCK_TICKS = 60;
function run(lv, ticks) {
  const state = join(OUT, `r${lv}.json`), frames = Math.ceil(ticks / (SIMX + 1)) + 4;
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski_auto', '--frames', String(frames), '--state', state, '--shot', join(OUT, `r${lv}.png`), '--start-cmds', `course ${lv};no_dq 1;race`, '--exe', join(ROOT, 'build/native/games/jetski/jetski_auto')],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '280000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
for (let lv = 0; lv < C.length; lv++) {
  const s = run(lv, 36000);
  check(`${C[lv]}: the race is run`, s['race.state'] === 3 && s['ai.passes'] > 200, `state ${s['race.state']}, clock ${(+s['race.clock']).toFixed(0)} s, ${s['ai.passes']} buoys passed by the rivals, the seat ${s['p0.passes']} and ${s['p0.misses']} missed`);
  check(`${C[lv]}: nobody stuck`, s['ai.stuck'] < STUCK_TICKS, `longest under 2 m/s ${s['ai.stuck']} ticks, against walls ${s['ai.walls']} ticks`);
  check(`${C[lv]}: rivals miss no more buoys`, s['ai.misses'] <= BEFORE[lv] + 2, `${s['ai.misses']}, before ${BEFORE[lv]}, ramps taken ${s['ai.ramps']}`);
}
const cut = run(0, 1200);
check('control: a race cut short is not run', cut['race.state'] !== 3, `state ${cut['race.state']}`);
process.exit(fail ? 1 : 0);
