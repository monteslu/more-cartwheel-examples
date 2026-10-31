// jetski's ramps never float: a whole race's worth of ticks at the biggest sea of every difficulty on every course, the ramps' sides
// measured against the water every tick at nine points each. The skirt's foot over the water is a gap: there must be none. How far the
// pontoon's belly comes clear is reported and bounded. Control: the ramps lifted 2.1 m, and the check must see gaps.
//   node tests/jetski-ramps.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/ramps'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
const SIMX = 12, TICKS = 24000, BELLY_LIMIT = +(process.env.RAMP_BELLY_LIMIT || 0.35);
let n = 0;
function run(cmds) {
  const tag = 'r' + n++, state = join(OUT, tag + '.json'), frames = Math.ceil(TICKS / (SIMX + 1)) + 4;
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--state', state, '--shot', join(OUT, tag + '.png'), '--start-cmds', cmds, '--exe', process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski')], {
    cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26,
    env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '280000' },
  });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const D = ['easy', 'normal', 'hard'], C = ['Crater Atoll', 'Cold Sound'];
for (let c = 0; c < C.length; c++) for (let d = 0; d < 3; d++) {
  const s = run(`course ${c};difficulty ${d};swell 1;race;ramp 0 400 300`);
  const at = `${s['ramp.points']} points; foot over water ${s['ramp.gap_points']}, by ${s['ramp.gap_max'].toFixed(2)} m at most; belly clear at ${s['ramp.belly_points']}, by ${s['ramp.belly_max'].toFixed(2)} m at most; steepest ${(Math.atan(s['ramp.tilt_max']) * 57.3).toFixed(1)} deg`;
  check(`${C[c]} ${D[d]}`, s['race.tick'] > TICKS * 0.9 && s['ramp.points'] > TICKS && s['ramp.gap_points'] === 0 && s['ramp.belly_max'] <= BELLY_LIMIT, at);
}
const lifted = run('course 1;difficulty 2;swell 1;ramp_lift 2.1;race;ramp 0 400 300');
check('control: lifted 2.1 m, the check sees gaps', lifted['ramp.gap_points'] > 1000, `${lifted['ramp.gap_points']} points over the water, by ${lifted['ramp.gap_max'].toFixed(2)} m`);
process.exit(fail ? 1 : 0);
