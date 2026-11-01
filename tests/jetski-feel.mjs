// Does the race answer? Run on the CPU with real input on the game's own counters.
//  feedback: each thing the rider did was answered on at least two of popup, sound, rumble, burst. Control: ev_on 0.
//  turbo: charged only past full power; a shot adds speed and field of view; a replay with it covers more water. Controls: empty stock fires nothing, a buoy under full power charges nothing.
//  pack: held place changes and a near rival over a whole race, a stopped human back with them. Control: pack 0.25, the small pull as released.
//  place: riders alongside do not flip the place shown. Control: distance in sample steps with no hold.
//  crash: square into a ramp's side, moving again quickly and answered. Control: old_crash 1, the dead stop.
//   node tests/jetski-feel.mjs [feedback turbo pack place crash]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/feel'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
const K = ['PASS', 'TOP', 'TRICK', 'LAND', 'MISS', 'GAIN', 'LOSE', 'LAP', 'FINAL', 'WALL', 'CRASH', 'CHARGE', 'TURBO'], TH = 'south';
const WANT = process.argv.slice(2);
const on = (n) => !WANT.length || WANT.includes(n);
function run(app, cmds, ticks, simx = 12, buttons = [TH]) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify(typeof buttons[0] === 'string' ? [{ frame: 4, buttons }] : buttons));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', app, '--frames', String(Math.ceil(ticks / (simx + 1)) + 4), '--input', input, '--state', state, '--shot', join(OUT, 's.png'), '--start-cmds', cmds, '--exe', join(ROOT, 'build/native/games/jetski', app)],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(simx), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '280000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-300), r.stderr.slice(-400)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(2);
if (on('feedback')) {
  const total = new Array(K.length * 4).fill(0), add = (s) => s.fb.forEach((v, i) => { total[i] += v; });
  add(run('jetski_auto', 'course 0;no_dq 1;race', 8000));
  add(run('jetski_auto', 'course 1;no_dq 1;difficulty 2;race', 15000, 24));
  add(run('jetski', 'course 0;race;ramp 1 30', 480, 12, [{ frame: 4, buttons: [TH] }, ...[15, 18, 21, 24, 27].flatMap((a) => [{ frame: a, buttons: [TH, 'west'] }, { frame: a + 1, buttons: [TH] }])]));
  add(run('jetski', 'course 0;race;go 500;sw ramp_side 0', 400));
  add(run('jetski', 'course 0;race;go 500;sw ramp_side 0 0.3', 400));
  add(run('jetski', 'course 0;race;go 500;sw stock 2;sw turbo 3', 200));
  add(run('jetski', 'course 0;race;go 500', 900, 12, []));
  for (let k = 0; k < K.length; k++) {
    const row = total.slice(k * 4, k * 4 + 4), channels = row.filter((v) => v > 0).length;
    check(`${K[k]} answered`, channels >= 2, `popup ${row[0]}, sound ${row[1]}, rumble ${row[2]}, burst ${row[3]}`);
  }
  const off = run('jetski_auto', 'sw ev_on 0;course 0;no_dq 1;race', 3000);
  check('control: ev_on 0 answers nothing', off.fb.reduce((a, b) => a + b, 0) === 0, `${off.fb.reduce((a, b) => a + b, 0)} answers`);
}
if (on('turbo')) {
  const base = run('jetski', 'course 2;race;go 600;sw meter 5', 45), shot = run('jetski', 'course 2;race;go 600;sw meter 5;sw stock 1;sw turbo 3', 45);
  check('a turbo adds speed within 0.75 s', shot['p0.speed'] - base['p0.speed'] >= 6 && shot['p0.turbos'] === 1, `${f2(base['p0.speed'])} without, ${f2(shot['p0.speed'])} with, ${shot['p0.turbos']} fired`);
  // (no extra sim ticks here: the camera moves once a frame)
  const fb = run('jetski', 'course 2;race;go 600;sw meter 5', 90, 0), fs_ = run('jetski', 'course 2;race;go 600;sw meter 5;sw stock 1;sw turbo 3', 90, 0);
  check('a turbo widens the view', fs_['cam.fov'] - fb['cam.fov'] >= 5, `${f2(fb['cam.fov'])} to ${f2(fs_['cam.fov'])} degrees`);
  const empty = run('jetski', 'course 2;race;go 600;sw meter 5;sw stock 0;sw turbo 3', 45);
  check('control: an empty stock fires nothing', empty['p0.turbos'] === 0 && Math.abs(empty['p0.speed'] - base['p0.speed']) < 1, `${empty['p0.turbos']} fired, ${f2(empty['p0.speed'])}`);
  const a = run('jetski_auto', 'course 0;no_dq 1;race;sw meter 5', 2400), b = run('jetski_auto', 'course 0;no_dq 1;race;sw meter 2', 500);
  check('past full power a buoy charges', a['p0.stock'] > 0, `stock ${f2(a['p0.stock'])}`);
  check('control: under full power a buoy charges nothing', b['p0.stock'] === 0, `stock ${f2(b['p0.stock'])}`);
  const slow = run('jetski', 'course 0;race;go 900;sw meter 5', 900), fast = run('jetski', 'course 0;race;go 900;sw meter 5;sw stock 3;sw turbo 3', 900);
  check('over a stretch a replay with a turbo covers more water', fast['p0.dist'] - 900 > (slow['p0.dist'] - 900) * 1.02, `${f2(slow['p0.dist'] - 900)} m without, ${f2(fast['p0.dist'] - 900)} m with`);
}
if (on('pack')) {
  for (const [lv, name] of [[0, 'Crater Atoll'], [1, 'Cold Sound'], [3, 'Iron Quay']]) {
    const s = run('jetski_auto', `course ${lv};no_dq 1;race`, 18000, 24), min = +s['race.clock'] / 60, near = s['race.near_sum'] / Math.max(1, s['race.near_n']);
    const c = run('jetski_auto', `sw pack 0.25;course ${lv};no_dq 1;race`, 18000, 24), cnear = c['race.near_sum'] / Math.max(1, c['race.near_n']);
    check(`${name}: a race, not a procession`, near < 30, `nearest rival ${f2(near)} m on average, ${s['p0.place_changes']} held changes of place in ${f2(min)} min; as released ${f2(cnear)} m, ${c['p0.place_changes']}`);
    check(`${name} control: the small pull leaves it further apart or quieter`, cnear > near || c['p0.place_changes'] < s['p0.place_changes'], `${f2(cnear)} m against ${f2(near)} m`);
  }
  const stop = run('jetski_auto', 'course 0;no_dq 1;race;go 800;sw hold 480', 4100, 24);
  check('stopped 8 s, back with them inside a minute', stop['race.near'] < 30, `nearest rival ${f2(stop['race.near'])} m after 68 s`);
}
if (on('place')) {
  let shown = 0, ctl = 0;
  for (const lv of [0, 1, 3]) {
    const s = run('jetski_auto', `course ${lv};no_dq 1;race`, 2800, 12), c = run('jetski_auto', `course ${lv};no_dq 1;race;sw dist_step 1;sw place_hold 0`, 2800, 12);
    check(`course ${lv}: the place shown holds still`, s['p0.place_changes'] <= 10, `${s['p0.place_changes']} changes shown in 47 s (${s['race.raw_flips']} raw); sample steps and no hold ${c['p0.place_changes']}`);
    shown += s['p0.place_changes'], ctl += c['p0.place_changes'];
  }
  check('control: sample steps and no hold flip, over the three courses', ctl >= shown * 2 && ctl >= 10, `${ctl} shown against ${shown}`);
}
if (on('crash')) {
  const courses = [[0, 'atoll'], [1, 'sound'], [2, 'canyon'], [3, 'harbour'], [4, 'storm']];
  for (const [lv, name] of courses) {
    const s = run('jetski', `course ${lv};race;go 400;sw ramp_side 2`, 400);
    check(`${name}: a crash is a crash`, s['p0.slow_max'] < 150 && s['p0.spills'] >= 1 && s.fb[10 * 4 + 1] >= 1, `longest under 3 m/s ${s['p0.slow_max']} ticks, ${s['p0.spills']} spills, sound ${s.fb[41]}, rumble ${s.fb[42]}, burst ${s.fb[43]}`);
  }
  const c = run('jetski', 'sw old_crash 1;course 0;race;go 400;sw ramp_side 2', 400);
  check('control: old_crash 1 is silent', c.fb[41] === 0 && c['p0.spills'] === 0, `sound ${c.fb[41]}, ${c['p0.spills']} spills, longest under 3 m/s ${c['p0.slow_max']} ticks`);
}
process.exit(fail ? 1 : 0);
