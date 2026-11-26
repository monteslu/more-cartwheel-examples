// The Battle map's checks (FARMLANDS, arena 3), each with a control that must fail:
//   grounding   every solid prop stands on the ground and every item's nominal height is on the floor under it
//               (the game's own check: props.floating, items.gap_mm); control ARENA_PROP_LIFT=1 lifts the first prop
//               and item 0.5 m and must be seen
//   navigation  every command post reaches every other over the game's navmesh (arenafps_navcheck, built from the
//               game's arena code); control NAVCHECK_SEAL=0 walls the first post in and must cut its four pairs
//   flow        tools/make-levels.mjs's report for the map (posts 60 to 90 m apart on their lanes, loops, no dead ends)
// Run: node games/arenafps/tools/battle-check.mjs [--build build/native]   (needs arenafps_auto and arenafps_navcheck)
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const GAME = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = resolve(GAME, '../..');
const i = process.argv.indexOf('--build');
const BUILD = resolve(ROOT, i > 0 ? process.argv[i + 1] : 'build/native');
const exeOf = (app) => {
  const j = join(BUILD, `apps/${app}.json`);
  if (!existsSync(j)) { console.error(`no ${j}: build ${app} first (scripts/build.sh ${app})`); process.exit(2); }
  return JSON.parse(readFileSync(j, 'utf8')).binary;
};
const out = join(ROOT, 'out/gate/battle-check');
mkdirSync(out, { recursive: true });
const ENV = { ...process.env, SDL_VIDEODRIVER: 'offscreen', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy', CW_WORKERS: '2', CW_WARMUP_SPREAD: '0', CW_QUALITY: 'verylow' };
for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete ENV[k];
let bad = 0;

function ground(extra) {
  const dump = join(out, `ground${extra.ARENA_PROP_LIFT ? '-lift' : ''}.json`);
  const p = spawnSync('nice', ['-n', '10', exeOf('arenafps_auto')], { env: { ...ENV, CW_FRAMES: '6', ARENA_RES: '320x180', ARENA_ID: '3', ARENA_PLAYERS: '1', CW_DEBUG_DUMP: dump, ...extra }, encoding: 'utf8', timeout: 300e3 });
  if (p.status !== 0) { console.error(`grounding: exit ${p.status}\n${(p.stderr || '').slice(-800)}`); process.exit(2); }
  const j = JSON.parse(readFileSync(dump, 'utf8'));
  if (j['game.state'] !== 1 || j['game.arena'] !== 3) { console.error('grounding: the match on FARMLANDS did not start'); process.exit(2); }
  return { props: j['props.count'], floating: j['props.floating'], gap: j['items.gap_mm'], coplanar: j['level.coplanar_left'] };
}
const g = ground({});
console.log(`grounding: ${g.props} solid props, ${g.floating} floating, largest item gap ${g.gap} mm, ${g.coplanar} coplanar pairs left`);
if (g.props < 50 || g.floating || g.gap > 30 || g.coplanar) bad++, console.error('FAIL grounding');
const gc = ground({ ARENA_PROP_LIFT: '1' });
console.log(`grounding control (first prop and item lifted 0.5 m): ${gc.floating} floating, item gap ${gc.gap} mm`);
if (!gc.floating || gc.gap <= 30) bad++, console.error('FAIL: the grounding control did not fail');

function nav(extra) {
  const p = spawnSync('nice', ['-n', '10', exeOf('arenafps_navcheck')], { env: { ...ENV, NAVCHECK_ARENA: '3', ...extra }, encoding: 'utf8', timeout: 300e3 });
  const line = (p.stdout || '').split('\n').find((l) => l.startsWith('{"arena"'));
  if (!line) { console.error(`navcheck: exit ${p.status}, no report\n${(p.stderr || '').slice(-800)}`); process.exit(2); }
  return JSON.parse(line);
}
const n = nav({});
console.log(`navigation: ${n.whole} of ${n.pairs} post pairs joined (${n.posts} posts, ${n.polys} polys, navmesh ${n.nav_ms} ms, longest walk ${n.longest_walk_m} m)`);
for (const d of n.pairs_detail) console.log(`  ${d.ok ? 'ok  ' : 'FAIL'} ${d.a} - ${d.b}: ${d.walk_m} m, ${d.points} waypoints`);
if (n.posts !== 5 || n.whole !== n.pairs) bad++, console.error('FAIL navigation');
const nc = nav({ NAVCHECK_SEAL: '0' });
const cut = nc.pairs_detail.filter((d) => !d.ok);
console.log(`navigation control (post ${n.pairs_detail[0]?.a} walled in): ${nc.whole} of ${nc.pairs} joined, cut: ${cut.map((d) => d.a + '-' + d.b).join(' ')}`);
if (cut.length < 4 || nc.whole === nc.pairs) bad++, console.error('FAIL: the navigation control did not fail');

const { battle } = await import('./battle-map.mjs');
const fr = battle(join(GAME, 'levels'), false);
console.log(`flow: ${JSON.stringify(fr)}`);
if (!fr || !fr.pass) bad++, console.error('FAIL flow');

if (bad) { console.error(`FAIL battle-check (${bad})`); process.exit(1); }
console.log('ok   battle-check');
