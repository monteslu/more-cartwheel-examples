// Pacing measurement for arenafps: runs the autopilot (every seat a bot) for a stretch of game time per arena and mode,
// on software GL with no window (no GPU, no desktop; two llvmpipe threads, two engine workers, nice 10: a shared machine), and prints encounter and spawn numbers per run.
//
//   node games/arenafps/tools/pacing.mjs [--secs 180] [--build build/native] [--runs a:m:p[:bots],...] [--out file.json]
//
// a = arena (0 Foundry, 1 Courtyard, 2 Terraces), m = mode (0 deathmatch, 1 team deathmatch, 2 co-op), p = seats,
// bots = bot count (omit for the game's default for that many seats). Default runs: every arena and mode at 4 seats,
// plus deathmatch at 2 seats and 1 seat. Only the simulation is measured, so one frame is drawn per 150 game ticks
// (ARENA_SIM_BATCH=150); the simulation is the same tick for tick as a normal run.
//
// Columns (see the stats.* fields in src/main.c):
//   enc s     alive seconds per encounter (an enemy comes into sight after 3 s with none, or first after a spawn)
//   gap s     longest alive time between two encounter starts of one fighter
//   contact   share of alive time with an enemy in sight
//   sight s   the first pass's measure: alive seconds per sighting (one at most every 5 s)
//   unsafe    spawns where a living enemy could see the spawn point or stood within 15 m
//   spawnK    deaths within 5 s of spawning to an enemy that could see the spawn point or stood within 15 m of it
//   quick     deaths within 6 s of spawning (the first pass's counter)
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const secs = +arg('--secs', 180);
const build = resolve(ROOT, arg('--build', 'build/native'));
const exe = join(build, 'project/games/arenafps/arenafps_auto');
const outDir = join(ROOT, 'out/pacing');
mkdirSync(outDir, { recursive: true });
if (!existsSync(exe)) { console.error(`no ${exe}: build arenafps_auto first`); process.exit(2); }
const runs = (arg('--runs', '') || ['0:0:4', '1:0:4', '2:0:4', '0:1:4', '1:1:4', '2:1:4', '0:2:4', '1:2:4', '2:2:4', '0:0:2', '1:0:2', '2:0:2', '0:0:1'].join(','))
  .split(',').filter(Boolean).map((s) => { const [a, m, p, b] = s.split(':').map(Number); return { a, m, p, b }; });

const ARENA = ['Foundry', 'Courtyard', 'Terraces'], MODE = ['DM', 'TDM', 'co-op'];
const env0 = { ...process.env };
for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env0[k];
const rows = [];
for (const r of runs) {
  const tag = `a${r.a}m${r.m}p${r.p}${Number.isFinite(r.b) ? 'b' + r.b : ''}`;
  const dump = join(outDir, `${tag}.json`);
  const env = {
    ...env0, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: '2', CW_WORKERS: '2',
    ARENA_SIM_BATCH: '150', CW_FRAMES: String(Math.ceil(secs * 60 / 150) + 2), CW_WARMUP_SPREAD: '0', CW_QUALITY: 'low', ARENA_RES: '320x180',
    ARENA_ID: String(r.a), ARENA_MODE: String(r.m), ARENA_PLAYERS: String(r.p), ARENA_LEVEL: '1', ARENA_FRAGS: '999', ARENA_SECS: '900', ARENA_REPEAT: '1',
    CW_DEBUG_DUMP: dump,
  };
  if (Number.isFinite(r.b)) env.ARENA_BOTS = String(r.b); else delete env.ARENA_BOTS;
  const t0 = Date.now();
  const p = spawnSync('nice', ['-n', '10', exe], { env, encoding: 'utf8', timeout: 1800e3 });
  if (p.status !== 0 || !existsSync(dump)) { console.error(`${tag}: exit ${p.status}\n${(p.stderr || '').slice(-2000)}`); process.exit(1); }
  const j = JSON.parse(readFileSync(dump, 'utf8'));
  const g = (k) => j[k];
  const ft = g('stats.fighter_ticks'), kills = g('stats.kills');
  const fighters = g('match.nf');
  const row = {
    run: `${ARENA[r.a]} ${MODE[r.m]} ${r.p}p`, fighters, ticks: g('game.steps') ?? g('game.ticks'),
    enc_s: +(ft / 60 / Math.max(1, g('stats.eng'))).toFixed(1), gap_s: +(g('stats.eng_gap_max') / 60).toFixed(1),
    contact: +(100 * g('stats.contact_ticks') / Math.max(1, ft)).toFixed(0),
    sight_s: +(ft / 60 / Math.max(1, g('stats.enc'))).toFixed(1),
    kills, spawns: g('stats.spawns'), unsafe: +(100 * g('stats.unsafe_spawns') / Math.max(1, g('stats.spawns'))).toFixed(0),
    spawnK: +(100 * g('stats.spawn_kills') / Math.max(1, kills)).toFixed(1), quick: +(100 * g('stats.quick_deaths') / Math.max(1, kills)).toFixed(0),
    no_kill_s: +(g('stats.gap_max') / 60).toFixed(1), ttk_s: g('stats.ttk_n') ? +(g('stats.ttk_sum') / g('stats.ttk_n') / 60).toFixed(2) : null,
    skipped: g('render.skipped'), wall_s: Math.round((Date.now() - t0) / 1000),
  };
  if (row.ticks < secs * 60 * 0.98) { console.error(`${tag}: only ${row.ticks} ticks of ${secs * 60}: the run did not cover the game time`); process.exit(1); }
  rows.push(row);
  console.log(JSON.stringify(row));
}
const cols = ['run', 'fighters', 'enc_s', 'gap_s', 'contact', 'sight_s', 'kills', 'unsafe', 'spawnK', 'quick', 'no_kill_s', 'ttk_s'];
console.log('\n| ' + cols.join(' | ') + ' |\n|' + cols.map(() => '---').join('|') + '|');
for (const r of rows) console.log('| ' + cols.map((c) => r[c] + (['contact', 'unsafe', 'spawnK', 'quick'].includes(c) ? '%' : '')).join(' | ') + ' |');
const out = arg('--out', '');
if (out) writeFileSync(resolve(out), JSON.stringify(rows, null, 1));
