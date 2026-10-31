// Full deathmatches between the scripted human (src/main.c, "the scripted human": a pad player modelled from what is on
// screen, with reaction time, noisy stick aim through the game's own look curve, bursts, strafing) and the bots, on the
// autopilot (software GL, simulation only), to count how often a casual or a skilled player wins at each bot level.
// tools/playtest-human.mjs plays the same model through romdev's pad on the cart; this tool is for numbers of matches.
//
//   node games/arenafps/tools/human-matches.mjs [--human casual|skilled] [--level 0|1|2|3] [--assist 0|1]
//        [--arenas 0,1,2] [--seeds 1,2,3] [--players 1] [--build build/native] [--out file.json]
//
// Each run is one match under the real rules (first to 25 frags or 8 minutes, the game's AUTO bot count). A win: player 1
// alone on the top score; near: within 2 frags of the winner.
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const human = arg('--human', 'casual'), level = +arg('--level', 1), assist = arg('--assist', '1');
const arenas = arg('--arenas', '0,1,2').split(',').map(Number), seeds = arg('--seeds', '1,2,3').split(',').map(Number);
const players = +arg('--players', 1), fov = arg('--fov', ''), tagx = arg('--tag', '');
const build = resolve(ROOT, arg('--build', 'build/native'));
const exe = arg('--exe', '') ? resolve(arg('--exe', '')) : ([join(build, 'project/games/arenafps/arenafps_auto'), join(build, 'games/arenafps/arenafps_auto')].find(existsSync) ?? join(build, 'games/arenafps/arenafps_auto')); /* engine-source tree, or the SDK tree */
const outDir = join(ROOT, 'out/human');
mkdirSync(outDir, { recursive: true });
if (!existsSync(exe)) { console.error(`no ${exe}: build arenafps_auto first`); process.exit(2); }
const ARENA = ['Foundry', 'Courtyard', 'Terraces'], LEVEL = ['easy', 'normal', 'veteran', 'hard'];
const env0 = { ...process.env };
for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env0[k];
const rows = [];
for (const a of arenas) for (const seed of seeds) {
  const tag = `${human}-l${level}-as${assist}${fov ? '-fov' + fov : ''}${tagx}-a${a}-s${seed}`;
  const dump = join(outDir, `${tag}.json`);
  const BATCH = 900; /* one drawn frame per 15 s of game time: only the simulation is measured (tick for tick the same) */
  const env = {
    ...env0, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: '2', CW_WORKERS: '2',
    ARENA_SIM_BATCH: String(BATCH), CW_FRAMES: String(Math.ceil(8.2 * 60 * 60 / BATCH) + 4), CW_WARMUP_SPREAD: '0', CW_QUALITY: 'low', ARENA_RES: '320x180',
    ARENA_ID: String(a), ARENA_MODE: '0', ARENA_PLAYERS: String(players), ARENA_LEVEL: String(level), ARENA_ONE: '1',
    ARENA_HUMAN: human, ARENA_HUMAN_SEED: String(seed), ARENA_ASSIST: assist, CW_DEBUG_DUMP: dump,
  };
  if (fov) env.ARENA_BOT_FOV = fov;
  for (let i = 2; i < process.argv.length; i++) if (process.argv[i] === '--env') { const [k, v] = process.argv[i + 1].split('='); env[k] = v; }
  const t0 = Date.now();
  const p = spawnSync('nice', ['-n', '10', exe], { env, encoding: 'utf8', timeout: 1800e3 });
  if (p.status !== 0 || !existsSync(dump)) { console.error(`${tag}: exit ${p.status}\n${(p.stderr || '').slice(-2000)}`); process.exit(1); }
  const j = JSON.parse(readFileSync(dump, 'utf8'));
  const nf = j['match.nf'], sc = j['fighters.score'].slice(0, nf), st = j['p1.stats'];
  if (j['game.state'] !== 2) { console.error(`${tag}: the match did not end (state ${j['game.state']})`); process.exit(1); }
  if (!(j['nav.links'] > 0) || !(j['nav.requests'] > 0)) { console.error(`${tag}: the arena did not load (no navigation links: a wrong --exe or working directory plays an empty box)`); process.exit(1); }
  const others = Math.max(...sc.slice(1)), top = Math.max(...sc);
  const row = {
    run: `${ARENA[a]} ${human} vs ${LEVEL[level]}${assist === '0' ? ' no assist' : ''} seed ${seed}`, p1: sc[0], best_bot: others, deaths: j['fighters.deaths'][0],
    win: sc[0] > others, near: sc[0] >= top - 2, mins: +(j['game.ticks'] / 3600).toFixed(1),
    kills: j['stats.kills'], spawnK: j['stats.spawn_kills'], acc: st[0] ? +(100 * st[1] / st[0]).toFixed(0) : 0, dealt: st[2], taken: st[3], scores: sc, wall_s: Math.round((Date.now() - t0) / 1000),
  };
  rows.push(row);
  console.log(JSON.stringify(row));
}
const wins = rows.filter((r) => r.win).length, near = rows.filter((r) => r.near).length;
const kd = rows.reduce((s, r) => s + r.p1, 0) / Math.max(1, rows.reduce((s, r) => s + r.deaths, 0));
console.log(`\n${human} vs ${LEVEL[level]}: ${wins}/${rows.length} won, ${near}/${rows.length} won or within 2, K/D ${kd.toFixed(2)}, mean accuracy ${(rows.reduce((s, r) => s + r.acc, 0) / rows.length).toFixed(0)}%, match ${(rows.reduce((s, r) => s + r.mins, 0) / rows.length).toFixed(1)} min, spawn kills ${(100 * rows.reduce((s, r) => s + r.spawnK, 0) / Math.max(1, rows.reduce((s, r) => s + r.kills, 0))).toFixed(0)}%`);
/* --expect-near f: fail unless that share of matches was won or lost by at most 2 frags; --expect-win-under f: fail unless under that share was won */
const en = arg('--expect-near', ''), ew = arg('--expect-win-under', '');
let bad = false;
if (en && near / rows.length < +en) { console.error(`FAIL: ${near}/${rows.length} won or near, wanted ${en}`); bad = true; }
if (ew && wins / rows.length >= +ew) { console.error(`FAIL: ${wins}/${rows.length} won, wanted under ${ew}`); bad = true; }
const out = arg('--out', '');
if (out) writeFileSync(resolve(out), JSON.stringify(rows, null, 1));
if (bad) process.exit(1);
