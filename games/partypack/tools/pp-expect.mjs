// Records the exact `expect` values of the gates in tests/partypack-gates.json that have none yet: runs each once
// on the native host (the gate's own env and input) and stores the state fields the gate compares. Run it inside the
// GPU suite (scripts/partypack-suite.sh expect), then read the diff before committing: a recorded value is only as good
// as the run, so the cross-host legs and the controls of the gate are what prove it.
//   node games/partypack/tools/pp-expect.mjs [gate ...]
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const E = join(ROOT, 'deps', 'cartwheel-engine');
const file = join(ROOT, 'tests', 'partypack-gates.json');
const gates = JSON.parse(readFileSync(file, 'utf8'));
const want = process.argv.slice(2);
const KEEP = ['game.state', 'game.round', 'game.mg', 'game.points', 'game.hash', 'game.points_total', 'game.humans'];
mkdirSync(join(ROOT, 'out', 'pp'), { recursive: true });
for (const g of gates) {
  if (want.length ? !want.includes(g.name) : g.expect) continue;
  const st = join(ROOT, 'out', 'pp', g.name + '.expect.json');
  const args = [join(E, 'tools', 'run-cart.mjs'), '--host', 'native', '--app', g.app, '--frames', String(g.frames), '--state', st, '--assets', join(ROOT, g.assets)];
  if (g.input) args.push('--input', join(ROOT, 'tests', g.input));
  const r = spawnSync('node', args, { env: { ...process.env, CW_PROJECT_ROOT: ROOT, ...(g.env || {}) }, encoding: 'utf8' });
  if (r.status !== 0) { console.error(`${g.name}: run failed\n${r.stderr}`); process.exit(1); }
  const s = JSON.parse(readFileSync(st, 'utf8'));
  g.expect = {};
  for (const k of (g.fields || KEEP).filter((k) => KEEP.includes(k))) g.expect[k] = s[k];
  console.log(g.name, JSON.stringify(g.expect));
}
writeFileSync(file, JSON.stringify(gates, null, 2) + '\n');
