// Steering that eases in, on the CPU with a software GL (the sit-down, open water, a small swell, full throttle).
//   Six tenths of the stick puts the bars at most 45 % over (a squared response: 0.36), so a nudge corrects without a
//   twitch; the whole stick still turns the hull as hard as before. Control: sw steer_curve 0 (the stick straight
//   through, 0.6) must fail that bound.
//   No weave: one correction on the d-pad (a quarter second), then hands off; over the next 3 s the hull's turn changes
//   direction at most once (p0.yaw_flips counts sign changes of the yaw rate over 0.08 rad/s).
//   node tests/jetski-steer.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/steer'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
function run(cmds, frames, script) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, 's.png'), '--start-cmds', cmds, '--exe', EXE],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f3 = (v) => (+v).toFixed(3);
const wrap = (d) => { while (d > Math.PI) d -= 2 * Math.PI; while (d < -Math.PI) d += 2 * Math.PI; return d; };
// up to speed for 2 s, then the stick held at a fraction for 2 s: the yaw rate over its last second
const A = 120, B = 240, C = 300;
const rate = (pre, lx) => {
  const script = [{ frame: 2, buttons: ['south'] }, { frame: A, buttons: ['south'], axes: { lx } }];
  const a = run(pre + 'course 4;trial', B, script), b = run(pre + 'course 4;trial', C, script);
  return { rate: Math.abs(wrap(b['p0.yaw'] - a['p0.yaw'])) / ((C - B) / 60), bars: Math.abs(+b['p0.steer']) };
};
const full = rate('', 1), part = rate('', 0.6), full0 = rate('sw steer_curve 0;', 1), part0 = rate('sw steer_curve 0;', 0.6);
check('six tenths of the stick puts the bars at most 45 % over', part.bars <= 0.45 && part.bars > 0.2, `${f3(part.bars)} (the whole stick ${f3(full.bars)}); turning ${f3(part.rate)} against ${f3(full.rate)} rad/s`);
check('the whole stick turns as hard as without the curve', Math.abs(full.rate - full0.rate) / full0.rate < 0.03, `${f3(full.rate)} against ${f3(full0.rate)} rad/s`);
check('control: steer_curve 0 fails the bound', !(part0.bars <= 0.45), `${f3(part0.bars)}`);
// one correction on the d-pad, then hands off: flips in the 3 s after it
const tap = [{ frame: 2, buttons: ['south'] }, { frame: A, buttons: ['south', 'right'] }, { frame: A + 15, buttons: ['south'] }];
const t0 = run('course 4;trial', A + 15, tap), t1 = run('course 4;trial', A + 15 + 180, tap);
const flips = t1['p0.yaw_flips'] - t0['p0.yaw_flips'];
check('one correction, then hands off: the turn changes direction at most once in 3 s', flips <= 1, `${flips} changes of direction, ${f3(t1['p0.yaw_rate_max'])} rad/s at most`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
