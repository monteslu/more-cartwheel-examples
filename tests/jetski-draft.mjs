// The sit-down's draft, on the CPU with a software GL (open water, a small swell): where the water stands on the craft
// model at rest and at full speed (p0.keel: the water's height over the craft's origin, eased over half a second; in
// craft.glb the keel is at -0.2, the chines at -0.09, the footwells' floor at +0.13 and the gunwale at +0.2). On the
// plane the hull runs on its bottom: the water between the keel and 15 cm under the footwells (-0.2 to -0.02), so the
// rider's boots never touch it; at rest it settles deeper (the deep-water feel) but the footwells stay dry, the water
// between -0.05 and +0.08. Control: sw sit_lift 0 (the old float, the footwells under the water at speed) must fail.
//   node tests/jetski-draft.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/draft'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
function run(cmds, frames, script) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--start-cmds', cmds, '--exe', EXE],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f3 = (v) => (+v).toFixed(3);
const draft = (pre) => {
  const rest = run(`${pre}craft 0;course 4;trial`, 150, []);
  const fast = run(`${pre}craft 0;course 4;trial`, 360, [{ frame: 2, buttons: ['south'] }]);
  return { rest: +rest['p0.keel'], fast: +fast['p0.keel'], speed: +fast['p0.speed'] };
};
const ok = (d) => d.fast >= -0.2 && d.fast <= -0.02 && d.rest >= -0.05 && d.rest <= 0.08;
const on = draft(''), old = draft('sw sit_lift 0;');
check('the sit-down on the plane runs on its bottom, at rest it settles with dry footwells', ok(on), `water at ${f3(on.rest)} m on the model at rest, ${f3(on.fast)} m at ${on.speed.toFixed(1)} m/s`);
check('control: sw sit_lift 0 (the old float) fails', !ok(old), `${f3(old.rest)} m at rest, ${f3(old.fast)} m at full speed`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
