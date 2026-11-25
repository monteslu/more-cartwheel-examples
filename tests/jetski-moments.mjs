// The race's big moments, on the CPU with a software GL.
//   Stamps: a turbo (as a lap, the last lap, a trick or a combo) is stamped across the middle of the picture in big type
//   that swells in from 1.3x and fades: at least 150 px of type at 1080p, where the old popup was 54. Control: sw stamp_on 0
//   (the old popup) must fail the size bound. That they never strobe is jetski-flash's (a rocket start, a turbo and the
//   finish, frame by frame).
//   Finish poses: over the line in the top three, both gloves go up over the helmet within 1.5 s; further back the
//   shoulders and head drop (the torso thrown at least 0.2 rad further forward than without the pose). Both on the
//   sit-down and the stand-up. Control: sw pose_on 0 must fail both.
//   Standings: every rider down the left in race order, sliding to a new place and glowing once when it changes. Control:
//   sw standings 0 (the old two neighbours) draws no column.
//   node tests/jetski-moments.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/moments'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
function run(cmds, frames, shot) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, '[]');
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, shot), '--start-cmds', cmds, '--exe', EXE],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (v === undefined ? 'none' : (+v).toFixed(2));
const arr = (v) => (Array.isArray(v) ? v.map(Number) : [0, 0, 0, 0]);

// stamps: a turbo fired at speed, 0.4 s on (past the swell, before the fade)
const TURBO = 'no_dq 1;race;go 300;sw stock 2;sw turbo 3';
const st = run(TURBO, 26, 'stamp.png'), st0 = run('sw stamp_on 0;' + TURBO, 26, 'stamp-off.png');
const s = arr(st['hud.stamp']), s0 = arr(st0['hud.stamp']);
check('a turbo is stamped big across the picture', s[0] >= 150 && s[1] > 0.5 && +st['hud.stamps'] >= 1, `type ${f2(s[0])} px at 1080p, alpha ${f2(s[1])}, ${st['hud.stamps']} stamped`);
check('control: stamp_on 0 (the old popup) fails the size bound', !(s0[0] >= 150), `type ${f2(s0[0])} px`);
// the standings: every rider down the left in race order (control: standings 0, the old two neighbours, draws no column)
const sd0 = run('sw standings 0;' + TURBO, 26, 'standings-off.png');
const rows = st['hud.order'] || [], order = st['race.order'] || [];
check('the standings show all six riders in race order', +st['hud.rows'] === 6 && order.every((v, k) => v === rows[k]), `${st['hud.rows']} rows: ${rows}, race order ${order}`);
check('control: standings 0 draws no column', !(+sd0['hud.rows'] === 6), `${sd0['hud.rows']} rows`);

// finish poses: 1.5 s after the line, first and fifth, each craft, with the pose and without
for (const craft of [0, 1]) {
  const name = craft ? 'stand-up' : 'sit-down', base = `craft ${craft};no_dq 1;race;go 300`;
  const win = run(`${base};finish`, 90, `win-${craft}.png`), win0 = run(`sw pose_on 0;${base};finish`, 90, `win-off-${craft}.png`);
  const lose = run(`${base};finish 5`, 90, `lose-${craft}.png`), lose0 = run(`sw pose_on 0;${base};finish 5`, 90, `lose-off-${craft}.png`);
  const w = arr(win['p0.finish_pose']), w0 = arr(win0['p0.finish_pose']), l = arr(lose['p0.finish_pose']);
  check(`${name}: over the line in the top three, gloves over the helmet within 1.5 s`, w[1] <= 3 && w[0] <= 1.5 && w[2] > 0.05, `place ${w[1]}, ${f2(w[0])} s after the line, the higher glove ${f2(w[2])} m over the head's top`);
  check(`${name}: control: pose_on 0 keeps the gloves on the bars`, !(w0[2] > 0.05), `the higher glove ${f2(w0[2])} m over the head's top`);
  const dt = +lose['rider.torso'] - +lose0['rider.torso'];
  check(`${name}: fifth over the line, the shoulders and head drop`, l[1] >= 4 && l[2] < 0 && dt >= 0.2, `place ${l[1]}, torso ${f2(lose['rider.torso'])} against ${f2(lose0['rider.torso'])} rad without the pose (+${f2(dt)})`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
