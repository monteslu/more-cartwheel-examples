// The race radio, on the CPU with a software GL: a whole race on Crater Atoll with the computer in the seat (four sim
// ticks a frame). The crew and the riders speak, but sparsely: at least three lines in the race, at least 8 s between
// any two, never the same line twice running, and every line shown as a subtitle too (so the radio's volume at 0 keeps
// the words). Control: sw radio_gap 0 (no gap of the radio's own) must fail the 8 s bound.
//   node tests/jetski-radio.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/radio'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
const SIMX = 4, TICKS = 18000, frames = Math.ceil(TICKS / (SIMX + 1)) + 4;
function run(cmds, tag) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, '[]');
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski_auto', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, tag + '.png'), '--start-cmds', cmds, '--exe', process.env.JETSKI_AUTO_EXE || join(ROOT, 'build/native/games/jetski/jetski_auto')],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '1200000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const r = run('course 0;no_dq 1;race', 'radio').radio.map(Number), r0 = run('sw radio_gap 0;course 0;no_dq 1;race', 'radio-off').radio.map(Number);
const said = (v) => `${v[0]} lines, ${v[4]} of them riders' calls, the least gap ${v[0] > 1 ? v[1].toFixed(1) + ' s' : 'none'}, ${v[2]} repeated, ${v[3]} shown`;
check('the radio speaks in a race, sparsely', r[0] >= 3 && r[1] >= 8 && r[2] === 0, said(r));
check('every line is shown as a subtitle', r[3] === r[0], said(r));
check('control: radio_gap 0 fails the 8 s bound', !(r0[1] >= 8), said(r0));
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
