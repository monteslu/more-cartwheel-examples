// The big ramps and the ways down, by real input on a software GL. A big ramp gives far more air than a normal one on the same course, and lands clean (control: a normal ramp fails the big bound). Stick and
// button in the air: a back flip, a front flip, a kick out, each paid; a flip not finished in time throws the rider. Ways down: nose down along the line of travel pierces the water, goes under, keeps its speed;
// tail first, or dropping straight down, is a wipeout; a normal landing keeps its speed. Control: dive 0, the piercing fall does not go under. Shallow water: the hull stays clear of the bed.
//   node tests/jetski-bigramp.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCourse, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/bigramp'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), TH = 'south';
mkdirSync(OUT, { recursive: true });
// (throttle after the commands: South pressed while the menu is still up starts a race on the menu's course)
function run(cmds, ticks, script = [{ frame: cmds.split(';').length + 3, buttons: [TH] }], SIMX = 12) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(Math.ceil(ticks / (SIMX + 1)) + 4), '--input', input, '--state', state, '--shot', join(OUT, 's.png'), '--start-cmds', cmds, '--exe', join(ROOT, 'build/native/games/jetski/jetski')],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '100000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-300), r.stderr.slice(-400)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(2);
const AIR_BIG = 2.7;
if (!process.argv.includes("--tricks")) for (const id of COURSE_IDS) {
  const c = readCourse(id), lv = COURSE_IDS.indexOf(id), big = c.ramps.map((r, i) => (r[5] > 0 ? i : -1)).filter((i) => i >= 0), norm = c.ramps.findIndex((r) => !(r[5] > 0));
  if (!big.length) continue;
  const n = run(`course ${lv};no_dq 1;swell 0.5;race;ramp ${norm} 40`, 480);
  for (const r of big) {
    const b = run(`course ${lv};no_dq 1;swell 0.5;race;ramp ${r} 50`, 600);
    check(`${id} big ramp ${r}: far more air`, b['p0.air_max'] >= AIR_BIG && b['p0.air_max'] >= n['p0.air_max'] * 1.25, `${f2(b['p0.air_max'])} s against ${f2(n['p0.air_max'])} s on a normal ramp, bound ${AIR_BIG}`);
    check(`${id} big ramp ${r}: lands clean`, b['p0.spills'] === 0 && b['p0.ramps'] >= 1, `${b['p0.spills']} spills, kept ${f2(b['p0.land_keep'])}`);
  }
  check(`${id} control: a normal ramp fails the bound`, n['p0.air_max'] < AIR_BIG, `${f2(n['p0.air_max'])} s`);
}
// tricks in that air: press at a moment, held stick picks the flip
const up = (lv, r, at, extra) => [{ frame: 9, buttons: [TH] }, { frame: at, buttons: [TH, ...extra.buttons], axes: extra.axes || {} }, { frame: at + 1, buttons: [TH], axes: extra.axes || {} }, { frame: at + 5, buttons: [TH] }];
const lvB = 0, rB = readCourse('atoll').ramps.findIndex((r) => r[5] > 0);
const AT = [31, 33, 35, 37, 39];
const trick = (extra, want) => { const seen = []; for (const a of AT) { const s = run(`course ${lvB};no_dq 1;swell 0.5;race;ramp ${rB} 50`, 600, up(lvB, rB, a, extra)); seen.push(`${a}:${s['p0.tricks']}t${s['p0.spills']}s${s['p0.kicks']}k`); if (want(s)) return s; } console.log('     tries', seen.join(' ')); return null; };
const back = trick({ buttons: ['west'], axes: { ly: 1 } }, (s) => s['p0.tricks'] >= 1 && s['p0.spills'] === 0);
check('a back flip finished lands clean', !!back, back ? `${back['p0.tricks']} tricks` : 'none landed');
const front = trick({ buttons: ['west'], axes: { ly: -1 } }, (s) => s['p0.tricks'] >= 1 && s['p0.spills'] === 0);
check('a front flip finished lands clean', !!front, front ? `${front['p0.tricks']} tricks` : 'none landed');
const kick = trick({ buttons: ['north'] }, (s) => s['p0.kicks'] >= 1);
check('the kick button kicks out', !!kick, kick ? `${kick['p0.kicks']} kicks, trick popups ${kick.fb[2 * 4]}` : 'none');
const none = run(`course ${lvB};no_dq 1;swell 0.5;race;ramp ${rB} 50`, 600);
check('control: no button, no trick or kick', none['p0.tricks'] === 0 && none['p0.kicks'] === 0, `${none['p0.tricks']} and ${none['p0.kicks']}`);
// a hop too short for a flip: the press is a style move and nobody is thrown; with the old rule (trick_guard 0) the flip is not finished in time and throws the rider
const shortHop = [{ frame: 9, buttons: [TH] }, { frame: 28, buttons: [TH, 'west'], axes: { ly: 1 } }, { frame: 30, buttons: [TH] }];
const safe = run('course 0;no_dq 1;race;go 400;drop 0.05 2 25', 70, shortHop, 0);
check('a press on a hop too short for a flip is a style move, not a wipeout', safe['p0.spills'] === 0 && safe['p0.styles'] === 1 && safe['p0.tricks'] === 0, `${safe['p0.spills']} spills, ${safe['p0.styles']} style moves`);
const late = run('sw trick_guard 0;course 0;no_dq 1;race;go 400;drop 0.05 2 25', 70, shortHop, 0);
check('control: trick_guard 0, a flip not finished in time throws the rider', late['p0.spills'] >= 1, `${late['p0.spills']} spills`);
// ways down, over deep open water: the human dropped at a pitch from a height at a speed
const DEEP = 'course 4;no_dq 1;race;go 200';
// (the stick holds the nose: left alone the hull settles level in the air)
const held = (ly) => [{ frame: 9, buttons: [TH], axes: { ly } }, { frame: 30, buttons: [TH] }];
const dive = run(`${DEEP};drop -1 22 16`, 360, held(-1)), offd = run(`sw dive 0;${DEEP};drop -1 22 16`, 360, held(-1)), tail = run(`${DEEP};drop 1.2 16 15`, 300, held(1)), flat = run(`${DEEP};drop 0 30 4`, 180), norm = run(`${DEEP};drop 0.05 3 25`, 150);
check('nose down along its line: it pierces and goes under', dive['p0.dives'] === 1 && dive['p0.dive_max'] > 1 && dive['p0.dive_under'] > 0.3 && dive['p0.spills'] === 0, `${dive['p0.dives']} dive, ${f2(dive['p0.dive_max'])} m down, ${f2(dive['p0.dive_under'])} s under, kept ${f2(dive['p0.land_keep'])}, speed after ${f2(dive['p0.speed'])}`);
check('control: dive 0, the same fall does not go under', offd['p0.dives'] === 0 && offd['p0.dive_max'] === 0, `${offd['p0.dives']} dives, ${offd['p0.spills']} spills, kept ${f2(offd['p0.land_keep'])}`);
check('tail first: a wipeout', tail['p0.spills'] === 1 && tail['p0.dives'] === 0, `${tail['p0.spills']} spills, ${tail['p0.dives']} dives`);
check('dropping straight down flat: a wipeout', flat['p0.spills'] === 1 && flat['p0.dives'] === 0, `${flat['p0.spills']} spills, ${flat['p0.dives']} dives`);
check('a normal landing keeps its speed', norm['p0.spills'] === 0 && norm['p0.dives'] === 0 && norm['p0.land_keep'] >= 0.9, `kept ${f2(norm['p0.land_keep'])}`);
process.exit(fail ? 1 : 0);
