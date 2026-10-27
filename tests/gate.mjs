// The games' gates (tests/gates.json), run by cartwheel-engine's gate runner
// (deps/cartwheel-engine/tests/gate.mjs) against this repository's builds.
// Arguments pass through:
//
//   node tests/gate.mjs [gate-name ...] [--hosts native,romdev,chromium,wcnative] [--gpu gl|webgpu]
//
// Each gate runs its app on every host it names with the same frame count and
// input script and compares each run with the native one: frames within a
// pixel bound, named debug state exactly. A control run (one frame short, or
// another input) must fail, or the gate could not tell runs apart.
import { spawn } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const runner = join(ROOT, 'deps/cartwheel-engine/tests/gate.mjs');
const p = spawn('node', [runner, ...process.argv.slice(2)], { stdio: 'inherit', env: { ...process.env, CW_PROJECT_ROOT: ROOT } });
p.on('close', (code) => process.exit(code ?? 1));
