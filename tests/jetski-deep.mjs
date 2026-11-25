// Deep water, on the CPU with a software GL (open water, a small swell): a craft settles into the water off the plane and
// rises onto it at speed, so a slow turn sinks and a launch lifts. The sit-down's middle rides at least 0.12 m deeper at
// rest than at full speed (p0.draft: the hull's middle under the surface, eased over half a second); the stand-up keeps
// its own (deeper still). Control: sw deep 0 (the sit-down on top at every speed, as it was) must fail that bound.
//   node tests/jetski-deep.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/deep'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
function run(cmds, frames, script, shot) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, shot), '--start-cmds', cmds, '--exe', EXE],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(3);
const spread = (pre, craft, tag) => {
  const rest = run(`${pre}craft ${craft};trial`, 150, [], `rest-${tag}.png`);
  const fast = run(`${pre}craft ${craft};trial`, 360, [{ frame: 2, buttons: ['south'] }], `fast-${tag}.png`);
  return { rest: +rest['p0.draft'], fast: +fast['p0.draft'], speed: +fast['p0.speed'], d: +rest['p0.draft'] - +fast['p0.draft'] };
};
const sit = spread('', 0, 'sit'), sit0 = spread('sw deep 0;', 0, 'sit-off'), stand = spread('', 1, 'stand');
check('sit-down: at least 0.12 m deeper at rest than at full speed', sit.d >= 0.12, `${f2(sit.rest)} m at rest, ${f2(sit.fast)} m at ${sit.speed.toFixed(1)} m/s`);
check('stand-up: deeper at rest than at speed, as before', stand.d >= 0.06, `${f2(stand.rest)} m at rest, ${f2(stand.fast)} m at ${stand.speed.toFixed(1)} m/s`);
check('control: deep 0 fails the bound', !(sit0.d >= 0.12), `${f2(sit0.rest)} m at rest, ${f2(sit0.fast)} m at full speed`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
