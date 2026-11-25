// What a trick is worth in stunt mode, from the game's own scorer (stunt_score: tricks as one flight's combo, banked, on a
// clean slate), on the CPU with a software GL. The same trick twice in one combo pays at most 80 % of what two of that
// trick would in full (the combo's 1.5x on both); two different tricks pay over 2x the first alone. Control: wxsw
// trick_repeat 0 (every repeat in full) fails the first bound.
//   node tests/jetski-trick-value.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/trick-value'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
writeFileSync(join(OUT, 'i.json'), '[]');
const cmds = ['stunt_score 1', 'stunt_score 1 1', 'stunt_score 1 2', 'wxsw trick_repeat 0', 'stunt_score 1 1'];
const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', '6', '--input', join(OUT, 'i.json'), '--state', join(OUT, 's.json'), '--shot', join(OUT, 's.png'), '--start-cmds', cmds.join(';'), '--exe', EXE],
  { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy' } });
if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
const got = [...(r.stdout + r.stderr).matchAll(/start cmd: stunt_score [0-9 ]+ -> (\d+)/g)].map((m) => +m[1]);
if (got.length !== 4) { console.error('replies: ' + got); process.exit(3); }
const [one, twice, two, twice0] = got;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const full = Math.round(2 * one * 1.5);
check('the same trick twice pays at most 80 % of two in full', twice <= 0.8 * full, `${twice} against ${full}`);
check('two different tricks pay over 2x the first alone', two > 2 * one, `${two} against ${one}`);
check('control: trick_repeat 0 pays the repeat in full (fails the first bound)', !(twice0 <= 0.8 * full), `${twice0} against ${full}`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
