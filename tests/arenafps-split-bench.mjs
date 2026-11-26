// arenafps splitscreen benchmark: the same autopilot match at 1, 2, 3 and 4 players (ARENA_PLAYERS), measured per frame
// on the native host (GL and WebGPU) and compared with one player. Usage as in cartwheel-examples' derby bench:
//   node deps/cartwheel-engine/tools/with-gpu-suite.mjs arenafps-split-bench node tests/arenafps-split-bench.mjs
//        [--players 1,2,3,4] [--gpus gl,webgpu] [--res 1920x1080] [--end 1200] [--window 600]
//        [--runs 5] [--pairs 7] [--shots dir] [--json out/arenafps-split-bench.json]
//        [--exes a,b]   A/B: these derby binaries instead of the build's, runs interleaved
//
// Run it through with-gpu-suite.mjs (the GPU suite lock, the test GPU, no
// desktop). Each round runs every player count twice: a clean run for the
// CPU phases and the draw calls (no GPU timers, whose polling costs the
// CPU), then a timed run (CW_GPU_TIMERS=1) for each pass's GPU time. Of the
// --runs rounds the least CPU and GPU time is kept (other work on the
// machine only ever adds time), the median wall time. The frames are paced
// flat out (no vsync); numbers are averages over the last --window frames
// before --end, all of them in the bout. CPU is the main thread's own CPU
// time per phase (sim: the fixed ticks; render: culling, recording and
// submitting the draws), so waits on the GPU stay out of it; frame is the
// wall time from frame to frame. The CPU against one player is the ratio
// within each round (its runs follow one another), the median of the rounds.
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync, existsSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const { tempDir } = await import(join(ENGINE, 'tools/lib/tmp.mjs'));

const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 && argv[i + 1] ? argv[i + 1] : d; };
const PLAYERS = opt('players', '1,2,3,4').split(',').map(Number);
const GPUS = opt('gpus', 'gl,webgpu').split(',');
const RES = opt('res', '1920x1080');
const END = +opt('end', 1200);
const WINDOW = +opt('window', 600);
const RUNS = +opt('runs', 5);
// --pairs N: also N rounds of one player and n players run side by side (the
// same load on the machine for both, so their ratio holds still where the
// machine is busy), the median ratio
const PAIRS = +opt('pairs', 0);
const SHOTS = opt('shots', ''); // a screenshot of each count's last frame (the first clean run): derby-<n>p-<gpu>.png
const OUT = join(ROOT, 'out');
mkdirSync(OUT, { recursive: true });
const JSON_OUT = resolve(opt('json', join(OUT, 'arenafps-split-bench.json')));

const built = (() => {
  try { return JSON.parse(readFileSync(join(ROOT, 'build/native/apps/arenafps_auto.json'), 'utf8')).binary; } catch { /* below */ }
  return join(ROOT, 'build/native/project/games/arenafps/arenafps_auto');
})();
// (an A/B binary takes its derby.cwpak along: <dir>/derby beside <dir>/derby.cwpak)
const EXES = opt('exes', built).split(',').map((e) => resolve(e));
for (const e of EXES) if (!existsSync(e)) { console.error(`no native arenafps_auto build (${e}): scripts/build.sh arenafps_auto`); process.exit(2); }

// the pad script (CW_INPUT lines: frame, pad 0's buttons and axes, then p= / p2= / p3= for pads 1-3).
// Pads 1..n-1 join on the title, pad 0 starts QUICK MATCH, then everyone
// drives: throttle held, each player weaving on their own beat.
async function run(exe, n, gpu, timed, shot) {
  const dir = tempDir(OUT, 'arenafps-split-bench-');
  const env = { ...process.env, CW_FRAMES: String(END), SDL_AUDIODRIVER: 'dummy', ARENA_PLAYERS: String(n), ARENA_ID: process.env.ARENA_ID || '0', ARENA_MODE: process.env.ARENA_MODE || '0', ARENA_BOTS: process.env.ARENA_BOTS || '4', ARENA_RES: RES,
    CW_DEBUG_CMDS: `perf ${WINDOW};pipeline`, CW_WGPU_VALIDATION: '0' };
  if (timed) env.CW_GPU_TIMERS = '1';
  if (shot) { mkdirSync(resolve(SHOTS), { recursive: true }); env.CW_SCREENSHOT = join(resolve(SHOTS), `arenafps-${n}p-${gpu}${EXES.length > 1 ? '-' + EXES.indexOf(exe) : ''}.png`); }
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  if (existsSync(exe + '.assets')) env.CW_ASSETS = exe + '.assets';
  const r = await new Promise((done) => {
    const c = spawn('timeout', ['-s', 'KILL', '600', exe], { env });
    let stdout = '', stderr = '';
    c.stdout.on('data', (d) => (stdout += d));
    c.stderr.on('data', (d) => (stderr += d));
    c.on('close', (status) => done({ status, stdout, stderr }));
  });
  if (r.status !== 0) throw new Error(`arenafps ${n}p ${gpu} exited ${r.status}: ${(r.stderr || '').slice(-2000)}`);
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(r.stderr) || [])[1]);
  else if (gl) gl.check('native', r.stderr);
  const json = r.stdout.split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));
  const perf = json.find((j) => j.frames !== undefined);
  const pipe = json.find((j) => j.passes);
  return { perf, pipe, stderr: r.stderr };
}

