// Riding the water does not punish you. A computer rider in the human's seat, a whole race on each course at the biggest sea, no tricks: of its landings, the median keeps over 0.97
// of its speed and under 5% keep less than 0.9. Control: fair 0, the sea's hops priced as deliberate landings, must fail. Second control: a nose dive asked for still costs over 15%.
//   node tests/jetski-landing-fair.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/fair'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), SIMX = 24;
mkdirSync(OUT, { recursive: true });
const C = ['Crater Atoll', 'Cold Sound', 'Red Reach', 'Iron Quay', 'Gale Bank'];
function run(app, cmds, ticks) {
  const state = join(OUT, 's.json');
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', app, '--frames', String(Math.ceil(ticks / (SIMX + 1)) + 4), '--state', state, '--shot', join(OUT, 's.png'), '--start-cmds', cmds, '--exe', join(ROOT, 'build/native/games/jetski', app)],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '280000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-300), r.stderr.slice(-400)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
const stats = (h) => { const n = h.reduce((a, b) => a + b, 0); let seen = 0, med = 0; for (let i = 0; i < h.length; i++) { seen += h[i]; if (seen * 2 >= n) { med = i / 20; break; } } const low = h.slice(0, 18).reduce((a, b) => a + b, 0); return { n, med, low: n ? low / n : 0, mean: n ? h.reduce((a, b, i) => a + b * (i + 0.5) / 20, 0) / n : 0 }; };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
for (let lv = 0; lv < C.length; lv++) {
  const a = stats(run('jetski_auto', `course ${lv};no_dq 1;swell 1;race`, 16000)['land.hist']);
  check(`${C[lv]}: landings are fair`, a.n >= 5 && a.med >= 0.95 && (a.low < 0.05 || a.low * a.n <= 1), `${a.n} landings, median in the ${a.med.toFixed(2)} band, ${(a.low * 100).toFixed(1)}% under 0.9, mean ${a.mean.toFixed(2)}`);
  if (lv < 2) { const b = stats(run('jetski_auto', `fair 0;course ${lv};no_dq 1;swell 1;race`, 16000)['land.hist']); check(`${C[lv]} control: fair 0 fails`, b.low >= 0.05, `${b.n} landings, ${(b.low * 100).toFixed(1)}% under 0.9, mean ${b.mean.toFixed(2)}`); }
}
const dive = run('jetski', 'course 0;race;go 300;drop -0.7 2 25', 180);
check('control: a nose dive still costs', dive['p0.land_keep'] < 0.85, `kept ${(+dive['p0.land_keep']).toFixed(2)}`);
process.exit(fail ? 1 : 0);
