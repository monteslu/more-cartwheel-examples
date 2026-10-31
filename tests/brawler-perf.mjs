// brawler perf: brawler_auto at 1920x1080 natively on the test GPU (the 890M), GL and WebGPU, with the pipeline's GPU pass timers, on
// one fixed load: four bots and a crowd of 14 kept on the street (BRAWLER_CROWD), each stage. Prints per-stage mean GPU ms of the last
// windows, triangles, draws and the passes. --bound <ms> checks every run against it; --exp oldfx is the earlier settings (the control: it must exceed the bound). Run: node tests/brawler-perf.mjs [--stages 0,1,2,3] [--gpus gl,webgpu] [--frames 720]
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'brawler-perf', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const arg = (k, d) => { const i = process.argv.indexOf('--' + k); return i > 0 ? process.argv[i + 1] : d; };
const EXTRA = Object.fromEntries((arg('env', '') || '').split(',').filter(Boolean).map((kv) => kv.split('=')));
const BOUND = +arg('bound', process.argv.length <= 2 ? 5 : 0); // no arguments: the gate: every stage on both backends within 5 ms GPU; the control (the earlier settings) must exceed it
let bad = 0;
const exe = join(ROOT, 'build/native/games/brawler/brawler_auto');
function run(gpu, stage, frames, extra = {}) {
  const env = { ...process.env, CW_FRAMES: String(frames), SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', BRAWLER_BENCH: '1', BRAWLER_PLAYERS: '4', BRAWLER_CROWD: '14', BRAWLER_STAGE: String(stage),
    BRAWLER_SOAK: '1', CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...EXTRA, ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const t0 = Date.now(), r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const lines = (r.stderr || '').split('\n').filter((l) => l.startsWith('bench ')).slice(2);
  const num = (l, k) => +((new RegExp(k + '=([\\d.]+)').exec(l) || [])[1]);
  const avg = (k) => lines.reduce((a, l) => a + num(l, k), 0) / Math.max(1, lines.length);
  return { status: r.status, gpu: avg('gpu_total'), tris: avg('tris'), draws: avg('draws'), enemies: avg('enemies'), cpu3d: avg('cpu3d_ms'), wallMs: (Date.now() - t0) / frames, last: lines[lines.length - 1] || (r.stderr || '').slice(-400) };
}
for (const exp of arg('exp', '').split('/'))
 for (const gpu of arg('gpus', 'gl,webgpu').split(','))
  for (const st of arg('stages', '0,1,2,3').split(',')) {
    const r = run(gpu, +st, +arg('frames', 720), exp ? { BRAWLER_EXP: exp } : {});
    console.log(`${exp || 'base'} ${gpu} stage ${st}: gpu ${r.gpu.toFixed(2)} ms, tris ${Math.round(r.tris)}, draws ${Math.round(r.draws)}, enemies ${r.enemies.toFixed(1)}, cpu3d ${r.cpu3d.toFixed(2)}, status ${r.status}`);
    console.log('  ' + r.last);
    if (BOUND && !(exp === 'oldfx' ? r.gpu > BOUND : r.gpu > 0 && r.gpu <= BOUND)) { console.log(`FAIL ${exp || 'base'} ${gpu} stage ${st}: ${r.gpu.toFixed(2)} ms against ${BOUND}`); bad++; }
  }
if (process.argv.length <= 2) { // the control that must fail: the earlier settings on the docks
  const c = run('gl', 1, +arg('frames', 720), { BRAWLER_EXP: 'oldfx' });
  if (c.gpu > BOUND) console.log(`ok   control fails as it must: oldfx ${c.gpu.toFixed(2)} ms`); else { console.log(`CONTROL PASSED (a failure): oldfx ${c.gpu.toFixed(2)} ms`); bad++; }
}
process.exit(bad ? 1 : 0);
