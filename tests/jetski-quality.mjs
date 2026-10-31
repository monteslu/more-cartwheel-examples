// jetski has a program for everything it draws at every quality on every course and backend: a state with no program is a skipped draw and a
// red banner in a player's hands. Read from the engine's own count (cw.three.missing). Its proof it can fail: 2 missing at Low and Medium
// before the deck's paint had its states in the table.
//   node tests/jetski-quality.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/quality'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
let fail = 0;
for (const g of ['gl', 'webgpu']) for (const q of ['low', 'medium', 'high', 'ultra']) for (const c of [0, 1]) {
  const state = join(OUT, 's.json');
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', g, '--app', 'jetski', '--frames', '60', '--shot', join(OUT, 's.png'), '--state', state, '--start-cmds', `quality ${q};course ${c};race`, '--exe', process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski')], { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, SDL_AUDIODRIVER: 'dummy' } });
  const m = r.status === 0 ? JSON.parse(readFileSync(state, 'utf8'))['cw.three.missing'] : 'run failed';
  if (m !== 0) fail++;
  console.log(`${m === 0 ? 'ok  ' : 'FAIL'} ${g} ${q} course ${c}: missing ${m}`);
}
process.exit(fail ? 1 : 0);
