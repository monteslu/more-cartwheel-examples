// arenafps Battle: a newcomer wins from what the screen shows (no GPU; software GL). The autopilot's scripted newcomer
// (ARENA_HUMAN=newcomer: a pad player who reacts late, sprays wide and misses most shots, its stick through the game's
// own look curve) plays seat 1 of Battles on Farmlands, 8 soldiers a side and 50 reinforcements a side (short battles;
// the rules are the full mode's). It walks where the HUD's objective marker points, presses the call the marker offers
// (its squad and the next go there), stands in the ring when it gets there and fights whoever comes into view; it reads
// the marker and the prompt from the debug field they are drawn from (ptest's objective block). Each battle starts
// from its own seed (ARENA_BATTLE_SEED); one not over in the run counts as a loss. Three legs of 20 battles:
//   easy    the bot level Easy, the game as it ships: the newcomer's side must win at least 14 of 20
//   cued    normal, both sides' bots even (ARENA_T_battle_ally=0): the newcomer as above
//   nocue   the same with ARENA_NO_CUES=1 (as sw cues 0: no marker or call prompt, no side colours on the posts, no
//           pools on the HUD, no damage bar): the control, which must fail: at most 5 wins, and cued at least twice it
//   node tests/arenafps-battle-newcomer.mjs [--build build/native] [--seeds 1,...,20] [--jobs 20] [--leg easy|cued|nocue]
//   (--leg runs one leg and keeps its result in out/battle-newcomer/<leg>.json; with none it runs the three and judges.
//    --judge only judges the legs' results there: the legs can run on different machines)
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const BUILD = resolve(ROOT, arg('--build', 'build/native'));
const SEEDS = arg('--seeds', Array.from({ length: 20 }, (_, k) => k + 1).join(',')).split(',');
const JOBS = +arg('--jobs', '20');
const appJson = join(BUILD, 'apps/arenafps_auto.json');
const JUDGE = process.argv.includes('--judge');
if (!JUDGE && !existsSync(appJson)) { console.error(`no ${appJson}: build arenafps_auto first (scripts/build.sh arenafps_auto), or pass --build <dir>`); process.exit(2); }
const exe = JUDGE ? '' : JSON.parse(readFileSync(appJson, 'utf8')).binary;
const out = join(ROOT, 'out/battle-newcomer');
mkdirSync(out, { recursive: true });
async function run(tag, seed, extra) {
  const dump = join(out, `${tag}-${seed}.json`);
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: process.env.LP_NUM_THREADS || '1', CW_WORKERS: '1',
    CW_FRAMES: '1200', CW_WARMUP_SPREAD: '0', CW_QUALITY: 'verylow', ARENA_RES: '160x90', ARENA_ID: '3', ARENA_MODE: '3', ARENA_PLAYERS: '1',
    ARENA_BATTLE_LIVE: '8', ARENA_BATTLE_POOL: '50', ARENA_BATTLE_SEED: seed, ARENA_HUMAN: 'newcomer', ARENA_HUMAN_SEED: seed, ARENA_ONE: '1', ARENA_SIM_BATCH: '24',
    CW_DEBUG_DUMP: dump, ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const status = await new Promise((done) => { const c = spawn('nice', ['-n', '10', exe], { env, stdio: 'ignore' }); const k = setTimeout(() => c.kill('SIGKILL'), 5400e3); c.on('close', (s) => { clearTimeout(k); done(s); }); });
  if (status !== 0) { console.error(`${tag} ${seed}: exit ${status}`); process.exit(2); }
  const j = JSON.parse(readFileSync(dump, 'utf8'));
  return { seed, mode: j['game.mode'], over: j['game.state'] === 2, winner: j['match.winner'], end: j['battle.end_tick'], pool: j['battle.pool'], alive: j['battle.alive'],
    frags: j['fighters.score'][0], deaths: j['fighters.deaths'][0], arrivals: j['battle.p1'][1] };
}
const LEGS = {
  easy: { ARENA_LEVEL: '0' },
  cued: { ARENA_LEVEL: '1', ARENA_T_battle_ally: '0' },
  nocue: { ARENA_LEVEL: '1', ARENA_T_battle_ally: '0', ARENA_NO_CUES: '1' },
};
const LEG = arg('--leg', '');
if (LEG && !LEGS[LEG]) { console.error('--leg easy|cued|nocue'); process.exit(2); }
const wins = (a) => a.filter((r) => r.mode === 3 && r.over && r.winner === 0).length;
if (!JUDGE) {
  const legs = LEG ? [LEG] : Object.keys(LEGS);
  const tasks = legs.flatMap((l) => SEEDS.map((s) => [l, s]));
  const res = Object.fromEntries(legs.map((l) => [l, []]));
  let next = 0;
  await Promise.all(Array.from({ length: JOBS }, async () => { while (next < tasks.length) { const [l, s] = tasks[next++]; const r = await run(l, s, LEGS[l]); res[l].push(r); console.log(`${l}: ${JSON.stringify(r)}`); } }));
  for (const l of legs) writeFileSync(join(out, `${l}.json`), JSON.stringify({ leg: l, seeds: SEEDS.length, wins: wins(res[l]), runs: res[l] }) + '\n');
  if (LEG) { console.log(`${LEG}: the newcomer's side won ${wins(res[LEG])} of ${SEEDS.length}`); process.exit(0); }
}
const got = (l) => { const p = join(out, `${l}.json`); if (!existsSync(p)) { console.error(`no ${p}: run --leg ${l}`); process.exit(2); } return JSON.parse(readFileSync(p, 'utf8')); };
const E = got('easy'), C = got('cued'), N = got('nocue');
console.log(`easy: the newcomer's side won ${E.wins} of ${E.seeds}; normal, even bots: ${C.wins} of ${C.seeds} with the cues, ${N.wins} of ${N.seeds} without`);
let bad = 0;
if (E.wins < Math.ceil(0.7 * E.seeds)) { console.error(`FAIL: on Easy the newcomer won ${E.wins} of ${E.seeds} (want ${Math.ceil(0.7 * E.seeds)})`); bad++; }
if (C.wins < 2 * N.wins || C.wins === 0) { console.error(`FAIL: the cues do not make the difference: ${C.wins} wins with them, ${N.wins} without (want at least twice)`); bad++; }
if (N.wins > Math.floor(0.25 * N.seeds)) { console.error(`FAIL: the control did not fail: without the cues the newcomer still won ${N.wins} of ${N.seeds} (want at most ${Math.floor(0.25 * N.seeds)})`); bad++; }
else console.log(`control (no cues) fails as it must: ${N.wins} of ${N.seeds}`);
if (bad) process.exit(1);
console.log('ok   arenafps-battle-newcomer');
