// Memory soak: the autopilot plays short matches, cycling the three arenas and modes (software GL, no GPU, no lock), and
// the game notes the C heap in use at the start of every match (debug field soak.heap_kb, glibc hosts). The same arena
// and mode come round every third match, so match k and match k + 3c should start with the same heap: growth between
// the second cycle's first match and the last same-phase match is what a match leaks.
//
//   node tools/soak.mjs [--frames N] [--secs S] [--batch B] [--leak] [--bin path] [--out prefix] [--max-growth pct]
//
// --leak runs the control (ARENA_LEAK=1: the level meshes are kept, as before the fix): it must fail.
// Exit 0 pass, 1 fail (growth over --max-growth, default 2 percent, a crash, or too few matches).
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..', '..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i >= 0 ? process.argv[i + 1] : d; };
const FRAMES = +arg('--frames', 700), SECS = +arg('--secs', 8), BATCH = +arg('--batch', 120);
const LEAK = process.argv.includes('--leak');
const BIN = arg('--bin', join(ROOT, 'build/native/project/games/arenafps/arenafps_auto'));
const OUT = arg('--out', join(ROOT, 'out/soak/arenafps' + (LEAK ? '-leak' : '')));
const MAXG = +arg('--max-growth', 2);
mkdirSync(dirname(OUT), { recursive: true });

const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', LP_NUM_THREADS: '2', CW_WORKERS: '2',
  CW_GPU: 'gl', ARENA_PLAYERS: '4', ARENA_FRAGS: '12', ARENA_SECS: String(SECS), ARENA_LEVEL: '1', ARENA_SIM_BATCH: String(BATCH), ARENA_RES: '480x270',
  CW_QUALITY: 'low', CW_FRAMES: String(FRAMES), CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_DEBUG_DUMP: OUT + '.json', ...(LEAK ? { ARENA_LEAK: '1' } : {}) };
for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
const rss = [];
const child = spawn('nice', ['-n', '10', BIN], { env, stdio: ['ignore', 'pipe', 'pipe'] });
let log = '';
child.stdout.on('data', (d) => { log += d; });
child.stderr.on('data', (d) => { log += d; });
const timer = setInterval(() => {
  try { const m = /VmRSS:\s+(\d+)/.exec(readFileSync(`/proc/${child.pid}/status`, 'utf8')); if (m) rss.push(+m[1]); } catch {}
}, 5000);
const code = await new Promise((r) => child.on('exit', (c) => r(c)));
clearInterval(timer);
writeFileSync(OUT + '.log', log);
writeFileSync(OUT + '.rss.json', JSON.stringify(rss)); /* VmRSS kB every 5 s */
let j = {};
try { j = JSON.parse(readFileSync(OUT + '.json', 'utf8')); } catch {}
const n = Math.min(j['soak.matches'] || 0, 48), heap = (j['soak.heap_kb'] || []).slice(0, n);
// the second cycle's first match (index 3) against the last match of the same arena and mode
let last = 3;
while (last + 3 < n) last += 3;
const g = n > 6 ? (100 * (heap[last] - heap[3])) / heap[3] : NaN;
const res = { code, matches: n, heap_mb: heap.map((k) => Math.round(k / 1024)), from: 3, to: last, growth_pct: +g.toFixed(2),
  per_match_mb: n > 6 ? +((heap[last] - heap[3]) / 1024 / ((last - 3))).toFixed(2) : null,
  rss_mb_first: rss.length ? Math.round(rss[Math.min(rss.length - 1, 2)] / 1024) : null, rss_mb_last: rss.length ? Math.round(rss[rss.length - 1] / 1024) : null,
  skipped: j['render.skipped'], gpu_errors: j['cw.gpu.errors'], leak_control: LEAK };
const pass = code === 0 && n > 6 && g < MAXG;
console.log(JSON.stringify(res));
console.log(pass ? 'PASS' : 'FAIL');
process.exitCode = pass ? 0 : 1;
