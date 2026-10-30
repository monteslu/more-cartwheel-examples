// hordeshooter's floors are grounded and clear (LEVELS.md section 6): games/hordeshooter/tools/check-levels.mjs over
// every .cwlevel (every placement on the floor or on the prop under it within 3 cm, no overhang, the hatch's
// circle clear, solids apart and inside the arena, no solid on a hazard, every named file there) must pass, and
// its control (each level's first placement lifted 0.5 m) must fail. No GPU. Run: node tests/hordeshooter-levels-check.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const tool = join(resolve(dirname(fileURLToPath(import.meta.url)), '..'), 'games/hordeshooter/tools/check-levels.mjs');
const run = (extra) => spawnSync('node', [tool, ...extra], { encoding: 'utf8' });
let bad = 0;
const r = run([]);
const n = (r.stdout.match(/placements/g) || []).length;
if (r.status === 0 && n >= 7) console.log(`ok   every floor grounded and clear (${n} levels)`);
else { console.log(`FAIL check-levels: status ${r.status}, ${n} levels\n${(r.stdout + r.stderr).slice(-800)}`); bad++; }
const c = run(['--control']);
if (c.status === 0) { console.log('CONTROL PASSED (a failure): a prop lifted 0.5 m was not caught'); bad++; }
else console.log('ok   control: a prop lifted 0.5 m fails the check');
process.exit(bad ? 1 : 0);
