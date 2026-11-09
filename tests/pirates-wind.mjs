// pirates' wind is a thing you sail by, and the AI captains sail by it too: on both maps (native, GL), a ship held at
// full sail on eight headings round the wind makes its best speed with the wind on its beam, less running before it
// and least beating into it (beam at least 1.5 x beating); an AI captain set between two crates, one 9 m dead upwind
// and the other 11 m downwind (and then the other way round), sails for the one that pays first: the one it then
// takes home to its harbor in fewer ticks, measured by sailing each run in turn; and an AI captain routed between
// every pair of harbors joined by sea arrives on every leg.
// Controls, which must fail: sw wind_blind 1 (the AI picks the nearer crate, as it once did) picks the slower one in
// at least one of the two layouts;
// sw no_wind 1 (every heading the same speed) has no beam reach to speak of.
//   node tests/pirates-wind.mjs
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const EXE = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/pirates.json'), 'utf8')).binary;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

function run(cmds) {
  const env = { ...process.env, SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', CW_FRAMES: '30', CW_WARMUP_SPREAD: '0', CW_HOST_PREF: '320x180', PIRATES_SMALL: '1',
    CW_DEBUG_CMDS: cmds.join(';') };
  const r = spawnSync(EXE, [], { env, encoding: 'utf8', timeout: 600000, maxBuffer: 1 << 26 });
  if (r.status !== 0) { console.error(`exit ${r.status}\n${(r.stderr || '').slice(-1500)}`); process.exit(3); }
  const line = (r.stdout + r.stderr).split('\n').find((l) => l.startsWith('{"map":') && l.includes('speed_by_heading'));
  if (!line) { console.error('no reply from pirates_test_wind'); process.exit(3); }
  return JSON.parse(line);
}

for (const map of [0, 1]) {
  const w = run([`pirates_map ${map}`, 'pirates_test_wind']);
  check(`map ${map}: points of sail`, w.beam > w.running && w.running > w.beating && w.beam >= 1.5 * w.beating,
    `beam reach ${w.beam.toFixed(2)} m/s, running ${w.running.toFixed(2)}, beating ${w.beating.toFixed(2)} (by heading ${w.speed_by_heading.map((v) => v.toFixed(2)).join(' ')})`);
  check(`map ${map}: harbor to harbor`, w.harbor_legs > 0 && w.stuck === 0, `${w.harbor_legs} legs, ${w.stuck} not arrived in 40 s, ${w.harbor_ticks} ticks in all`);
  let controlWrong = 0;
  for (const nearDown of [0, 1]) {
    const c = nearDown ? run([`pirates_map ${map}`, 'pirates_test_wind 1']) : w;
    const faster = c.upwind_crate_ticks < c.downwind_crate_ticks ? 'upwind' : 'downwind';
    const sailed = [c.upwind_crate_ticks, c.downwind_crate_ticks].every((t) => t > 0 && t < 3600);
    check(`map ${map}, the nearer crate ${c.nearer}: the AI sails for the crate that pays first`, sailed && c.picked === faster,
      `picked the ${c.picked} crate; to it and home: upwind ${c.upwind_crate_ticks} ticks, downwind ${c.downwind_crate_ticks}`);
    const b = run([`pirates_map ${map}`, 'sw wind_blind 1', `pirates_test_wind ${nearDown}`]);
    if (b.picked !== faster) controlWrong++;
  }
  check(`map ${map} control: sw wind_blind 1 (the nearer crate) picks the slower one at least once`, controlWrong > 0, `${controlWrong} of 2`);
  const n = run([`pirates_map ${map}`, 'sw no_wind 1', 'pirates_test_wind']);
  check(`map ${map} control: sw no_wind 1 has no beam reach`, n.beam < 1.5 * n.beating, `beam ${n.beam.toFixed(2)}, beating ${n.beating.toFixed(2)}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
