// arenafps Battle: a person downed at 0 reinforcements takes over a teammate bot (no GPU; software GL). The real game
// (not the autopilot), driven by pad 0 alone: Play, join, the setup panel's mode row to BATTLE, start. On match tick 20
// test commands leave blue at 0 reinforcements with 2 on the field (seat 0's soldier and one bot) against 10 red, on
// tick 40 seat 0's soldier is downed. The pad then holds the left stick forward. Checks after the takeover: seat 0
// plays another soldier (seat.fighter), and 1.5 s after the handover it is alive, blue has 1 alive, the battle is still on and the soldier has walked
// forward along the way it faced at the handover (the stick moved it), the view never turned faster than 3.6 rad/s
// through the takeover and the second after it, the view was at the soldier's eye after the handover, and no frame of
// the camera was offset (cam.offset).
// Control: ARENA_TAKEOVER=0 (no takeover: the person watches and the bot plays on by itself) must fail the
// "seat 0 drives" checks.
//   node tests/arenafps-takeover.mjs [--build build/native]
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const i = process.argv.indexOf('--build');
const BUILD = resolve(ROOT, i > 0 ? process.argv[i + 1] : 'build/native');
const appJson = join(BUILD, 'apps/arenafps.json');
if (!existsSync(appJson)) { console.error(`no ${appJson}: build arenafps first (scripts/build.sh arenafps), or pass --build <dir>`); process.exit(2); }
const exe = JSON.parse(readFileSync(appJson, 'utf8')).binary;
const out = join(ROOT, 'out/takeover');
mkdirSync(out, { recursive: true });
// pad 0: <frame> <buttons> lx ly rx ry lt rt (buttons: south 1, start 64, up 2048, left 8192)
const SOUTH = 1, START = 64, UP = 2048, LEFT = 8192;
const lines = [];
const tap = (f, b) => { lines.push(`${f} ${b} 0 0 0 0 0 0`, `${f + 6} 0 0 0 0 0 0 0`); };
tap(120, SOUTH); tap(180, SOUTH); tap(240, START);          /* the title: Play; join; on to the setup panel */
for (let k = 0; k < 5; k++) tap(320 + k * 20, UP);          /* from the rules row up to the mode row */
tap(430, LEFT);                                              /* deathmatch, one to the left: BATTLE (the last) */
tap(470, SOUTH);                                             /* start */
lines.push('560 0 0 -1 0 0 0 0');                            /* from here the left stick forward */
const input = join(out, 'pad.txt');
writeFileSync(input, lines.join('\n') + '\n');
const FRAMES = 960;
function run(tag, extra) {
  const dump = join(out, `${tag}.json`);
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: process.env.LP_NUM_THREADS || '2', CW_WORKERS: '2',
    CW_FRAMES: String(FRAMES), CW_WARMUP_SPREAD: '0', CW_QUALITY: 'verylow', CW_INPUT: input, CW_SCREENSHOT: join(out, `${tag}.png`),
    ARENA_BATTLE_AT: '20:pool 0 0|20:alive 0 2|20:pool 1 0|20:alive 1 10|40:downseat 0', CW_DEBUG_DUMP: dump, ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const p = spawnSync('nice', ['-n', '10', exe], { env, encoding: 'utf8', timeout: 1200e3 });
  if (p.status !== 0) { console.error(`${tag}: exit ${p.status}\n${(p.stderr || '').slice(-800)}`); process.exit(2); }
  const j = JSON.parse(readFileSync(dump, 'utf8'));
  const seat = j['seat.fighter'][0], st = j['tk.start'], af = j['tk.after']; /* the handover, and 1.5 s after it */
  const dx = af[0] - st[0], dz = af[1] - st[1];
  const fwd = -Math.sin(st[2]) * dx - Math.cos(st[2]) * dz; /* yaw 0 faces -Z */
  return { state: j['game.state'], mode: j['game.mode'], seat, seatAlive: af[3], alive: [af[2]], state15: af[3], pool: j['battle.pool'], tk: j['tk.stats'],
    fwd: Math.round(fwd * 100) / 100, moved: Math.round(Math.hypot(dx, dz) * 100) / 100, offset: j['cam.offset'] };
}
const judge = (r) => {
  const f = [];
  if (r.mode !== 3) f.push(`not a battle (mode ${r.mode})`);
  if (r.tk[0] < 1 || r.seat === 0) f.push(`seat 0 still plays fighter ${r.seat} (handovers ${r.tk[0]})`);
  if (!r.seatAlive) f.push('1.5 s after the handover seat 0\'s soldier is not alive in a battle still on');
  if (r.alive[0] !== 1) f.push(`blue has ${r.alive[0]} alive, not 1`);
  if (r.tk[0] >= 1 && !(r.fwd >= 2 && r.fwd >= 0.6 * r.moved)) f.push(`the stick did not move the soldier forward (${r.fwd} m ahead of ${r.moved} m)`);
  if (r.tk[0] < 1) f.push('no handover: the stick drove nobody');
  if (r.tk[1] > 3650) f.push(`the view turned at ${r.tk[1] / 1000} rad/s (over 3.6)`);
  if (r.tk[2] > 30) f.push(`the view was ${r.tk[2]} mm off the soldier's eye after the handover`);
  if (r.offset[1] !== 0) f.push(`${r.offset[1]} camera frames offset`);
  return f;
};
const r = run('takeover', {});
console.log(`takeover: ${JSON.stringify(r)}`);
const c = run('control', { ARENA_TAKEOVER: '0' });
console.log(`control (no takeover): ${JSON.stringify(c)}`);
const fr = judge(r), fc = judge(c);
for (const m of fr) console.error(`FAIL: ${m}`);
if (!fc.some((m) => m.startsWith('seat 0 still plays'))) console.error('FAIL: the control did not fail the "seat 0 drives" check');
else console.log(`control fails as it must: ${fc.join('; ')}`);
if (fr.length || !fc.some((m) => m.startsWith('seat 0 still plays'))) process.exit(1);
console.log('ok   arenafps-takeover');
