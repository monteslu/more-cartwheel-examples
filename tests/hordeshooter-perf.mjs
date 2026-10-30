// hordeshooter late-wave perf gate: hordeshooter_bench (the bots at the run's wave 24, floor 5 wave 4, with three players, about 2000
// enemies) at 1920x1080, High, natively on the test GPU (the 890M), GL and WebGPU, with the pipeline's GPU
// pass timers. Bounds the frame's GPU time (the mean of the bench's last three 120-frame windows):
//   GL      4.1 ms   (a third of the 12.2 before the baked yard; about 3.8 here)
//   WebGPU  4.1 ms   (11.1 before; about 3.8 here)
// Every run must draw everything (no "not in any program table" line) and keep about 2000 enemies.
// Controls, which must fail: GL at 2560x1440 (1.8 times the pixels: the bound is tight enough to see it), and
// WebGPU against an impossible 1 ms bound.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs). Run: node tests/hordeshooter-perf.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) { // take the suite lock and the GPU sandbox, then run this file inside it
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'hordeshooter-perf', 'node', fileURLToPath(import.meta.url)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = join(ROOT, 'build/native/project/games/hordeshooter/hordeshooter_bench');

function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '600', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', HORDE_BENCH: '1',
    CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const err = r.stderr || '';
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(err) || [])[1]);
  else if (gl) gl.check('native', err);
  const win = err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-3).map((l) => +(/gpu_total=([\d.]+)/.exec(l) || [])[1]);
  const enemies = +((/enemies=(\d+)/.exec(err.split('\n').filter((l) => l.startsWith('bench ')).slice(-1)[0] || '') || [])[1]);
  return { status: r.status, gpu: win.length ? win.reduce((a, b) => a + b, 0) / win.length : NaN, enemies,
    skipped: /in none of the app's program tables/.test(err) };
}

let bad = 0;
const check = (name, r, bound, mustFail = false) => {
  const within = r.status === 0 && !r.skipped && r.enemies > 1500 && r.gpu > 0 && r.gpu <= bound;
  const line = `${name}: GPU ${r.gpu.toFixed(2)} ms (bound ${bound}), ${r.enemies} enemies, status ${r.status}${r.skipped ? ', SKIPPED DRAWS' : ''}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}`); bad++; } else console.log(`ok   ${line}`);
};
check('GL', run('gl'), 4.1);
check('WebGPU', run('webgpu'), 4.1);
check('GL at 2560x1440 (1.8 times the pixels)', run('gl', { CW_RENDER_SCALE: '2' }), 4.1, true);
check('WebGPU against 1 ms', run('webgpu'), 1, true);
process.exit(bad ? 1 : 0);
