// Stunt rings, on the CPU with a software GL: in stunt mode a ring stands over each ramp's jump, at the peak of the arc a
// rider's usual speed off its lip takes, and flying through one pays 600 into the combo. The seat lined up with a ramp
// (ramp_try) flies through its ring, on two ramps of Crater Atoll (the second and third). Control: wxsw rings 0 (no rings) passes none.
//   node tests/jetski-rings.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/rings'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
function run(cmds, frames, tag) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify([{ frame: 2, buttons: ['south'] }]));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, tag + '.png'), '--start-cmds', cmds, '--exe', EXE],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
let passed = 0, rings = 0;
for (const r of [1, 2]) {
  const s = run(`mode 2;course 0;race;ramp_try ${r}`, 600, `ramp${r}`);
  rings = s['stunt.rings']; passed += s['stunt.rings_passed'];
  console.log(`  ramp ${r}: ${s['stunt.rings_passed']} rings flown through, ${s['p0.ramps'] ?? '?'} ramps, score ${s['stunt.score']}`);
}
const off = run('wxsw rings 0;mode 2;course 0;race;ramp_try 0', 600, 'ramp0-off');
check('stunt mode puts a ring over every ramp', rings >= 2, `${rings} rings`);
check('lined up with two ramps, the seat flies through their rings', passed >= 2, `${passed} rings flown through`);
check('control: rings 0, none', off['stunt.rings'] === 0 && off['stunt.rings_passed'] === 0, `${off['stunt.rings']} rings, ${off['stunt.rings_passed']} through`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
