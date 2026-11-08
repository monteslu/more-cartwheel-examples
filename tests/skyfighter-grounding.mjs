// skyfighter: nothing floats (no GPU; software GL, one sortie launched per mission). In every mission each placed prop (buildings,
// their instanced blocks and roofs, landmarks, walls, trees, rigs and barges, each building's ruin) stands on the ground under its
// whole base, sits in something that carries it, or reaches below the deepest wave trough at sea; draped roads and fields keep to the
// ground (games/skyfighter/src/grounding.h). The controls (SKY_GROUND_OLD_DISTRICTS=1: the district blocks placed as before, the island's
// by a slope test along one axis at the centre and Steel Tide's 5 m above the sea) must fail in Coastal Scramble and in Steel Tide, or
// the check could not see a building hanging off a hillside or a slab over the water.
// Run: node tests/skyfighter-grounding.mjs [--build build/native] [--jobs 3]
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (name, def) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : def; };
const exe = join(resolve(ROOT, arg('--build', 'build/native')), 'games/skyfighter/skyfighter');
const jobs = Number(arg('--jobs', 3));
const out = join(ROOT, 'out/skyfighter-grounding');
mkdirSync(out, { recursive: true });
const input = join(out, 'launch.txt');
writeFileSync(input, '25 1\n28 0\n'); // South on the title launches the selected mission
const NAMES = ['Country Fair', 'Time Warp', 'Fuel Chain', 'Coastal Scramble', 'Sky Fortress', 'Red Fortress', 'Operation Snowglobe', 'Steel Tide'];
function run(mission, extra, tag) {
  const dump = join(out, `${tag}.json`);
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: '2', CW_WORKERS: '2',
    CW_FRAMES: '45', CW_WARMUP_SPREAD: '0', CW_QUALITY: 'verylow', CW_INPUT: input, SKY_ART_MISSION: String(mission), SKY_GROUND_CHECK: '1', CW_DEBUG_DUMP: dump, ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  return new Promise((done) => {
    const p = spawn('nice', ['-n', '10', exe], { env });
    let log = '';
    p.stderr.on('data', (d) => { log += d; });
    p.stdout.on('data', (d) => { log += d; });
    const timer = setTimeout(() => p.kill('SIGKILL'), 600e3);
    p.on('close', (code) => {
      clearTimeout(timer);
      if (code !== 0) { console.error(`${tag}: exit ${code}\n${log.slice(-800)}`); process.exit(2); }
      const j = JSON.parse(readFileSync(dump, 'utf8'));
      if (j['mission.state'] !== 1 || j['mission.level'] !== mission || !(j['ground.units'] > 0)) { console.error(`${tag}: mission ${mission} did not start (state ${j['mission.state']}, level ${j['mission.level']})`); process.exit(2); }
      done({ mission, units: j['ground.units'], floating: j['ground.floating'], worst: j['ground.worst_mm'], at: j['ground.worst'], surfaces: j['ground.surfaces'], surfaceFloating: j['ground.surface_floating'], lines: log.split('\n').filter((l) => l.startsWith('ground: ') && !l.includes(' props, ')) });
    });
  });
}
async function pool(tasks) {
  const results = []; let next = 0;
  await Promise.all(Array.from({ length: jobs }, async () => { while (next < tasks.length) { const i = next++; results[i] = await tasks[i](); } }));
  return results;
}
const results = await pool([0, 1, 2, 3, 4, 5, 6, 7].map((m) => () => run(m, {}, `m${m}`)).concat([3, 7].map((m) => () => run(m, { SKY_GROUND_OLD_DISTRICTS: '1' }, `control${m}`))));
let bad = 0;
for (const r of results.slice(0, 8)) {
  console.log(`mission ${r.mission} ${NAMES[r.mission]}: ${r.units} props, ${r.floating} floating${r.floating ? ` (worst ${(r.worst / 1000).toFixed(2)} m at ${r.at.map((v) => v.toFixed(0)).join(' ')})` : ''}; ${r.surfaces} draped surfaces, ${r.surfaceFloating} lifted`);
  if (r.units < 50 || r.floating || r.surfaceFloating) { bad++; for (const l of r.lines.slice(0, 12)) console.log(`    ${l}`); }
}
for (const c of results.slice(8)) {
  console.log(`control (${NAMES[c.mission]}, the old district placement): ${c.floating} floating, worst ${(c.worst / 1000).toFixed(2)} m`);
  if (!c.floating || c.worst < 1000) { console.error('FAIL: the control did not fail'); process.exit(1); }
}
if (bad) { console.error('FAIL: something floats'); process.exit(1); }
console.log('ok   skyfighter-grounding');
