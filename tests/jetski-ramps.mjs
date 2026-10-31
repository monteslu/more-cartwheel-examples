// jetski's ramps stand: rigid structures on piles, for a race's worth of ticks at the biggest sea of every difficulty on both courses, and
// on Cold Sound at both ends of its tide. Around each ramp every tick: no pile's foot above the water (a gap), the deck's low end under the
// water (a rider climbs on from the sea), the top clear of the water. Control: lifted 60 m, the check must see gaps. The jump: the same
// approach at different moments of the sea gives the same air; control: decks that tip with the swell do not.
//   node tests/jetski-ramps.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/ramps'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
const SIMX = 12;
let n = 0;
function run(cmds, ticks, buttons = []) {
  const tag = 'r' + n++, state = join(OUT, tag + '.json'), input = join(OUT, tag + '.input.json'), frames = Math.ceil(ticks / (SIMX + 1)) + 4;
  writeFileSync(input, JSON.stringify([{ frame: 4, buttons }]));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, tag + '.png'), '--start-cmds', cmds, '--exe', process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski')], {
    cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26,
    env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(SIMX), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '280000' },
  });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const D = ['easy', 'normal', 'hard'], C = ['Crater Atoll', 'Cold Sound'], f = (v) => (+v).toFixed(2);
const stand = (name, cmds) => {
  const s = run(cmds, 24000);
  check(name, s['ramp.points'] > 100000 && s['ramp.gap_points'] === 0 && s['ramp.dry_entry'] === 0 && s['ramp.swamped'] === 0,
    `${s['ramp.points']} points; foot over water ${s['ramp.gap_points']}; low end dry ${s['ramp.dry_entry']} ticks, least under ${f(s['ramp.entry_min'])} m; swamped ${s['ramp.swamped']} ticks, least clear ${f(s['ramp.lip_min'])} m; foot over the sea bed by ${f(s['ramp.foot_ground_max'])} m at most`);
};
for (let c = 0; c < 2; c++) for (let d = 0; d < 3; d++) stand(`${C[c]} ${D[d]}`, `course ${c};difficulty ${d};swell 1;race;ramp 0 400 300`);
for (const [t, tag] of [[2.6, 'high'], [-1.6, 'low']]) stand(`Cold Sound hard, ${tag} tide`, `course 1;difficulty 2;swell 1;tide ${t};race;ramp 0 400 300`);
const lifted = run('course 1;difficulty 2;swell 1;ramp_lift 60;race;ramp 0 400 300', 2400);
check('control: lifted 60 m, the check sees gaps', lifted['ramp.gap_points'] > 1000, `${lifted['ramp.gap_points']} points`);
// the jump: five moments of the sea, the same approach, the same air; every ramp of a course gets some
const jumps = (extra, c, tide) => [0, 7.3, 13.1, 21.7, 29.9].map((off, i) => {
  const s = run(`course ${c};difficulty 2;swell 1;${tide};${extra}race;set Root/Water time_offset ${off};ramp ${i * 2} 40`, 560, ['south']);
  return { air: s['p0.air_max'], ramps: s['p0.ramps'], spills: s['p0.spills'] };
});
const spread = (a) => { const v = a.map((x) => x.air); return (Math.max(...v) - Math.min(...v)) / Math.max(...v); };
for (const [c, tide, tag] of [[0, '', 'Crater Atoll'], [1, 'tide 1', 'Cold Sound']]) {
  const j = jumps('', c, tide), s0 = spread(j);
  check(`${tag}: a big, repeatable jump`, j.every((x) => x.ramps >= 1 && x.air >= 2.0) && s0 <= 0.15, `air ${j.map((x) => f(x.air)).join(' ')} s, spread ${(s0 * 100).toFixed(0)}%`);
  check(`${tag}: clean landings`, j.every((x) => x.spills === 0), `spills ${j.map((x) => x.spills).join(' ')}`);
}
// the computer riders take the ramps and land them, and a ramp does not cost them buoys; control: no one goes for a ramp
for (let c = 0; c < 2; c++) {
  const on = run(`course ${c};race`, 16000), off = run(`ai_ramps 0;course ${c};race`, 16000);
  const rate = (s) => s['ai.misses'] / Math.max(1, s['ai.misses'] + s['ai.passes']);
  check(`${C[c]}: the computer riders use the ramps`, on['ai.ramps'] >= 15, `${on['ai.ramps']} ramps taken, control ${off['ai.ramps']}`);
  check(`${C[c]}: and land them`, on['ai.spills'] <= on['ai.ramps'] * 0.05 && on['ai.walls'] <= on['ai.ramps'] * 0.25, `${on['ai.spills']} spills, ${on['ai.walls']} ticks against a side wall in ${on['ai.ramps']} ramps; control ${off['ai.spills']} spills, ${off['ai.walls']} ticks`);
  check(`${C[c]}: a ramp costs no buoy`, rate(on) <= rate(off) + 0.05, `missed ${(rate(on) * 100).toFixed(1)}% of ${on['ai.misses'] + on['ai.passes']} with ramps, ${(rate(off) * 100).toFixed(1)}% of ${off['ai.misses'] + off['ai.passes']} without`);
}
const tip = jumps('ramp_seesaw 1;', 1, 'tide 1'), s1 = spread(tip);
check('control: decks that tip with the swell do not repeat', s1 > 0.15, `air ${tip.map((x) => f(x.air)).join(' ')} s, spread ${(s1 * 100).toFixed(0)}%`);
process.exit(fail ? 1 : 0);
