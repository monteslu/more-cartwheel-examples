// arenafps: a downed player sees who downed them (no GPU; software GL). Two seats play Foundry deathmatch with bots in the
// autopilot build; on every dead tick after the view has turned (24 ticks) whose death has a killer, the killer's chest must
// be inside that player's view on at least 90 percent of them (killcam = [those ticks, ticks with the killer in view]).
// Control: ARENA_NO_KILLCAM=1 (the old dead view, lying on the floor looking nowhere) must fall under the bound.
//   node tests/arenafps-killcam.mjs [--build build/native]
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const i = process.argv.indexOf('--build');
const exe = join(resolve(ROOT, i > 0 ? process.argv[i + 1] : 'build/native'), 'games/arenafps/arenafps_auto');
const out = join(ROOT, 'out/killcam');
mkdirSync(out, { recursive: true });
function run(tag, extra) {
  const dump = join(out, `${tag}.json`);
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: '2', CW_WORKERS: '2',
    CW_FRAMES: '900', CW_WARMUP_SPREAD: '0', CW_QUALITY: 'verylow', ARENA_RES: '320x180', ARENA_ID: '0', ARENA_PLAYERS: '2', ARENA_BOTS: '4', ARENA_LEVEL: '3', ARENA_SIM_BATCH: '12', CW_DEBUG_DUMP: dump, ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const p = spawnSync('nice', ['-n', '10', exe], { env, encoding: 'utf8', timeout: 900e3 });
  if (p.status !== 0) { console.error(`${tag}: exit ${p.status}\n${(p.stderr || '').slice(-800)}`); process.exit(2); }
  const j = JSON.parse(readFileSync(dump, 'utf8'));
  if (j['game.state'] === 0) { console.error(`${tag}: the match did not start`); process.exit(2); }
  const [dead, seen] = j['killcam'];
  return { dead, seen, share: dead ? seen / dead : 0, deaths: j['fighters.deaths'].slice(0, 2) };
}
const r = run('killcam', {});
console.log(`killer cam: ${r.seen} of ${r.dead} dead ticks with the killer in view (${(100 * r.share).toFixed(1)} %), deaths of the two seats ${r.deaths}`);
const c = run('control', { ARENA_NO_KILLCAM: '1' });
console.log(`control (the old dead view): ${c.seen} of ${c.dead} (${(100 * c.share).toFixed(1)} %)`);
let bad = 0;
if (r.dead < 300) { console.error('FAIL: too few dead ticks to judge (the seats were not downed often enough)'); bad++; }
if (r.share < 0.9) { console.error('FAIL: the killer is out of view on over 10 percent of dead ticks'); bad++; }
if (c.share >= 0.9) { console.error('FAIL: the control did not fail'); bad++; }
if (bad) process.exit(1);
console.log('ok   arenafps-killcam');
