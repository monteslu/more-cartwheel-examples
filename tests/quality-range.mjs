// Every game's GPU time at each graphics preset, natively on GL and WebGPU at 1080p, on the test GPU: the engine's
// tools/quality-range.mjs over the scenes in tests/quality-range-scenes.json (each game in play, with its own shares
// of High). Its control (every preset drawing High's settings) must fail.
//   node tests/quality-range.mjs [--apps pirates,jetski]
import { spawnSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const r = spawnSync('node', [join(ENGINE, 'tools/quality-range.mjs'), '--project', ROOT, '--scenes', join(ROOT, 'tests/quality-range-scenes.json'), ...process.argv.slice(2)], { stdio: 'inherit' });
process.exit(r.status ?? 1);
