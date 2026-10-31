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
  // buttons: held from frame 4; or a list of { frame, buttons }, for a press at a moment
  writeFileSync(input, JSON.stringify(typeof buttons[0] === 'string' ? [{ frame: 4, buttons }] : buttons));
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
const HULL = (extra) => `difficulty 2;swell 1;${extra}go 1500`;
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
// the hull stays on the water in the biggest sea, across its faces at speed; control: on the spring alone it goes under
const onTop = run(HULL(''), [TH], 1200), under = run(HULL('ride_top 0;'), [TH], 1200);
check('the hull rides on top of the biggest sea', onTop['p0.sink_max'] <= 0.33, `deepest ${f2(onTop['p0.sink_max'])} m`);
check('control: on its spring alone it sinks', under['p0.sink_max'] > 0.5, `deepest ${f2(under['p0.sink_max'])} m`);
// a ramp on the line throws the hull high and long enough for a trick; beside it nothing happens; the trick lands
const onRamp = run('ramp 1 30', [TH], 480), beside = run('ramp 1 30 22', [TH], 480), trick = run('ramp 1 30', [{ frame: 4, buttons: [TH] }, { frame: 23, buttons: [TH, 'west'] }, { frame: 24, buttons: [TH] }], 480);
check('JT1 a ramp gives big air', onRamp['p0.ramps'] >= 1 && onRamp['p0.air_max'] >= 1.0, `${onRamp['p0.ramps']} ramp, ${f2(onRamp['p0.air_max'])} s in the air`);
check('JT1 control: beside the ramp, no ramp', beside['p0.ramps'] === 0 && beside['p0.air_max'] < onRamp['p0.air_max'] * 0.6, `${beside['p0.ramps']} ramps, ${f2(beside['p0.air_max'])} s`);
check('JT2 a trick in that air completes', trick['p0.tricks'] >= 1 && trick['p0.spills'] === 0, `${trick['p0.tricks']} tricks, ${trick['p0.spills']} spills`);
check('JT2 control: no button, no trick', onRamp['p0.tricks'] === 0, `${onRamp['p0.tricks']} tricks`);
// Cold Sound's arch: shut while the tide stands high, the short way once it has fallen
const high = run('course 1;race;tide 2.6;arch 60', [TH], 360), low = run('course 1;race;tide -1.6;arch 60', [TH], 360);
check('JS6 high tide shuts the arch', high['p0.shut_hits'] >= 1 && high['p0.shortcuts'] === 0, `${high['p0.shut_hits']} hits, ${high['p0.shortcuts']} through`);
check('JS6 low tide opens it', low['p0.shortcuts'] === 1 && low['p0.shut_hits'] === 0, `${low['p0.shut_hits']} hits, ${low['p0.shortcuts']} through`);
// the computer riders get round every course: no one stuck on land; control: the grid before it moves
for (const [lv, name] of [[0, 'Crater Atoll'], [1, 'Cold Sound']]) {
  const r0 = run(`course ${lv};race`, [], 120), r1 = run(`course ${lv};race`, [], 7000);
  check(`${name}: the riders go the whole way round`, r1['p1.dist'] > 2500 && r1['p2.dist'] > 2500, `${f2(r1['p1.dist'])} m and ${f2(r1['p2.dist'])} m after 7000 ticks`);
  check(`${name} control: not yet on the grid`, r0['p1.dist'] < 300, `${f2(r0['p1.dist'])} m`);
}
// nothing floats: the numbers come back through the game's own check of every scatter instance
for (const [lv, name] of [[0, 'Crater Atoll'], [1, 'Cold Sound']]) {
  const exe = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
  const out = (lift) => { const f = join(OUT, 'ground' + lv + lift + '.json'); const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', '6', '--state', f, '--shot', f + '.png', '--start-cmds', `course ${lv};grounding ${lift}`, '--exe', exe], { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy' } }); return JSON.parse(readFileSync(f, 'utf8')); };
  const ok = out(0), lifted = out(0.6);
  check(`${name}: nothing hangs in the air`, ok['ground.gaps'] === 0 && ok['ground.total'] > 5000, `${ok['ground.total']} instances, ${ok['ground.gaps']} with a gap, worst ${f2(ok['ground.worst'])} m`);
  check(`${name} control: lifted 0.6 m, the check sees it`, lifted['ground.gaps'] > 500, `${lifted['ground.gaps']} with a gap`);
}
process.exit(fail ? 1 : 0);
