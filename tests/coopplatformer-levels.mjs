// coopplatformer: every level is finished by the bots from its start, with 1, 2 and 4 players, sim only, and every level's
// scenery and things stand on the ground (the scene is built for each level on the software renderer, no GPU lock). Controls
// that must fail: bots that never hold Run do not get past the first level's running jumps, and a tree lifted off the ground
// (COOP_FLOAT_BUG) shows in the grounding numbers.
//   node tests/coopplatformer-levels.mjs [--levels a,b,c]
import { spawn } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const DIR = process.env.COOP_BUILD_DIR || join(ROOT, 'build/native/games/coopplatformer');
const BIN = join(DIR, 'coopplatformer_auto');
const OUT = join(ROOT, 'out', 'levels-gate');
mkdirSync(OUT, { recursive: true });

function run(env) {
  return new Promise((done, fail) => {
    const e = { ...process.env, ...env, SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software' };
    for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete e[k];
    const p = spawn('nice', ['-n', '10', BIN], { cwd: DIR, env: e, stdio: ['ignore', 'pipe', 'ignore'] });
    let out = '';
    p.stdout.on('data', (d) => { out += d; });
    p.on('error', fail);
    p.on('close', () => done(out));
  });
}
/** @returns {{wins:number, fastest:number, deaths:number, stall:number}} a soak's last line */
async function soak(level, players, ticks, extra = {}) {
  const out = await run({ COOP_LEVEL: String(level), COOP_SOAK: String(ticks), COOP_PLAYERS: String(players), ...extra });
  const m = /soak done ticks=\d+ wins=(\d+) .* longest_stall=(\d+) fastest_win=(\d+)/.exec(out);
  const total = /total (\d+), median/.exec(out);
  return m ? { wins: +m[1], stall: +m[2], fastest: +m[3], deaths: total ? +total[1] : -1 } : { wins: -1, stall: -1, fastest: -1, deaths: -1 };
}
const count = +(/levels (\d+)/.exec(await run({ COOP_REACH_COUNT: '1' }))?.[1] ?? 0);
const arg = process.argv.indexOf('--levels');
const levels = arg > 0 ? process.argv[arg + 1].split(',').map(Number) : Array.from({ length: count }, (_, i) => i);
let bad = 0;
if (!levels.length) { console.log('FAIL no levels'); bad++; }
let totalTicks = 0;
for (let i = 0; i < levels.length; i += 4) {
  const batch = levels.slice(i, i + 4);
  await Promise.all(batch.map(async (l) => {
    const r1 = await soak(l, 1, 14000), r2 = await soak(l, 2, 14000), r4 = await soak(l, 4, 14000);
    const good = r1.wins > 0 && r2.wins > 0 && r4.wins > 0;
    if (!good) bad++;
    totalTicks += r1.fastest;
    console.log(`${good ? 'ok  ' : 'FAIL'} level ${l}: 1p ${(r1.fastest / 3600).toFixed(1)} min x${r1.wins}, 2p ${(r2.fastest / 3600).toFixed(1)} x${r2.wins}, 4p ${(r4.fastest / 3600).toFixed(1)} x${r4.wins}; deaths ${r1.deaths} ${r2.deaths} ${r4.deaths}, longest stall ${Math.max(r1.stall, r2.stall, r4.stall)} ticks`);
  }));
}
console.log(`     the bots' fastest full run: ${(totalTicks / 3600).toFixed(0)} minutes in all`);
// grounding: the scene of each level, three frames
async function grounded(level, extra = {}) {
  const dump = join(OUT, `state-${level}-${Object.keys(extra).length}.json`);
  await run({ COOP_LEVEL: String(level), COOP_FRESH: '1', CW_FRAMES: '3', CW_DEBUG_DUMP: dump, ...extra });
  const s = JSON.parse(readFileSync(dump, 'utf8'));
  return { scenery: s['g.scenery'], things: s['g.things'] };
}
for (const l of levels) {
  const g = await grounded(l);
  const good = g.scenery && g.things && g.scenery[1] === 0 && g.things[1] === 0 && g.scenery[0] > 20 && g.things[0] > 0;
  if (!good) bad++;
  console.log(`${good ? 'ok  ' : 'FAIL'} level ${l} grounding: props ${g.scenery}, things and signs ${g.things}`);
}
// controls
const norun = await soak(0, 1, 7000, { COOP_BOT_NO_RUN: '1' });
if (norun.wins > 0) { console.log(`FAIL control: bots that never run finished the first level (${norun.wins} wins), so its gaps do not ask for a running jump`); bad++; }
else console.log(`ok   control: bots that never run do not finish the first level (wins ${norun.wins})`);
const lifted = await grounded(0, { COOP_FLOAT_BUG: '1' });
if (!lifted.scenery || lifted.scenery[1] === 0) { console.log('FAIL control: a lifted tree was not seen'); bad++; }
else console.log(`ok   control: a lifted tree shows (${lifted.scenery})`);
rmSync(OUT, { recursive: true, force: true });
console.log(bad ? `FAIL coopplatformer-levels: ${bad}` : `ok   coopplatformer-levels: ${levels.length} levels`);
process.exit(bad ? 1 : 0);
