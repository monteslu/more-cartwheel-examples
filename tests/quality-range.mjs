// Every game's GPU time at each graphics preset, natively on GL and WebGPU at 1080p, on the test GPU: the engine's
// tools/quality-range.mjs over the scenes in tests/quality-range-scenes.json (each game in play, with its own shares
// of High). Its control (every preset drawing High's settings) must fail. A GPU suite may hold the lock 15 minutes, so
// the games run in three parts (the gates quality-range-1, -2 and -3 each run one part through tests/quality-range-<n>.mjs).
//   node tests/quality-range.mjs [--part 1|2|3] [--apps pirates,jetski]
import { spawnSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
export const PARTS = { 1: 'pirates,jetski,arenafps', 2: 'tuxracer,skyfighter,coopplatformer', 3: 'fighter,brawler,hordeshooter,partypack' };
export function runPart(part, extra = []) {
  const apps = part ? ['--apps', PARTS[part]] : [];
  const r = spawnSync('node', [join(ENGINE, 'tools/quality-range.mjs'), '--project', ROOT, '--scenes', join(ROOT, 'tests/quality-range-scenes.json'), ...apps, ...extra], { stdio: 'inherit' });
  return r.status ?? 1;
}
if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const argv = process.argv.slice(2);
  const i = argv.indexOf('--part');
  const part = i >= 0 ? argv[i + 1] : null;
  process.exit(runPart(part, argv.filter((a, k) => k !== i && k !== i + 1)));
}
