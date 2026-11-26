// arenafps Battle: the honest count and the last stand (no GPU; software GL). The autopilot plays a Battle on Foundry,
// one seat and 8 soldiers a side; on match tick 30 a test command sets the red side's reinforcements to 3 while its
// 8 are on the field. Those 3 points go on respawns (or the bleed), and at 0 the red soldiers still standing fight on:
// the run must show red at 0 reinforcements with soldiers alive for at least 300 ticks with the battle still on, and
// the battle ending on the tick red's last soldier went down (battle.zero_tick = battle.end_tick), blue the winner.
// Control: ARENA_COUNT_RULE=reference (the count taken at each death, spawns free while any is left, a side loses at
// 0 with its soldiers still standing: the rule that reads as a reverse score) must end the battle on the tick red's
// count hits 0, with red soldiers alive, so the checks fail.
//   node tests/arenafps-laststand.mjs [--build build/native]
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const i = process.argv.indexOf('--build');
const BUILD = resolve(ROOT, i > 0 ? process.argv[i + 1] : 'build/native');
const appJson = join(BUILD, 'apps/arenafps_auto.json');
if (!existsSync(appJson)) { console.error(`no ${appJson}: build arenafps_auto first (scripts/build.sh arenafps_auto), or pass --build <dir>`); process.exit(2); }
const exe = JSON.parse(readFileSync(appJson, 'utf8')).binary;
const out = join(ROOT, 'out/laststand');
mkdirSync(out, { recursive: true });
function run(tag, extra) {
  const dump = join(out, `${tag}.json`);
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: process.env.LP_NUM_THREADS || '2', CW_WORKERS: '2',
    CW_FRAMES: '1500', CW_WARMUP_SPREAD: '0', CW_QUALITY: 'verylow', ARENA_RES: '320x180', ARENA_ID: '0', ARENA_MODE: '3', ARENA_PLAYERS: '1', ARENA_BATTLE_LIVE: '8',
    ARENA_BATTLE_AT: '30:pool 1 3', ARENA_ONE: '1', ARENA_SIM_BATCH: '8', CW_DEBUG_DUMP: dump, ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const p = spawnSync('nice', ['-n', '10', exe], { env, encoding: 'utf8', timeout: 1200e3 });
  if (p.status !== 0) { console.error(`${tag}: exit ${p.status}\n${(p.stderr || '').slice(-800)}`); process.exit(2); }
  const j = JSON.parse(readFileSync(dump, 'utf8'));
  return { state: j['game.state'], mode: j['game.mode'], winner: j['match.winner'], pool: j['battle.pool'], alive: j['battle.alive'], ls: j['battle.ls_ticks'], pool0: j['battle.pool0_tick'],
    zero: j['battle.zero_tick'], end: j['battle.end_tick'], spent: j['battle.spent'], bleeds: j['battle.bleeds'] };
}
const judge = (r) => {
  const f = [];
  if (r.mode !== 3) f.push(`not a battle (mode ${r.mode})`);
  if (r.state !== 2 || r.end < 0) f.push('the battle did not end in the run');
  if (r.pool0[1] < 0) f.push('red never reached 0 reinforcements');
  if (r.ls[1] < 300) f.push(`red's last stand lasted ${r.ls[1]} ticks (under 300)`);
  if (r.end - r.pool0[1] < 300) f.push(`the battle ended ${r.end - r.pool0[1]} ticks after red's count hit 0 (under 300)`);
  if (r.zero[1] !== r.end) f.push(`the end (tick ${r.end}) is not the tick red's last soldier went down (${r.zero[1]})`);
  if (r.winner !== 0) f.push(`winner ${r.winner}, not blue`);
  return f;
};
const r = run('laststand', {});
console.log(`honest count: ${JSON.stringify(r)}`);
const c = run('control', { ARENA_COUNT_RULE: 'reference' });
console.log(`control (the count taken at death, lose at 0): ${JSON.stringify(c)}`);
const fr = judge(r), fc = judge(c);
for (const m of fr) console.error(`FAIL: ${m}`);
if (!fc.length) console.error('FAIL: the control did not fail');
else console.log(`control fails as it must: ${fc.join('; ')}`);
if (fr.length || !fc.length) process.exit(1);
console.log('ok   arenafps-laststand');
