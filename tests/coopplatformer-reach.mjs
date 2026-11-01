// coopplatformer: every coin, ability pedestal, flag, cracked wall and room of every level can be reached with the game's
// own movement, and no coin sits in rock (games/coopplatformer/reach.c runs the search, no picture, no GPU).
// The control is the levels as they were (COOP_REACH_OLD=1): its search must find the faults.
//   node tests/coopplatformer-reach.mjs [--level N]
import { spawn } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const DIR = join(ROOT, 'build/native/games/coopplatformer');
const BIN = join(DIR, 'coopplatformer_auto');
const LIMIT_MS = 8 * 60000;

/** one search, in the background at low priority; its last line says ok or FAIL */
function search(env) {
  return new Promise((done, fail) => {
    const e = { ...process.env, ...env, COOP_REACH: '1', SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2' };
    for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete e[k];
    const p = spawn('nice', ['-n', '10', BIN], { cwd: DIR, env: e, stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', timedOut = false;
    p.stdout.on('data', (d) => { out += d; });
    p.stderr.on('data', () => {});
    p.on('error', fail);
    /* each search has a time limit, and running out of it fails loudly */
    const limit = setTimeout(() => { timedOut = true; p.kill('SIGKILL'); }, LIMIT_MS);
    p.on('close', (code) => {
      clearTimeout(limit);
      const lines = out.split('\n').filter((l) => l.startsWith('reach:'));
      if (timedOut) lines.push(`reach: FAIL: over the ${LIMIT_MS / 60000} minute limit for one level, TIMED OUT`);
      done({ code: timedOut ? 2 : code, lines });
    });
  });
}

/** the number of levels, from the game */
const count = await new Promise((done) => {
  const e = { ...process.env, COOP_REACH_COUNT: '1', SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', LIBGL_ALWAYS_SOFTWARE: '1' };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete e[k];
  const p = spawn(BIN, [], { cwd: DIR, env: e, stdio: ['ignore', 'pipe', 'ignore'] });
  let out = '';
  p.stdout.on('data', (d) => { out += d; });
  p.on('close', () => done(+(/levels (\d+)/.exec(out)?.[1] ?? 0)));
});
const argLevels = process.argv.includes('--levels') ? process.argv[process.argv.indexOf('--levels') + 1].split(',').map(Number) : null;
const levels = argLevels ?? Array.from({ length: count }, (_, i) => i);
let failed = 0;
for (let i = 0; i < levels.length;) {
  const batch = levels.slice(i, i + 3); // three at a time: each holds a core and a few GB for some minutes
  i += batch.length;
  const res = await Promise.all(batch.map((l) => search({ COOP_LEVEL: String(l) })));
  res.forEach((now, k) => {
    for (const line of now.lines) if (!/stage \d+ abilities/.test(line) && line !== 'reach: ok') console.log(line);
    const good = now.code === 0 && now.lines.at(-1) === 'reach: ok';
    console.log(`${good ? 'ok  ' : 'FAIL'} level ${batch[k]}`);
    if (!good) failed++;
  });
}
const nowOk = failed === 0 && levels.length > 0;
console.log(nowOk ? `ok   coopplatformer-reach: ${levels.length} levels` : 'FAIL coopplatformer-reach: the levels have places nobody can reach');

const was = await search({ COOP_REACH_OLD: '1', COOP_LEVEL: '0' });
const wasFaults = was.lines.filter((l) => /COIN|ROOM|PEDESTAL|FLAG|CRACK|GOAL/.test(l));
for (const l of wasFaults) console.log(`control: ${l}`);
const ctlOk = was.code === 1 && wasFaults.length > 0;
console.log(ctlOk ? `ok   control: the old levels fail with ${wasFaults.length} faults` : 'FAIL control: the old levels passed, so this check proves nothing');
process.exit(nowOk && ctlOk ? 0 : 1);
