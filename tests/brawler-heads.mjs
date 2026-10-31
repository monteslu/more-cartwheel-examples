// brawler proportions gate: every shipped character model is in human proportion (games/brawler/tools/check-heads.mjs reads the glb files),
// and two controls built by the generator must fail: a tiny head on a long neck, and an ellipsoid balloon head a sixth of the body.
// No GPU. Run: node tests/brawler-heads.mjs
import { spawnSync } from 'node:child_process';
import { cpSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), TOOLS = join(ROOT, 'games/brawler/tools');
const CHECK = join(TOOLS, 'check-heads.mjs'), GEN = join(TOOLS, 'make-characters.mjs'), NAMES = ['rook', 'mika', 'thug'];
const run = (args, env = {}) => spawnSync('node', args, { stdio: ['ignore', 'pipe', 'inherit'], encoding: 'utf8', env: { ...process.env, ...env } });
let bad = 0;
const real = run([CHECK]);
console.log(real.stdout);
if (real.status) { console.log('FAIL the shipped models'); bad++; } else console.log('ok   the shipped models');
for (const prop of ['longneck', 'balloon']) {
  const dir = join(ROOT, 'build', 'heads-control-' + prop), tex = join(dir, 'tex');
  rmSync(dir, { recursive: true, force: true }); mkdirSync(dir, { recursive: true });
  cpSync(join(ROOT, 'games/brawler/assets/textures'), tex, { recursive: true });
  const g = run([GEN, ...NAMES], { BRAWLER_PROP: prop, BRAWLER_MODELS_OUT: dir, BRAWLER_TEX_DIR: tex });
  if (g.status) { console.log(`FAIL control ${prop} did not build`); bad++; }
  const c = run([CHECK, '--dir', dir, ...NAMES]);
  console.log(c.stdout);
  if (!c.status) { console.log(`CONTROL PASSED (a failure): ${prop}`); bad++; } else console.log(`ok   control ${prop} fails as it must`);
  rmSync(dir, { recursive: true, force: true });
}
process.exit(bad ? 1 : 0);
