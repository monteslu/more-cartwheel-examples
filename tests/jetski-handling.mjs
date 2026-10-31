// jetski's handling, measured: real input through the game's actions, numbers read back from the game. Each line below is a
// rubric criterion, with its own must-fail comparison. On a software GL with the sim stepping many ticks a frame:
//   node tests/jetski-handling.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/handling'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
const SIMX = 12;
let n = 0;
function run(cmds, buttons, ticks) {
  const tag = 'h' + n++, input = join(OUT, tag + '.input.json'), state = join(OUT, tag + '.json');
  writeFileSync(input, JSON.stringify([{ frame: 4, buttons }]));
  const frames = Math.ceil(ticks / (SIMX + 1)) + 4;
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, tag + '.png'), '--start-cmds', cmds, '--exe', process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski')], {
    cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26,
    env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy' },
  });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(2);
const TH = 'south';
const S = 'go 150';
const straight = run(S, [TH], 360);
const steer = run(S, [TH, 'right'], 360);
const carve = run(S, [TH, 'right', 'down'], 360);
const steer2 = run(S, [TH, 'right'], 360);
check('JS12 lean tightens the carve', carve['p0.radius'] <= steer['p0.radius'] * 0.7, `radius ${f2(steer['p0.radius'])} m, with lean ${f2(carve['p0.radius'])} m, ratio ${f2(carve['p0.radius'] / steer['p0.radius'])}`);
check('JS12 control: the same stick twice is not tighter', steer2['p0.radius'] > steer['p0.radius'] * 0.7, `ratio ${f2(steer2['p0.radius'] / steer['p0.radius'])}`);
check('JS13 too much stick costs speed', steer['p0.speed'] <= straight['p0.speed'] * 0.85, `straight ${f2(straight['p0.speed'])} m/s, full stick ${f2(steer['p0.speed'])} m/s`);
check('JS14 the rider works: torso against the hull', Math.abs(steer['rider.torso'] - carve['rider.torso']) >= 0.175, `torso ${f2(steer['rider.torso'])} rad, leaning back ${f2(carve['rider.torso'])} rad`);
const flat = run(S + ';drop 0.1 2.5 22', [TH], 150), nose = run(S + ';drop -0.7 2.5 22', [TH], 150), noseBack = run(S + ';drop -0.7 2.5 22', [TH, 'down'], 150);
check('JS15 flat landing keeps its speed', flat['p0.land_keep'] >= 0.95, `kept ${f2(flat['p0.land_keep'])}`);
check('JS15 nose-down landing loses speed', nose['p0.land_keep'] <= 0.85, `kept ${f2(nose['p0.land_keep'])}`);
check('JS15 weight back helps', noseBack['p0.land_keep'] > nose['p0.land_keep'], `kept ${f2(noseBack['p0.land_keep'])} against ${f2(nose['p0.land_keep'])}`);
const crash = run(S + ';drop -2.6 3 22', [TH], 90), after = run(S + ';drop -2.6 3 22', [TH], 90 + 200);
check('JS13 a wild landing spills', crash['p0.spills'] >= 1 && crash['p0.spill'] > 0, `spills ${crash['p0.spills']}, ${f2(crash['p0.spill'])} s left`);
check('JS13 back in control under 3 s', after['p0.spill'] <= 0 && after['p0.speed'] > 8, `${after['p0.spills']} spills, ${f2(after['p0.spill'])} s left, ${f2(after['p0.speed'])} m/s 3.3 s later`);
const out = run('go 850 1.57', [TH], 900), inb = run('go 850', [], 900);
check('JS20 five seconds outside the course ends the race', out['p0.dq'] === 2, `dq ${out['p0.dq']}, ${f2(out['p0.out_t'])} s out, at ${out['p0.pos'].map((v) => v.toFixed(0))}, ${f2(out['p0.speed'])} m/s`);
check('JS20 control: staying on the course does not', inb['p0.dq'] === 0, `dq ${inb['p0.dq']}`);
process.exit(fail ? 1 : 0);
