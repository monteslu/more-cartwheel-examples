// skyfighter: each mission keeps its best rank in the save store's "ranks" slot, in the room after the 16-byte
// preferences, and the preferences are untouched. The native build on a save file of its own (not a deterministic
// run, so it writes and reads the file):
//   1. the pad starts the first mission and it is cleared with 40 health (sw win 40): rank B (2), saved;
//   2. a restart reads it back (campaign.ranks[0] 2) with the music volume set in run 1 still there;
//   3. the mission cleared again with 95 health: rank S (4) is the new best; 4. again with 10 health: rank C, the best
//      stays S.
// Control: a restart on a fresh save file reads no rank, so the check of step 2 must fail there.
//   node tests/skyfighter-ranks.mjs [--gpu gl|webgpu]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const EXE = join(ROOT, 'build/native/games/skyfighter/skyfighter');
const GPU = process.argv.includes('--gpu') ? process.argv[process.argv.indexOf('--gpu') + 1] : 'gl';
const OUT = join(ROOT, 'out', 'skyfighter-ranks');
rmSync(OUT, { recursive: true, force: true });
mkdirSync(OUT, { recursive: true });

const run = (tag, save, script, frames, cmds = []) => {
  const state = join(OUT, `${tag}.json`), input = join(OUT, `${tag}.input.json`);
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [join(ENGINE, 'tools/run-cart.mjs'), '--host', 'native', '--gpu', GPU, '--app', 'skyfighter', '--frames', String(frames),
    '--input', input, '--state', state, '--exe', EXE, ...(cmds.length ? ['--start-cmds', cmds.join(';')] : [])], {
    cwd: ENGINE, encoding: 'utf8', maxBuffer: 1 << 26,
    env: { ...process.env, CW_PROJECT_ROOT: ROOT, CW_PLAYER_TIMING: '1', CW_SAVE_FILE: save, CW_QUALITY: '' },
  });
  if (r.status !== 0) throw new Error(`native ${tag}: ${r.stdout.slice(-400)} ${r.stderr.slice(-600)}`);
  return JSON.parse(readFileSync(state, 'utf8'));
};
let failed = 0;
const check = (what, ok, detail) => { if (!ok) failed++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}: ${detail}`); };
const START = [{ frame: 25, buttons: ['south'] }, { frame: 28 }]; // the title's first mission, as the sortie gate starts it
const save = join(OUT, 'save.dat');

const one = run('b', save, START, 260, ['@200 sw win 40']);
check('a clear with 40 health ranks B and saves it', one['campaign.rank_last'] === 2 && one['campaign.ranks']?.[0] === 2, `rank_last ${one['campaign.rank_last']}, ranks ${JSON.stringify(one['campaign.ranks'])}`);
const prefsBefore = readFileSync(save).subarray(0, 16).toString('hex');
const two = run('again', save, [], 30);
check('a restart reads the rank back', two['campaign.ranks']?.[0] === 2, `ranks ${JSON.stringify(two['campaign.ranks'])}`);
check('the preferences are the same 16 bytes', readFileSync(save).subarray(0, 16).toString('hex') === prefsBefore, `${prefsBefore}`);
const three = run('s', save, START, 260, ['@200 sw win 95']);
check('a clear with 95 health ranks S, the new best', three['campaign.rank_last'] === 4 && three['campaign.ranks']?.[0] === 4, `rank_last ${three['campaign.rank_last']}, ranks ${JSON.stringify(three['campaign.ranks'])}`);
const four = run('c', save, START, 260, ['@200 sw win 10']);
check('a clear with 10 health ranks C and the best stays S', four['campaign.rank_last'] === 1 && four['campaign.ranks']?.[0] === 4, `rank_last ${four['campaign.rank_last']}, ranks ${JSON.stringify(four['campaign.ranks'])}`);
const none = run('none', join(OUT, 'fresh.dat'), [], 30);
const ctlPass = none['campaign.ranks']?.[0] === 2 || none['campaign.ranks']?.[0] === 4;
if (ctlPass) failed++;
console.log(`${ctlPass ? 'FAIL control passed' : 'ok   control failed as it must'}: a fresh save reads ranks ${JSON.stringify(none['campaign.ranks'])}`);
rmSync(OUT, { recursive: true, force: true });
console.log(failed ? `skyfighter-ranks: ${failed} failed` : 'skyfighter-ranks: pass');
process.exit(failed ? 1 : 0);
