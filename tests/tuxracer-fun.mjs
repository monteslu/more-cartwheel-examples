// tuxracer's remake additions beside the faithful race, each against a control that must fail (native runs):
//   ghost      the recording decoded at every tick sits within 1 cm of the path it was recorded from
//              (control: the same check one tick late must miss by more)
//   ghost-show a kept run is drawn beside him in the next Practice race (control: the Ghost option off)
//   camera     the speed camera opens its field of view with speed (control: Classic stays at 60 degrees)
//   finish     the finish moment fires once, with a medal (control: a run that never reaches the line)
//   spray      the spray stays below the horizon: pixels above it that change with the spray hidden
//              (control: the spray as it was, TUXRACER_SPRAY_V0, puts plumes up there)
//   node tests/tuxracer-fun.mjs [--gpu gl|webgpu] [--out DIR]
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readImage } from '../deps/cartwheel-engine/tools/lib/png.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const GPU = arg('--gpu', 'webgpu');
const OUT = resolve(arg('--out', join(ROOT, 'out/gate/tuxracer-fun')));
mkdirSync(OUT, { recursive: true });
const INPUT = join(ROOT, 'tests/tuxracer-bunny-run.json');

function run(name, frames, cmds, { input = INPUT, env = {} } = {}) {
  const shot = join(OUT, name + '.png'), state = join(OUT, name + '.json');
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), '--host', 'native', '--app', 'tuxracer', '--frames', String(frames),
    '--gpu', GPU, '--start-cmds', cmds.join(';'), ...(input ? ['--input', input] : []), '--shot', shot, '--state', state],
    { encoding: 'utf8', env: { ...process.env, CW_PROJECT_ROOT: ROOT, ...env } });
  if (r.status !== 0) throw new Error(`${name}: run failed\n${r.stdout}\n${r.stderr}`);
  return { s: JSON.parse(readFileSync(state, 'utf8')), shot };
}
const results = [];
const check = (name, ok, detail) => { results.push({ name, ok, detail }); console.log(`${ok ? 'PASS' : 'FAIL'} ${name}: ${detail}`); };

// ghost: decode against the recorded path (0.1 mm units)
{
  const a = run('ghost-check', 431, ['race bunny_hill', '@430 ghost check']).s.ghost;
  const c = run('ghost-check-control', 431, ['race bunny_hill', '@430 ghost check 1']).s.ghost;
  check('ghost', a[2] <= 100 && a[3] >= 300, `worst ${(a[2] / 10).toFixed(1)} mm over ${a[3]} ticks (limit 10 mm)`);
  check('ghost control', c[2] > 100, `one tick late: worst ${(c[2] / 10).toFixed(1)} mm (must exceed 10 mm)`);
}
// ghost-show: keep the run, race again with no input: the ghost is drawn
{
  const cmds = ['race bunny_hill', '@500 ghost keep', '@502 race bunny_hill'];
  const a = run('ghost-show', 950, cmds).s.ghost;
  const c = run('ghost-show-control', 950, ['opt ghost off', ...cmds]).s.ghost;
  check('ghost-show', a[0] === 1 && a[1] > 100, `shown ${a[0]}, ${a[1]} samples`);
  check('ghost-show control', c[0] === 0, `Ghost off: shown ${c[0]}`);
}
// camera: field of view over the bunny run (0.01 degree)
{
  const a = run('camera-speed', 430, ['race bunny_hill']).s['cam.fov'];
  const c = run('camera-classic', 430, ['opt camera classic', 'race bunny_hill']).s['cam.fov'];
  check('camera', a[2] - a[1] >= 600, `speed camera ${a[1] / 100} to ${a[2] / 100} degrees`);
  check('camera control', c[2] - c[1] === 0 && c[2] === 6000, `classic ${c[1] / 100} to ${c[2] / 100} degrees`);
}
// finish: 12 m before the line at 3 s; the moment fires once and gives a medal
{
  const a = run('finish', 420, ['race bunny_hill', '@200 tp finish 12'], { input: join(ROOT, 'tests/tuxracer-bunny-run.json') }).s;
  const c = run('finish-control', 420, ['race bunny_hill']).s;
  check('finish', a['race.finish_events'] === 1 && a['race.medal'] === 3 && a['game.state'] === 8, `events ${a['race.finish_events']}, medal ${a['race.medal']}, state ${a['game.state']}`);
  check('finish control', c['race.finish_events'] === 0 && c['race.medal'] === 0, `events ${c['race.finish_events']}, medal ${c['race.medal']}`);
}
// spray above the horizon: a frame with the spray and the same frame without
function above(a, b) {
  const A = readImage(a), B = readImage(b), lim = Math.floor(A.h * 0.42); // the horizon sits near 0.6 of the height: well above it
  let n = 0;
  for (let y = Math.floor(A.h * 0.16); y < lim; y++) for (let x = 0; x < A.w; x++) {
    const i = (y * A.w + x) * 4;
    if (Math.abs(A.rgba[i] - B.rgba[i]) + Math.abs(A.rgba[i + 1] - B.rgba[i + 1]) + Math.abs(A.rgba[i + 2] - B.rgba[i + 2]) > 24) n++;
  }
  return n;
}
{
  const f = 250, at = ['race bunny_hill'];
  const now = above(run('spray', f, at).shot, run('spray-hidden', f, [...at, 'hide spray']).shot);
  const v0 = { env: { TUXRACER_SPRAY_V0: '1' } };
  const old = above(run('spray-v0', f, at, v0).shot, run('spray-v0-hidden', f, [...at, 'hide spray'], v0).shot);
  check('spray', now <= 200, `${now} pixels above the horizon change with the spray`);
  check('spray control', old > 2000, `the old spray: ${old} pixels`);
}
const bad = results.filter((r) => !r.ok);
console.log(bad.length ? `FAIL ${bad.length} of ${results.length}` : `PASS all ${results.length}`);
process.exit(bad.length ? 1 : 0);
