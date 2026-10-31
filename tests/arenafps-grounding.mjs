// arenafps: nothing floats (no GPU; software GL, a few ticks per arena). Every solid prop stands on a surface and every pickup's
// nominal height is on the floor under it, in all three arenas. The control (ARENA_PROP_LIFT=1, the first prop lifted 0.5 m) must
// fail, or the check could not see a floating prop. Run: node tests/arenafps-grounding.mjs [--build build/native]
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const i = process.argv.indexOf('--build');
const exe = join(resolve(ROOT, i > 0 ? process.argv[i + 1] : 'build/native'), 'games/arenafps/arenafps_auto');
const out = join(ROOT, 'out/grounding');
mkdirSync(out, { recursive: true });
function run(arena, extra) {
  const dump = join(out, `a${arena}.json`);
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: '2', CW_WORKERS: '2',
    CW_FRAMES: '6', CW_WARMUP_SPREAD: '0', CW_QUALITY: 'low', ARENA_RES: '320x180', ARENA_ID: String(arena), ARENA_PLAYERS: '1', CW_DEBUG_DUMP: dump, ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const p = spawnSync('nice', ['-n', '10', exe], { env, encoding: 'utf8', timeout: 300e3 });
  if (p.status !== 0) { console.error(`arena ${arena}: exit ${p.status}\n${(p.stderr || '').slice(-800)}`); process.exit(2); }
  const j = JSON.parse(readFileSync(dump, 'utf8'));
  if (j['game.state'] !== 1 || !(j['nav.links'] > 0)) { console.error(`arena ${arena}: the match did not start`); process.exit(2); }
  return { props: j['props.count'], floating: j['props.floating'], gap: j['items.gap_mm'] };
}
let bad = 0;
for (const a of [0, 1, 2]) {
  const r = run(a, {});
  console.log(`arena ${a}: ${r.props} solid props, ${r.floating} floating, largest item gap ${r.gap} mm`);
  if (r.props < 5 || r.floating || r.gap > 30) bad++;
}
const c = run(0, { ARENA_PROP_LIFT: '1' });
console.log(`control (first prop and first item lifted 0.5 m): ${c.floating} floating, item gap ${c.gap} mm`);
if (!c.floating || c.gap <= 30) { console.error('FAIL: the control did not fail'); process.exit(1); }
if (bad) { console.error('FAIL: something floats'); process.exit(1); }
console.log('ok   arenafps-grounding');
