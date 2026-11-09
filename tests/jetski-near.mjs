// Speed you can see: things pass close. In the first minute of a race on every course (the computer steering the
// player's craft, throttle held), the lines of floats strung out from the marks must bring at least 20 of them within
// 15 m of the player (fx.near_floats). Control, which must fail: sw floats 0 (no floats, the open water as released).
//   node tests/jetski-near.mjs [course ...]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/near'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
const COURSES = process.argv.slice(2).length ? process.argv.slice(2).map(Number) : [0, 1, 2, 3, 4];
const TICKS = 216 + 3600, SIMX = 24;
function run(cmds) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify([{ frame: 4, buttons: ['south'] }]));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski_auto', '--frames', String(Math.ceil(TICKS / (SIMX + 1)) + 4), '--input', input, '--state', state, '--shot', join(OUT, 's.png'), '--start-cmds', cmds, '--exe', join(ROOT, 'build/native/games/jetski/jetski_auto')],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '280000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-300), r.stderr.slice(-400)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
for (const c of COURSES) {
  const s = run(`course ${c};no_dq 1;race`), n = +s['fx.near_floats'], sec = (+s['race.clock']) || 0;
  check(`course ${c}: floats pass close`, n >= 20, `${n} floats within 15 m in the first ${sec.toFixed ? (+sec).toFixed(0) : sec} s of racing`);
  const k = run(`sw floats 0;course ${c};no_dq 1;race`), kn = +k['fx.near_floats'];
  check(`course ${c} control: sw floats 0 brings none`, kn < 20, `${kn}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
