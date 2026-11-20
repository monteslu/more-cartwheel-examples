// arenafps: the setup screen's aim assist and rumble choices are kept in the game's save. Run 1 (a fresh save file) goes
// from the title to the setup screen on the pad, turns aim assist off and rumble off, and quits; run 2 starts again on
// the save run 1 wrote and must read both choices back (game.aim_assist 0, game.rumble_off 1). Control: run 2 on a
// fresh save file reads the
// defaults (assist on, rumble on), so the same check must fail there. Native, software GL, no GPU lock; CW_PLAYER_TIMING
// makes the runs keep their save (a fixed-frame test run reads and writes none).
//   node tests/arenafps-save.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync, existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const DIR = process.env.ARENA_BUILD_DIR || join(ROOT, 'build/native/games/arenafps');
const BIN = join(DIR, 'arenafps');
const OUT = join(ROOT, 'out', 'arenafps-save');
rmSync(OUT, { recursive: true, force: true });
mkdirSync(OUT, { recursive: true });

// title (Start), join (Start), then the setup screen opens on its last row (rules): up to rumble, toggle it, up to aim
// assist, toggle it, and stay on the setup screen
// the native input script (src/platform/input_script.c): "<frame> <buttons> <lx> <ly> <rx> <ry> <lt> <rt>", buttons as
// the cw_pad_button bit mask (start 1 << 6, up 1 << 11, right 1 << 14)
const INPUT = join(OUT, 'input.txt');
const START = 1 << 6, UP = 1 << 11, RIGHT = 1 << 14;
writeFileSync(INPUT, [[0, 0], [20, START], [22, 0], [40, START], [42, 0], [80, UP], [83, 0], [90, RIGHT], [93, 0], [100, UP], [103, 0],
  [110, RIGHT], [113, 0]].map(([fr, b]) => `${fr} ${b} 0 0 0 0 0 0`).join('\n') + '\n');

function run(save, frames, input) {
  const state = join(OUT, 'state.json');
  rmSync(state, { force: true });
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', LIBGL_ALWAYS_SOFTWARE: '1', CW_GPU: 'gl',
    CW_TEST_GPU: 'software', CW_PLAYER_TIMING: '1', CW_FRAMES: String(frames), CW_DEBUG_DUMP: state, CW_ASSETS: BIN + '.assets', CW_SAVE_FILE: save,
    ...(input ? { CW_INPUT: input } : {}) };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const r = spawnSync(BIN, [], { cwd: DIR, env, encoding: 'utf8', timeout: 300000 });
  let s;
  try { s = JSON.parse(readFileSync(state, 'utf8')); } catch { return { err: `no state (exit ${r.status}): ${(r.stderr || '').split('\n').slice(-5).join(' | ')}` }; }
  const f = (k) => s[k] ?? s.fields?.[k];
  if (process.env.ARENA_SAVE_DEBUG) console.log(JSON.stringify({ state: f('game.state'), shell: f('shell.screen') }));
  return { assist: f('game.aim_assist'), rumbleOff: f('game.rumble_off'), state: f('game.state') };
}

let bad = 0;
const check = (ok, what) => { if (!ok) bad++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); };
const save = join(OUT, 'save.dat');
const one = run(save, 140, INPUT);
if (one.err) check(false, `run 1: ${one.err}`);
else check(one.assist === 0 && one.rumbleOff === 1, `run 1 turned aim assist off and rumble off on the setup screen (assist ${one.assist}, rumble_off ${one.rumbleOff})`);
check(existsSync(save), 'run 1 wrote its save');
const two = run(save, 10);
if (two.err) check(false, `run 2: ${two.err}`);
else check(two.assist === 0 && two.rumbleOff === 1, `run 2 read them back from the save (assist ${two.assist}, rumble_off ${two.rumbleOff})`);
const ctl = run(join(OUT, 'fresh.dat'), 10);
if (ctl.err) check(false, `control: ${ctl.err}`);
else check(!(ctl.assist === 0 && ctl.rumbleOff === 1), `control: a fresh save reads the defaults, so the check fails there (assist ${ctl.assist}, rumble_off ${ctl.rumbleOff})`);
rmSync(OUT, { recursive: true, force: true });
console.log(bad ? `arenafps-save: ${bad} failed` : 'arenafps-save: pass');
process.exit(bad ? 1 : 0);
