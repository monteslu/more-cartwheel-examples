// A ramp is fine if a rider can get to it: from just past the buoy before it, at race speed, by the craft's real handling (the sim's own steering, on its own input), over the
// ramp and then past the next buoy on its correct side, at Hard (every buoy counts) on the course's biggest sea. A rider lines a jump up sooner or later, so three ways of doing
// that are tried and one is enough. A ramp you cannot get to without missing a buoy fails. Control: the released layout's ramps far off the line (tests/jetski-ramp-reach-control.json) stood
// where they were: those must fail. Prints the failures per course; --json <file> writes them for tools/course-map.mjs.
//   node tests/jetski-ramp-reach.mjs [--json <file>] [--courses=0,1]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCourse, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/reach'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), SIMX = 12, TICKS = 1600;
mkdirSync(OUT, { recursive: true });
const WHY = { 1: 'ok', '-1': 'a buoy missed', '-2': 'never reached it', '-3': 'over it, then no buoy', 0: 'no result' };
function run(lv, r, aside, look, at = '') {
  const state = join(OUT, `t${lv}.json`), input = join(OUT, 'in.json');
  writeFileSync(input, JSON.stringify([]));
  const p = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(Math.ceil(TICKS / (SIMX + 1)) + 4), '--state', state, '--shot', join(OUT, `t${lv}.png`), '--start-cmds', `course ${lv};difficulty 2;swell 1;no_dq 1;race;ramp_try ${r} ${aside} ${look} ${at}`, '--exe', join(ROOT, 'build/native/games/jetski/jetski')],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '60000' } });
  if (p.status !== 0) { console.error(p.stdout.slice(-300), p.stderr.slice(-400)); process.exit(3); }
  const st = JSON.parse(readFileSync(state, 'utf8'));
  run.detail = `hit ${st['try.hit']}, from buoy ${st['try.prev']}, missed next-after ${st['try.missed']}`;
  return st['try.res'];
}
const only = process.argv.find((a) => a.startsWith('--courses='))?.slice(10).split(',').map(Number), out = {};
let fail = 0;
const LOOKS = [30, 18, 45], tryAll = (lv, r, at) => { let res = 0; for (const l of LOOKS) { res = run(lv, r, 0, l, at); if (res === 1) return 1; } return res; };
const CONTROL = JSON.parse(readFileSync(join(ROOT, 'tests/jetski-ramp-reach-control.json'), 'utf8'));
for (let lv = 0; lv < COURSE_IDS.length; lv++) {
  if (only && !only.includes(lv)) continue;
  const c = readCourse(COURSE_IDS[lv]), bad = [];
  c.ramps.forEach((q, r) => { const res = tryAll(lv, r); if (res !== 1) bad.push({ r, x: q[0], z: q[1], off: q[4], s: q[3], why: WHY[res] + ' (' + run.detail + ')' }); });
  out[COURSE_IDS[lv]] = bad;
  console.log(`${bad.length ? 'FAIL' : 'ok  '} ${COURSE_IDS[lv]}: ${c.ramps.length - bad.length} of ${c.ramps.length} ramps reachable${bad.length ? ': ' + bad.map((b) => `#${b.r} (${b.x.toFixed(0)},${b.z.toFixed(0)}) s ${b.s.toFixed(0)} ${b.off.toFixed(0)} m off, ${b.why}`).join('; ') : ''}`);
  if (bad.length) fail++;
  const far = CONTROL[COURSE_IDS[lv]] || [], still = far.filter((q) => tryAll(lv, 0, `${q[0]} ${q[1]} ${q[2]}`) === 1);
  console.log(`${far.length && !still.length ? 'ok  ' : 'FAIL'} ${COURSE_IDS[lv]} control: ${far.length - still.length} of ${far.length} of the released far ramps fail${still.length ? ', reachable: ' + still.map((q) => q[0].toFixed(0) + ',' + q[1].toFixed(0)).join(' ') : ''}`);
  if (!far.length || still.length) fail++;
}
const j = process.argv.indexOf('--json');
if (j > 0) writeFileSync(process.argv[j + 1], JSON.stringify(out));
process.exit(fail ? 1 : 0);
