// brawler proportions gate: every shipped character model is in human proportion (games/brawler/tools/check-heads.mjs reads the glb files),
// and two controls built by the generator must fail: a tiny head on a long neck, and an ellipsoid balloon head a sixth of the body.
// The garments too (games/brawler/tools/check-garments.mjs): the wanker's gut, the men's sleeveless T-shirts; the creep's coat through his clips; controls: the gut as a narrow sack, the men in women's tank tops, the coat's fronts that stay put when his arms spread, a skirt whose fronts do not go with his thighs.
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
const GARMENTS = join(TOOLS, 'check-garments.mjs');
const shipped = run([GARMENTS]);
console.log(shipped.stdout);
if (shipped.status) { console.log('FAIL the shipped garments'); bad++; } else console.log('ok   the shipped garments');
const SET = ['wanker', 'jet', 'bruiser', 'creep'];
for (const [prop, names] of [['sack', SET], ['tanktop', SET], ['stuckcoat', SET], ['stiffskirt', SET]]) {
  const dir = join(ROOT, 'build', 'garments-control-' + prop), tex = join(dir, 'tex');
  rmSync(dir, { recursive: true, force: true }); mkdirSync(dir, { recursive: true });
  cpSync(join(ROOT, 'games/brawler/assets/textures'), tex, { recursive: true });
  const g = run([GEN, ...names], { BRAWLER_PROP: prop, BRAWLER_MODELS_OUT: dir, BRAWLER_TEX_DIR: tex });
  if (g.status) { console.log(`FAIL control ${prop} did not build`); bad++; }
  const c = run([GARMENTS, '--dir', dir]);
  console.log(c.stdout);
  const named = prop === 'sack' ? /wanker[^\n]*\n\s+BAD/.test(c.stdout) : prop === 'tanktop' ? /jet[^\n]*\n\s+BAD/.test(c.stdout) && /bruiser[^\n]*\n\s+BAD/.test(c.stdout) : prop === 'stuckcoat' ? /BAD[^\n]*briefs/.test(c.stdout) : /BAD[^\n]*inside a bare leg/.test(c.stdout);
  if (!c.status && !named) { console.log(`CONTROL PASSED (a failure): ${prop}`); bad++; } else if (!named) { console.log(`CONTROL FAILED FOR ANOTHER REASON: ${prop}`); bad++; } else console.log(`ok   control ${prop} fails as it must`);
  rmSync(dir, { recursive: true, force: true });
}
process.exit(bad ? 1 : 0);
