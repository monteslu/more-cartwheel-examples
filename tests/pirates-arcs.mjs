// pirates' cannonballs fly in arcs and land on the rings: in a Treasure Race of four bot captains (all fighting from
// the first minute), every ball is drawn on a parabola from the gun deck, so the highest any ball is drawn over the water
// (pirates.shot_peak) must be at least 3.5 m, and each captain's middle ball that falls in the sea must splash within
// 0.6 m of the landing ring it showed when it fired (pirates.shot_aim_error). Both maps, native, GL.
// Control, which must fail: sw flat_shots 1 (balls skim the water at 0.7 m, as they once did) must peak under 1 m.
//   node tests/pirates-arcs.mjs [--frames 2400]
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/pirates-arcs');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const FRAMES = +arg('--frames', '2400');
const EXE = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/pirates.json'), 'utf8')).binary;
mkdirSync(OUT, { recursive: true });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

function run(tag, cmds) {
  const dump = join(OUT, `${tag}.json`);
  rmSync(dump, { force: true });
  const env = { ...process.env, SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', CW_FRAMES: String(FRAMES), CW_FRAME_HZ: '60', CW_WARMUP_SPREAD: '0',
    CW_HOST_PREF: '1280x720', CW_START_CMDS: cmds.join(';'), CW_DEBUG_DUMP: dump };
  const r = spawnSync(EXE, [], { env, encoding: 'utf8', timeout: 600000 });
  if (r.status !== 0) { console.error(`${tag}: exit ${r.status}\n${(r.stderr || '').slice(-1500)}`); process.exit(3); }
  const d = JSON.parse(readFileSync(dump, 'utf8'));
  const f = (n) => +(d[n] ?? d.fields?.[n]);
  return { peak: f('pirates.shot_peak'), err: f('pirates.shot_aim_error'), shots: f('pirates.shots'), map: f('pirates.map') };
}

for (const map of [0, 1]) {
  const ok = run(`map${map}`, [`@20 pirates_map ${map}`, '@30 pirates_match 1 1']);
  check(`map ${map}: balls arc and land on their rings`, ok.map === map && ok.shots > 30 && ok.peak >= 3.5 && ok.err > 0 && ok.err <= 0.6,
    `map ${ok.map}, ${ok.shots} shots, highest ball ${ok.peak.toFixed(2)} m, farthest splash from its ring ${ok.err.toFixed(3)} m`);
  const ctl = run(`map${map}-control`, [`@20 pirates_map ${map}`, '@30 pirates_match 1 1', '@31 sw flat_shots 1']);
  check(`map ${map} control: sw flat_shots 1 peaks under 1 m`, ctl.shots > 30 && ctl.peak < 1, `highest ball ${ctl.peak.toFixed(2)} m`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