const r2 = (x) => (x == null ? null : Math.round(x * 100) / 100);
const median = (xs) => [...xs].sort((a, b) => a - b)[xs.length >> 1];
const results = [];
for (const gpu of GPUS) {
  const key = (e, n) => `${e}|${n}`, cleans = new Map(), timeds = new Map();
  for (let k = 0; k < RUNS; k++)
    for (const n of PLAYERS)
      for (const e of EXES) {
        if (!cleans.has(key(e, n))) cleans.set(key(e, n), []), timeds.set(key(e, n), []);
        cleans.get(key(e, n)).push(await run(e, n, gpu, false, SHOTS && k === 0));
        timeds.get(key(e, n)).push(await run(e, n, gpu, true));
      }
  for (const e of EXES) for (const n of PLAYERS) {
    const runs = cleans.get(key(e, n)), clean = runs[0], tr = timeds.get(key(e, n));
    // the GPU numbers: the timed run with the least GPU time (the desktop and other jobs share the GPU)
    const timed = tr.reduce((a, b) => (b.pipe.gpu_ms < a.pipe.gpu_ms ? b : a));
    const build = EXES.length > 1 ? String.fromCharCode(65 + EXES.indexOf(e)) : '';
    const p = clean.perf, a = (k) => median(runs.map((x) => x.perf[k].avg));
    const c = (k) => (p.thread_cpu ? Math.min(...runs.map((x) => x.perf.thread_cpu[k])) : null); // main-thread CPU time
    const passes = Object.fromEntries((timed.pipe.passes || []).filter((x) => x.ran && !x.fused && x.gpu_ms > 0).map((x) => [x.name, r2(x.gpu_ms)]));
    const cpuPasses = Object.fromEntries((clean.pipe.passes || []).filter((x) => x.ran && !x.fused && x.cpu_ms > 0).map((x) => [x.name, r2(x.cpu_ms)]));
    // against one player: the ratio within each round (the runs next to each other), the median of the rounds
    const ones = cleans.get(key(e, 1));
    const vs1 = (k) => (ones && p.thread_cpu ? median(runs.map((x, i) => x.perf.thread_cpu[k] / ones[i].perf.thread_cpu[k])) : null);
    // side by side: one player and n players at the same time, the ratio of each pair, the median
    let pairs = null;
    if (PAIRS && n > 1) {
      const rs = [];
      for (let k = 0; k < PAIRS; k++) {
        const [one, many] = await Promise.all([run(e, 1, gpu, false), run(e, n, gpu, false)]);
        rs.push(many.perf.thread_cpu.render / one.perf.thread_cpu.render);
      }
      pairs = median(rs);
    }
    results.push({ build, gpu, players: n, cpu_render_vs1: vs1('render'), cpu_render_vs1_pairs: pairs, res: RES, frame: r2(a('frame')), sim: r2(a('sim')), update: r2(a('update')), render: r2(a('render')), cpu_sim: r2(c('sim')), cpu_render: r2(c('render')), cpu_frame: r2(c('frame')),
      present: r2(a('present')), draws: p.draw_calls, tris: p.triangles, gpu_ms: r2(timed.pipe.gpu_ms), gpu_passes: passes, cpu_passes: cpuPasses });
    console.error(`${build}${build && ' '}${gpu} ${n}p: frame ${r2(a('frame'))} ms, sim ${r2(a('sim'))}, render ${r2(a('render'))} (cpu ${r2(c('render'))}), gpu ${r2(timed.pipe.gpu_ms)} ms, ${p.draw_calls} draws`);
  }
}

// the table: each count against one player on the same backend
const pct = (x, b) => (b ? `${x >= b ? '+' : ''}${Math.round((x / b - 1) * 100)}%` : '-');
const pctr = (r) => (r == null ? '-' : `${r >= 1 ? '+' : ''}${Math.round((r - 1) * 100)}%`);
const lines = [`arenafps splitscreen, ${RES}, native, last ${WINDOW} of ${END} frames`, '',
  `| backend | players | frame ms | sim CPU ms | render CPU ms | GPU ms | draws | render CPU vs 1P |${PAIRS ? ' side by side |' : ''} GPU vs 1P |`,
  `|---|---|---|---|---|---|---|---|${PAIRS ? '---|' : ''}---|`];
for (const r of results) {
  const one = results.find((x) => x.build === r.build && x.gpu === r.gpu && x.players === 1) || r;
  lines.push(`| ${r.build}${r.build && ' '}${r.gpu} | ${r.players} | ${r.frame} | ${r.cpu_sim} | ${r.cpu_render} | ${r.gpu_ms} | ${r.draws} | ${pctr(r.cpu_render_vs1)} |${PAIRS ? ` ${r.players === 1 ? '+0%' : pctr(r.cpu_render_vs1_pairs)} |` : ''} ${pct(r.gpu_ms, one.gpu_ms)} |`);
}
lines.push('', 'GPU ms by pass:');
for (const r of results) lines.push(`  ${r.build}${r.build && ' '}${r.gpu} ${r.players}p: ${Object.entries(r.gpu_passes).map(([k, v]) => `${k} ${v}`).join(', ')}`);
console.log(lines.join('\n'));
writeFileSync(JSON_OUT, JSON.stringify(results, null, 2) + '\n');
console.error(`wrote ${JSON_OUT}`);
