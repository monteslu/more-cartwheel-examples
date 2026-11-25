// The chase camera, on the CPU with a software GL: low and close in the spray. At race speed (over 25 m/s) the camera
// sits at most 4.2 m behind the craft and 1.5 m over the water under it (it was 5 m and 2 m), and it never goes under the
// water (cam.under_ticks stays 0 over the run). Control: sw cam_old 1 (the old chase) must fail the bound.
// Thick spray: in the same frame, the bright white (luminance over 0.85) behind and around the craft is at least 1.5x what
// the thin spray of before (sw spray_thick 0, the control, which fails the bound by construction) makes.
// Water on the lens: a hard landing beads drops on the camera's glass that clear within 2.5 s, and they never cover more
// than 15 % of the middle third. Control: sw lens 0 makes none.
//   node tests/jetski-camera.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { inflateSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/camera'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const EXE = process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski');
mkdirSync(OUT, { recursive: true });
function run(cmds, frames, script, shot, extra = {}) {
  const state = join(OUT, 's.json'), input = join(OUT, 'i.json');
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(frames), '--input', input, '--state', state, '--shot', join(OUT, shot), '--start-cmds', cmds, '--exe', EXE],
    { cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', CW_WGPU_ADAPTER: 'llvmpipe', SDL_AUDIODRIVER: 'dummy', CW_RUN_TIMEOUT_MS: '400000', ...extra } });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(3);
const eye = (pre) => run(`${pre}no_dq 1;race;go 300`, 240, [{ frame: 2, buttons: ['south'] }], pre ? 'cam-old.png' : 'cam.png');
const now = eye(''), old = eye('sw cam_old 1;');
const e = now['cam.eye'].map(Number), o = old['cam.eye'].map(Number);
check('at race speed the camera sits low and close', e[2] > 25 && e[0] <= 4.2 && e[1] <= 1.5 && +now['cam.under_ticks'] === 0, `${f2(e[0])} m behind, ${f2(e[1])} m over the water at ${e[2].toFixed(1)} m/s, ${now['cam.under_ticks']} ticks under`);
check('control: cam_old 1 fails the bound', !(o[0] <= 4.2 && o[1] <= 1.5), `${f2(o[0])} m behind, ${f2(o[1])} m over the water at ${o[2].toFixed(1)} m/s`);
function png(path) {
  const b = readFileSync(path), w = b.readUInt32BE(16), h = b.readUInt32BE(20), ch = b[25] === 6 ? 4 : 3, parts = [];
  for (let o = 8; o < b.length;) { const n = b.readUInt32BE(o); if (b.toString('latin1', o + 4, o + 8) === 'IDAT') parts.push(b.subarray(o + 8, o + 8 + n)); o += 12 + n; }
  const raw = inflateSync(Buffer.concat(parts)), out = Buffer.alloc(w * h * ch), s = w * ch;
  for (let y = 0; y < h; y++) {
    const f = raw[y * (s + 1)], r = y * (s + 1) + 1, d = y * s;
    for (let x = 0; x < s; x++) {
      const a = x >= ch ? out[d + x - ch] : 0, u = y ? out[d + x - s] : 0, c = y && x >= ch ? out[d + x - s - ch] : 0;
      let p = 0;
      if (f === 1) p = a; else if (f === 2) p = u; else if (f === 3) p = (a + u) >> 1;
      else if (f === 4) { const q = a + u - c, pa = Math.abs(q - a), pb = Math.abs(q - u), pc = Math.abs(q - c); p = pa <= pb && pa <= pc ? a : pb <= pc ? u : c; }
      out[d + x] = (raw[r + x] + p) & 255;
    }
  }
  return { w, h, ch, out };
}
/* bright white in the lower middle of the frame (the craft, its tail and its sheets; the HUD is the same in both) */
const bright = (path) => {
  const { w, h, ch, out } = png(path); let n = 0;
  for (let y = Math.round(h * 0.5); y < Math.round(h * 0.97); y++) for (let x = Math.round(w * 0.25); x < Math.round(w * 0.75); x++) {
    const i = (y * w + x) * ch, l = (0.2126 * out[i] + 0.7152 * out[i + 1] + 0.0722 * out[i + 2]) / 255;
    if (l > 0.85) n++;
  }
  return n;
};
const SPRAY = 'no_dq 1;race;go 300';
run(SPRAY, 240, [{ frame: 2, buttons: ['south'] }], 'spray.png');
run('sw spray_thick 0;' + SPRAY, 240, [{ frame: 2, buttons: ['south'] }], 'spray-thin.png');
const thick = bright(join(OUT, 'spray.png')), thin = bright(join(OUT, 'spray-thin.png'));
check('thick white spray: at least 1.5x the bright water of the thin spray, same frame', thick >= 1.5 * thin && thin > 0, `${thick} bright pixels against ${thin} (${(thick / Math.max(1, thin)).toFixed(2)}x)`);
const LAND = 'no_dq 1;race;go 300;drop 0.05 4 24';
const l1 = run(LAND, 90, [{ frame: 2, buttons: ['south'] }], 'lens.png'), l2 = run(LAND, 90 + 150, [{ frame: 2, buttons: ['south'] }], 'lens-after.png'), l0 = run('sw lens 0;' + LAND, 90, [{ frame: 2, buttons: ['south'] }], 'lens-off.png');
const L1 = l1.lens.map(Number), L2 = l2.lens.map(Number), L0 = l0.lens.map(Number);
check('a hard landing beads drops on the lens, clear of the middle', L1[0] >= 4 && L1[1] <= 0.15, `${L1[0]} drops, ${(100 * L1[1]).toFixed(1)} % of the middle third`);
check('the drops clear within 2.5 s', L2[0] === 0, `${L2[0]} left 2.5 s later`);
check('control: lens 0 makes none', L0[2] === 0, `${L0[2]} drops made`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
