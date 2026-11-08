// hordeshooter: nothing floats (a GPU suite; native GL at 1080p). Every crate, barrel, wall and pillar of the yard, and
// every solid prop of the deeper floors, has a dark band at its foot: on the camera's side, just past its footprint
// (ground.probe, on screen, from the view), the floor is darker than the same frame drawn with no contact shadows
// (HORDE_CONTACT_OFF=1; the sim and the bots do not see the switch, so the two frames differ only there). Very Low and
// High, the yard and each deeper floor's first wave, the crowd not drawn (HORDE_CROWD_OFF: it would stand over a foot).
// And nothing of the crowd is drawn sunk into a deeper floor's closed hatch, a low plate in the middle of the floor, over
// a run with the bots holding on it and the horde round them
// (ground.deck_sunk 0, ground.deck_on > 0: enemies drawn on the plate, frames x enemies).
// Controls: two runs with no contact shadows compared the same way must find no dark feet (the comparison sees the
// shadows, not run-to-run noise), and HORDE_DECK_OFF=1 (the crowd drawn at floor level) must sink some into the plate.
//   node tests/hordeshooter-grounding.mjs [--build build/native] [--gpu gl|webgpu] [--quick]
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readImage } from '../deps/cartwheel-engine/tools/lib/png.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const exe = join(resolve(ROOT, arg('--build', 'build/native')), 'games/hordeshooter/hordeshooter_auto');
const GPU = arg('--gpu', 'gl');
const OUT = join(ROOT, 'out/gate/hordeshooter-grounding');
mkdirSync(OUT, { recursive: true });

function run(tag, frames, extra) {
  const png = join(OUT, `${tag}.png`), dump = join(OUT, `${tag}.json`);
  const env = { ...process.env, CW_GPU: GPU, SDL_AUDIODRIVER: 'dummy', CW_FRAMES: String(frames), CW_WARMUP_SPREAD: '0', CW_RENDER_SCALE: '1.5',
    CW_SCREENSHOT: png, CW_DEBUG_DUMP: dump, HORDE_NOHUD: '1', ...extra };
  const p = spawnSync(exe, [], { env, encoding: 'utf8', timeout: 600e3, cwd: dirname(exe) });
  if (p.status !== 0) { console.error(`${tag}: exit ${p.status}\n${(p.stderr || '').slice(-800)}`); process.exit(2); }
  return { j: JSON.parse(readFileSync(dump, 'utf8')), img: readImage(png) };
}
const lum = (img, x, y) => { const i = 4 * (y * img.w + x); return 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2]; };
/* the mean luminance of a 9 x 6 pixel patch whose top middle is (u, v) */
const patch = (img, u, v) => {
  const cx = Math.round(u * img.w), cy = Math.round(v * img.h);
  let s = 0, n = 0;
  for (let y = cy; y < cy + 6; y++) for (let x = cx - 4; x <= cx + 4; x++) if (x >= 0 && y >= 0 && x < img.w && y < img.h) s += lum(img, x, y), n++;
  return n ? s / n : 0;
};
/* each probe's foot in run a over the same pixels in run b; a foot counts as grounded under 0.85 */
function feet(a, b) {
  const p = a.j['ground.probe'], n = a.j['ground.probes'];
  if (JSON.stringify(p.slice(0, n * 5)) !== JSON.stringify(b.j['ground.probe'].slice(0, n * 5))) { console.error('the two runs placed the probes differently'); process.exit(2); }
  const r = [];
  for (let k = 0; k < n; k++) {
    const on = patch(a.img, p[k * 5] / 1e4, p[k * 5 + 1] / 1e4), off = patch(b.img, p[k * 5] / 1e4, p[k * 5 + 1] / 1e4);
    if (off > 4) r.push(on / off);
  }
  return { n: r.length, grounded: r.filter((x) => x < 0.85).length, median: r.sort((x, y) => x - y)[r.length >> 1] ?? 1 };
}

let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const FLOORS = process.argv.includes('--quick') ? [['yard', 1]] : [['yard', 1], ['sluice', 6], ['ossuary', 11], ['foundry', 16], ['grotto', 21], ['cathedral', 27], ['rift', 33], ['core', 39]];
for (const [name, wave] of FLOORS) {
  for (const q of [0, 3]) {
    const env = { HORDE_WAVE: String(wave), HORDE_PLAYERS: '2', HORDE_QUALITY: String(q), HORDE_HOLD: '2', HORDE_CROWD_OFF: '1' }; /* the crowd not drawn: it would stand over feet */
    const on = run(`${name}-q${q}`, 150, env), off = run(`${name}-q${q}-off`, 150, { ...env, HORDE_CONTACT_OFF: '1' }), off2 = run(`${name}-q${q}-off2`, 150, { ...env, HORDE_CONTACT_OFF: '1' });
    const a = feet(on, off), c = feet(off2, off), tag = `${name} ${q ? 'High' : 'Very Low'}`;
    check(`${tag}: every foot darker than with no contact shadows`, a.n >= 1 && a.grounded === a.n, `${a.grounded} of ${a.n} (median ${a.median.toFixed(2)})`);
    check(`${tag} control: two runs with none, no foot darker`, c.n === a.n && c.grounded === 0, `${c.grounded} of ${c.n} (median ${c.median.toFixed(2)})`);
  }
}
{ /* the hatch: the bots stand on it (they start there and hold), the horde comes to them */
  const env = { HORDE_WAVE: '6', HORDE_PLAYERS: '2', HORDE_QUALITY: '3', HORDE_HOLD: '2' };
  const a = run('sluice-hatch', 900, env).j, c = run('sluice-hatch-control', 900, { ...env, HORDE_DECK_OFF: '1' }).j;
  check('nothing of the crowd drawn sunk into the closed hatch', a['ground.deck_sunk'] === 0 && a['ground.deck_on'] > 100, `${a['ground.deck_sunk']} sunk, ${a['ground.deck_on']} drawn on the plate`);
  check('control: drawn at floor level, some sink into it', c['ground.deck_sunk'] > 100, `${c['ground.deck_sunk']} drawn below the plate's top`);
}
console.log(fail ? `${fail} FAILED` : 'ok   hordeshooter-grounding');
process.exit(fail ? 1 : 0);
