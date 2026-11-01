// A hard hit from another craft can put a rider down; a rub never does. At top speed, at several moments of the sea: a rival into the human's flank, head on, and side by side.
// Decided by a hash of the moment, so each repeat of one setup is the same. A whole race is counted too: it must stay a race. Control: ram 0, no one goes down however hard the hit.
//   node tests/jetski-ram.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/ram'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
writeFileSync(join(OUT, 'i.json'), '[]');
function run(app, cmds, ticks, simx) {
  const state = join(OUT, 's.json');
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', app, '--frames', String(Math.ceil(ticks / (simx + 1)) + 4), '--input', join(OUT, 'i.json'), '--state', state, '--shot', join(OUT, 's.png'), '--start-cmds', cmds, '--exe', join(ROOT, 'build/native/games/jetski', app)],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', JETSKI_SIMX: String(simx), SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '280000' } });
  if (r.status !== 0) { console.error(r.stdout.slice(-300), r.stderr.slice(-400)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const SPOTS = [300, 500, 700, 950, 1200, 1500, 1800, 2100];
const set = (kind, pre = '') => SPOTS.map((s) => { const r = run('jetski', `${pre}course 0;difficulty 1;no_dq 1;race;go ${s};sw ram_setup ${kind}`, 90, 0); return { last: r['ram.last'].map((v) => (+v).toFixed(2)).join('/'), hits: r['ram.hits'], me: r['p0.spills'], him: r['p1.spills'], fb: r.fb[41] + r.fb[37] }; });
const sum = (v, k) => v.reduce((a, b) => a + b[k], 0), down = (v) => v.filter((x) => x.me + x.him > 0).length, both = (v) => v.filter((x) => x.me > 0 && x.him > 0).length;
const tb = set(0), he = set(1), rub = set(2), off = set(0, 'sw ram 0;');
console.log('     last hit, closing/flank of the human/flank of the rival:', tb.map((x) => x.last).join(' '));
check('a rival into the flank usually puts someone down', down(tb) >= 5 && sum(tb, 'hits') >= 6, `${sum(tb, 'hits')} hits of ${SPOTS.length}, someone down in ${down(tb)}, the one on the flank ${sum(tb, 'me')}, the one who hit ${sum(tb, 'him')}, both ${both(tb)}`);
check('the one on the flank goes down more often than the one who hit', sum(tb, 'me') > sum(tb, 'him'), `${sum(tb, 'me')} against ${sum(tb, 'him')}`);
check('not a certainty', down(tb) + down(he) < SPOTS.length * 2 || both(tb) < SPOTS.length, `flank ${down(tb)}, head on ${down(he)}, of ${SPOTS.length} each, both ${both(tb)}`);
check('head on at speed can take both', down(he) >= 5 && both(he) >= 1, `someone down in ${down(he)}, both in ${both(he)}`);
check('side by side at the same speed: nobody', down(rub) === 0, `${sum(rub, 'hits')} hits, someone down in ${down(rub)}`);
check('the human is told', tb.every((x) => x.me === 0 || x.fb > 0), `sound on each human wipeout`);
check('control: ram 0, nobody goes down', down(off) === 0, `someone down in ${down(off)}`);
const C = ['Crater Atoll', 'Cold Sound', 'Red Reach', 'Iron Quay', 'Gale Bank'];
if (!process.argv.includes("--quick")) for (let lv = 0; lv < C.length; lv++) {
  const r = run('jetski_auto', `course ${lv};no_dq 1;race`, 18000, 24);
  check(`${C[lv]}: a whole race stays a race`, r['ram.wipes'] <= 6 && r['ai.stuck'] < 60, `${r['ram.hits']} hard hits, ${r['ram.wipes']} riders down in ${(+r['race.clock'] / 60).toFixed(1)} min`);
}
process.exit(fail ? 1 : 0);
