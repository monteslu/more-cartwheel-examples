// The flash sweep: no game strobes. Each entry of tests/flash-sweep.json is a stretch of real play through a game's
// brightest moments (explosions, hits, weapons, pickups, weather, menus, transitions), run natively on the fixed clock
// with every frame captured, and held to the photosensitive flash rules by the engine's tools/flash-check.mjs
// (WCAG 2.2 success criterion 2.3.1 and the Harding test's thresholds: at most three flashes and three red flashes a
// second over a sixth of the screen, the same for small lights switching together, no regular flicker above 3 Hz).
// Every entry also runs its CONTROL, which must fail: the same frames with an 8 Hz full-screen strobe laid over them.
//
// An entry names a gate of tests/*gates*.json (its app, input script, start requests and environment) and how many
// frames to check, or gives those itself: { "name", "gate" | "app", "frames", "from"?, "input"?, "startCmds"?, "env"?,
// "gpu"?, "maxRise"?, "knownFail"? } (maxRise: also bound the hardest two-frame rise, for a moment that must swell, such as
// lightning; knownFail: a failure waiting on a fix outside this repository, reported as KNOWN and not counted, and reported
// again when it starts to pass so the mark comes off).
//   node tests/flash-sweep.mjs [name ...] [--keep]    (--keep: each run's frames stay in out/flash/<name>.cwfd)
import { spawnSync } from 'node:child_process';
import { readFileSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const only = process.argv.slice(2).filter((a) => !a.startsWith('--')), keep = process.argv.includes('--keep');
const { takeSuiteLock } = await import(join(ENGINE, 'tools/lib/suite-lock.mjs'));
const { enterGpuSandbox } = await import(join(ENGINE, 'tools/lib/gpu-sandbox.mjs'));
await enterGpuSandbox();
await takeSuiteLock('flash-sweep');

const gates = new Map();
for (const f of readdirSync(join(ROOT, 'tests')).filter((n) => /gates.*\.json$/.test(n)))
  for (const g of JSON.parse(readFileSync(join(ROOT, 'tests', f), 'utf8'))) if (g.app) gates.set(g.name, g);
const entries = JSON.parse(readFileSync(join(ROOT, 'tests/flash-sweep.json'), 'utf8')).filter((e) => !only.length || only.includes(e.name));
if (!entries.length) { console.log('FAIL no sweep entries (tests/flash-sweep.json, or the names given)'); process.exit(1); }
let bad = 0, known = 0;
for (const e of entries) {
  const g = e.gate ? gates.get(e.gate) : {};
  if (!g) { console.log(`FAIL ${e.name}: no gate named ${e.gate}`); bad++; continue; }
  const app = e.app || g.app, input = e.input ?? g.input, start = e.startCmds ?? g.startCmds, env = { ...(g.env || {}), ...(e.env || {}) };
  const args = [join(ENGINE, 'tools/flash-check.mjs'), '--app', app, '--frames', String(e.frames ?? g.frames), '--from', String(e.from ?? 2),
    '--label', e.name, '--gpu', e.gpu || 'gl'];
  if (input) args.push('--input', join(ROOT, 'tests', input));
  if (start && start.length) args.push('--start-cmds', start.join(';'));
  if (e.maxRise != null) args.push('--max-rise', String(e.maxRise));
  if (keep) args.push('--keep', join(ROOT, 'out/flash', `${e.name}.cwfd`));
  const r = spawnSync('node', args, { encoding: 'utf8', env: { ...process.env, ...env, CW_PROJECT_ROOT: ROOT } });
  process.stdout.write(r.stdout || '');
  if (e.knownFail) {
    console.log(r.status !== 0 ? `KNOWN ${e.name} fails, waiting on: ${e.knownFail}` : `NOTE ${e.name} passes now: take its knownFail mark off`);
    if (r.status !== 0) known++;
    continue;
  }
  if (r.status !== 0) { bad++; if (r.stderr) process.stdout.write(r.stderr.slice(-1500)); }
}
console.log(bad ? `FAIL flash sweep: ${bad} of ${entries.length} runs` : `ok   flash sweep: ${entries.length - known} runs, none strobes, every control fails${known ? ` (${known} known failure(s) waiting on fixes elsewhere)` : ""}`);
process.exit(bad ? 1 : 0);
