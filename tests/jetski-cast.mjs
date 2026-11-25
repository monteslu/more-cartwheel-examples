// The cast, on the CPU with a software GL: six riders, each rated 1 to 5 for acceleration, top speed, handling, tricks and
// weight (the bars on the title menu's Rider row). The ratings change the ride: alone on open water, full throttle down a
// straight, each rider's top speed ranks as their top-speed bars do and the fastest is at least 5 % over the slowest; the
// time to get up on the plane ranks as their acceleration bars do (more bars, sooner). Control: sw rider_stats 0 (every
// rider the same) must fail the 5 % spread.
//   node tests/jetski-cast.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/cast'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
const SIMX = 0; /* (the trial's measures run on the frame's own tick) */
function run(cmds, ticks, tag) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify([{ frame: 4, buttons: ['south'] }]));
  const frames = ticks + 4;
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, tag + '.png'), '--start-cmds', cmds, '--exe', EXE],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
// the bars, as the game draws them (game.h RIDER_STATS): accel, top, handling, tricks, weight
const BARS = [[3, 3, 3, 3, 3], [2, 5, 2, 3, 3], [5, 2, 4, 2, 2], [2, 4, 2, 2, 5], [3, 2, 3, 5, 2], [4, 2, 5, 2, 2]];
const ride = (pre) => BARS.map((_, r) => { const s = run(`${pre}sw rider ${r};course 4;trial 0.05`, 720, `r${r}${pre ? '-off' : ''}`); return { top: +s.trial[0], plane: +s.trial[6] }; });
const now = ride(''), off = ride('sw rider_stats 0;');
const spread = (a) => Math.max(...a) / Math.min(...a) - 1;
/* ranks agree: any rider with more bars is not slower (ties in bars may land either way) */
const agrees = (vals, bars, better) => bars.every((b, i) => bars.every((c, j) => !(b > c) || better(vals[i], vals[j])));
const tops = now.map((x) => x.top), planes = now.map((x) => x.plane);
console.log('  top speed m/s: ' + tops.map((v) => v.toFixed(2)).join(' ') + '; to the plane s: ' + planes.map((v) => v.toFixed(2)).join(' '));
check('the fastest rider tops the slowest by at least 5 %', spread(tops) >= 0.05, `${(100 * spread(tops)).toFixed(1)} %`);
check('top speeds rank as the top-speed bars', agrees(tops, BARS.map((b) => b[1]), (a, b) => a >= b - 0.05), tops.map((v) => v.toFixed(1)).join(', '));
check('the time to the plane ranks as the acceleration bars', planes.every((p) => p > 0) && agrees(planes, BARS.map((b) => b[0]), (a, b) => a <= b + 0.02), planes.map((v) => v.toFixed(2)).join(', '));
check('control: rider_stats 0, every rider the same, fails the spread', !(spread(off.map((x) => x.top)) >= 0.05), `${(100 * spread(off.map((x) => x.top))).toFixed(1)} %`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
