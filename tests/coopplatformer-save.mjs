// coopplatformer: a save from version 4 (before the big coins) still loads. The stored block
// tests/coopplatformer-save-v4.dat (tests/coopplatformer-save-v4-make.mjs) carries settings and progress that are not
// the defaults; the game reads it (COOP_SAVE_FIXTURE) and carries it forward before the shell checks it. It must keep
// the level, the unlocked levels, coins, abilities, totals, hats, the screen shake setting and the music volume, and
// start the big coins empty (the fixture has 0xff where they go). Control (COOP_SAVE_NO_MIGRATE): the shell starts
// over, so every field is back to its default and the check must fail. Native, software GL, no GPU lock.
//   node tests/coopplatformer-save.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const DIR = process.env.COOP_BUILD_DIR || join(ROOT, 'build/native/games/coopplatformer');
const BIN = join(DIR, 'coopplatformer');
const OUT = join(ROOT, 'out', 'save-gate');
mkdirSync(OUT, { recursive: true });
const FIXTURE = join(ROOT, 'tests/coopplatformer-save-v4.dat');
// migrated, level, unlocked, coins, abilities, total deaths, hats, shake (2: Off), music volume
const WANT = [1, 7, 9, 123, 3, 42, 2, 2, 3];

function run(extra) {
  const state = join(OUT, 'state.json');
  rmSync(state, { force: true });
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', LIBGL_ALWAYS_SOFTWARE: '1', CW_GPU: 'gl', CW_TEST_GPU: 'software',
    CW_FRAMES: '5', CW_DEBUG_DUMP: state, CW_ASSETS: BIN + '.assets', COOP_SAVE_FIXTURE: FIXTURE, CW_SAVE_FILE: join(OUT, 'unused.dat'), ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const r = spawnSync(BIN, [], { cwd: DIR, env, encoding: 'utf8', timeout: 120000 });
  let s = {};
  try { s = JSON.parse(readFileSync(state, 'utf8')); } catch { return { err: `no state (exit ${r.status}): ${(r.stderr || '').split('\n').slice(-5).join(' | ')}` }; }
  const f = (k) => s[k] ?? s.fields?.[k];
  return { m: f('g.migrate'), c: f('g.migrate_coins') };
}
const same = (a, b) => Array.isArray(a) && a.length === b.length && a.every((v, i) => v === b[i]);
let bad = 0;
const r = run({});
if (r.err) { console.log(`FAIL v4 save: ${r.err}`); bad++; }
else {
  const ok = same(r.m, WANT) && r.c === 0;
  if (!ok) bad++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} v4 save carried forward: g.migrate ${JSON.stringify(r.m)} (want ${JSON.stringify(WANT)}), big coins ${r.c} (want 0)`);
}
const c = run({ COOP_SAVE_NO_MIGRATE: '1' });
if (c.err) { console.log(`FAIL control: ${c.err}`); bad++; }
else {
  const differs = !(same(c.m, WANT) && c.c === 0);
  if (!differs) bad++;
  console.log(`${differs ? 'ok  ' : 'FAIL'} control: no migration, the shell starts over: g.migrate ${JSON.stringify(c.m)} (must differ)`);
}
rmSync(OUT, { recursive: true, force: true });
console.log(bad ? `FAIL coopplatformer-save: ${bad}` : 'ok   coopplatformer-save');
process.exit(bad ? 1 : 0);
