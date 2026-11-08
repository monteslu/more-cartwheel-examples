// pirates' labels over the sea never cover each other: in a Treasure Race of four captains (three of them bots, all
// fighting from the first minute) at 1080p, every annotation box drawn over the sea (each ship's name with its
// protection timer and hull bar, each harbor's name, each hit's damage number over its ship) is counted against every
// other one each frame, and the most overlapping pairs in any frame (pirates.label_overlaps_max) must be 0 on both
// maps. Native, GL, through the GPU suite.
// Control, which must fail: sw labels_stack 1 (every ship label drawn at its ship, nothing kept clear) must overlap.
//   node tests/pirates-labels.mjs [--frames 2400]
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/pirates-labels');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const FRAMES = +arg('--frames', '2400');
const EXE = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/pirates.json'), 'utf8')).binary;
mkdirSync(OUT, { recursive: true });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

function run(tag, cmds) {
  const dump = join(OUT, `${tag}.json`), shot = join(OUT, `${tag}.png`);
  rmSync(dump, { force: true });
  const env = { ...process.env, SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', CW_FRAMES: String(FRAMES), CW_FRAME_HZ: '60', CW_WARMUP_SPREAD: '0',
    CW_HOST_PREF: '1920x1080', CW_START_CMDS: cmds.join(';'), CW_DEBUG_DUMP: dump, CW_SCREENSHOT: shot };
  const r = spawnSync(EXE, [], { env, encoding: 'utf8', timeout: 600000 });
  if (r.status !== 0) { console.error(`${tag}: exit ${r.status}\n${(r.stderr || '').slice(-1500)}`); process.exit(3); }
  const d = JSON.parse(readFileSync(dump, 'utf8'));
  const f = (n) => { const v = d[n] ?? d.fields?.[n]; return Array.isArray(v) ? v : +v; };
  return { max: f('pirates.label_overlaps_max'), shots: f('pirates.shots'), state: f('pirates.state'), map: f('pirates.map'), hits: f('pirates.hit_ticks') };
}

for (const map of [0, 1]) {
  const ok = run(`map${map}`, [`@20 pirates_map ${map}`, '@30 pirates_match 1 1']);
  check(`map ${map}: a four-captain race, no annotation covers another`, ok.state === 1 && ok.map === map && ok.shots > 30 && ok.max === 0,
    `state ${ok.state}, map ${ok.map}, ${ok.shots} shots fired, most overlapping pairs in a frame ${ok.max}`);
  const ctl = run(`map${map}-control`, [`@20 pirates_map ${map}`, '@30 pirates_match 1 1', '@31 sw labels_stack 1']);
  check(`map ${map} control: sw labels_stack 1 overlaps`, ctl.shots > 30 && ctl.max > 0, `most overlapping pairs in a frame ${ctl.max}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
