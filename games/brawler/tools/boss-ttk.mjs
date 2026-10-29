// How long each boss takes to beat: brawler_auto goes straight to a stage's boss wave (debug command `wave 4`, or
// `wave 2` for the arcade's mini boss) with N bots and prints the fight's length from the `brawler-stats` line the stage
// clear writes: boss_frames (frames shown, hit-stop and the slow-motion fall included: what a player waits) and
// boss_hits (blows that landed on the boss). Off the GPU lock: software GL, 2 threads, nice 10, many ticks per frame.
//
//   node games/brawler/tools/boss-ttk.mjs --exe <brawler_auto> --pak <cwpak> [--stages 0,1,2,3] [--players 1,4]
//        [--seeds 4] [--botdmg 1] [--turbo 30] [--out file.json]
//
// --botdmg scales the bots' blows (the game's default for bots is 3: they are poor fighters). 1 is a fighter's real damage.
// Each seed (BRAWLER_SEED) is another match from the same rules, so the table shows a spread, not one run.
import { spawn } from 'node:child_process';
import { writeFileSync } from 'node:fs';

const argv = process.argv.slice(2);
const arg = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const exe = arg('--exe'), pak = arg('--pak');
if (!exe || !pak) { console.error('usage: boss-ttk.mjs --exe <brawler_auto> --pak <cwpak> [--stages 0,1,2,3] [--players 1,4] [--seeds N] [--botdmg K]'); process.exit(2); }
const stages = arg('--stages', '0,1,2,3').split(',').map(Number), players = arg('--players', '1,4').split(',').map(Number);
const seeds = +arg('--seeds', 4), botdmg = arg('--botdmg', '1'), turbo = +arg('--turbo', 30), mini = argv.includes('--mini');
const BOSS = ['Foreman', 'Arthur', 'Matron', 'Colossus'];
const kv = (line) => Object.fromEntries([...line.matchAll(/(\w+)=(-?\d+)/g)].map((m) => [m[1], +m[2]]));

function run(stage, n, seed) {
  const env = {
    ...process.env, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_WORKERS: '2', SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy',
    CW_QUALITY: 'low', CW_FRAMES: String(Math.ceil((12 * 3600) / turbo)), CW_PAK: pak, BRAWLER_TURBO: String(turbo), BRAWLER_LOG: '1',
    BRAWLER_PLAYERS: String(n), BRAWLER_STAGE: String(stage), BRAWLER_BOTDMG: botdmg, BRAWLER_SEED: String(seed),
    CW_START_CMDS: mini ? 'wave 2' : 'wave 4',
  };
  return new Promise((resolve) => {
    const p = spawn('nice', ['-n', '10', exe], { env, stdio: ['ignore', 'pipe', 'pipe'] });
    let buf = '', out = null;
    const on = (d) => {
      buf += d;
      let i;
      while ((i = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, i); buf = buf.slice(i + 1);
        if (!out && line.includes('brawler-stats')) { out = { why: /brawler-stats (\w+)/.exec(line)[1], ...kv(line) }; p.kill('SIGTERM'); }
        else if (/\[error\]/.test(line)) console.log(line);
      }
    };
    p.stdout.on('data', on); p.stderr.on('data', on);
    p.on('close', () => resolve(out ?? { why: 'timeout' }));
  });
}

const rows = [];
for (const s of stages) for (const n of players) {
  const runs = [];
  for (let k = 0; k < seeds; k++) {
    const r = await run(s, n, k);
    runs.push(r);
    console.log(`${BOSS[s]} players=${n} seed=${k}: ${r.why} fight ${r.boss_frames > 0 ? (r.boss_frames / 60).toFixed(1) + ' s' : '-'} (ticks ${r.boss_ticks}) blows ${r.boss_hits} deaths ${r.deaths} dmg_taken ${r.dmg_taken}`);
  }
  const won = runs.filter((r) => r.why === 'clear' && r.boss_frames > 0).map((r) => r.boss_frames / 60).sort((a, b) => a - b);
  const med = won.length ? won[Math.floor(won.length / 2)] : null;
  rows.push({ boss: BOSS[s], stage: s, players: n, botdmg: +botdmg, won: won.length, of: runs.length, median_s: med, min_s: won[0] ?? null, max_s: won.at(-1) ?? null, runs });
}
console.log('\nboss        players  won   median s   range s');
for (const r of rows) console.log(`${r.boss.padEnd(11)} ${String(r.players).padStart(7)}  ${r.won}/${r.of}  ${r.median_s == null ? '-' : r.median_s.toFixed(1).padStart(9)}   ${r.min_s == null ? '' : r.min_s.toFixed(1) + '..' + r.max_s.toFixed(1)}`);
if (arg('--out')) writeFileSync(arg('--out'), JSON.stringify(rows, null, 2));
