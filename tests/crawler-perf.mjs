// crawler perf gate: crawler_bench (four bots on a crowded late floor: 1800 monsters on the floor, the spawners pouring
// more) at 1920x1080, High, natively on the test GPU (the 890M), GL and WebGPU, with the pipeline's GPU pass timers.
// Bounds the frame's GPU time (the mean of the bench's last three 120-frame windows) at 4.1 ms, as hordeshooter's.
//   play: the game's own rule, a full screen (SCREEN_CAP, 120 monsters) round the team and the rest queued off it, on a
//         crowded late floor of every tileset (its own kit, bake and creatures); the screen must hold no more than the
//         cap (with a margin for the frame's edge).
//   stress: CRAWLER_NO_CAP, the whole 1800 packed into the corridors round the team on floor 58: the same bound, a gate in
//         its own right (the renderer carries a crowd far past what the game shows).
// Every run must draw everything (no "not in any program table" line) and keep the crowd. Controls, which must fail:
// GL at 3840x2160 (4 times the pixels), WebGPU against an impossible 1 ms bound, and the stress scene held to the
// play scenes' screen cap.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs). Run: node tests/crawler-perf.mjs [--quick]
// (--quick: the bench floor only, GL and WebGPU, and the controls)
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) { // take the suite lock and the GPU sandbox, then run this file inside it
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'crawler-perf', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const quick = process.argv.includes('--quick');
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/crawler_bench.json'), 'utf8')).binary;

function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '600', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', CRAWLER_BENCH: '1',
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
  const last = err.split('\n').filter((l) => l.startsWith('bench ')).slice(-1)[0] || '';
  return { status: r.status, gpu: win.length ? win.reduce((a, b) => a + b, 0) / win.length : NaN, enemies: +((/enemies=(\d+)/.exec(last) || [])[1]),
    drawn: +((/drawn=(\d+)/.exec(last) || [])[1]), onScreen: +((/on_screen=(\d+)/.exec(last) || [])[1]), skipped: /in none of the app's program tables/.test(err), passes: (err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-1)[0] || '') };
}

let bad = 0;
/* SCREEN_CAP (game.h): spawns never take the screen past it, but the team walking onto a waiting queue can carry it up
 * to half again over (these scenes fill the floor one a tile, a queue everywhere); and the monsters the frame's edge
 * margin adds to the drawn count */
const CAP = 120 * 1.5, CAP_EDGE = 120;
const check = (name, r, bound, mustFail = false, capped = false) => {
  const crowd = capped ? 300 : 1200; /* the floor's total (the play scenes space theirs one a tile) */
  const within = r.status === 0 && !r.skipped && r.enemies > crowd && r.gpu > 0 && r.gpu <= bound && (!capped || (r.onScreen <= CAP && r.drawn <= CAP + CAP_EDGE));
  const line = `${name}: GPU ${r.gpu.toFixed(2)} ms (bound ${bound}), ${r.enemies} monsters (${r.onScreen} on screen, ${r.drawn} drawn), status ${r.status}${r.skipped ? ', SKIPPED DRAWS' : ''}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}\n     ${r.passes}`); bad++; } else console.log(`ok   ${line}`);
};
const STRESS = { CRAWLER_NO_CAP: '1' };
check('play floor 58 GL', run('gl'), 4.1, false, true);
check('play floor 58 WebGPU', run('webgpu'), 4.1, false, true);
if (!quick)
  for (const floor of [18, 38, 78, 98, 118]) /* a crowded late floor of every other tileset */
    for (const gpu of ['gl', 'webgpu']) check(`play floor ${floor} ${gpu === 'gl' ? 'GL' : 'WebGPU'}`, run(gpu, { CRAWLER_FLOOR: String(floor) }), 4.1, false, true);
const sg = run('gl', STRESS), sw = run('webgpu', STRESS);
check('stress floor 58 GL (no cap)', sg, 4.1);
check('stress floor 58 WebGPU (no cap)', sw, 4.1);
check('GL at 3840x2160 (4 times the pixels)', run('gl', { ...STRESS, CW_RENDER_SCALE: '3' }), 4.1, true);
check('WebGPU against 1 ms', sw, 1, true);
check('the stress scene held to the screen cap', sw, 4.1, true, true);
process.exit(bad ? 1 : 0);
