// The start and finish banners and the title logo, redrawn at high resolution in the original's designs (the 0.61
// start.png / finish.png banner: a checkered flag panel at each end, the word in yellow italic capitals on dark green
// or black between them; menu_title.png: "Tux" in yellow block letters over a frosty splash, "RACER" in white italic
// capitals with a blue edge across its foot). Drawn from the game's own fonts (assets/fonts, OFL) by tools/lib/vec2d.mjs.
// Deterministic (its own RNG). Writes assets/textures/start-banner.png, finish-banner.png and logo.png.
//   node games/tuxracer/tools/make-signs.mjs
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { writePng } from '../../../deps/cartwheel-engine/tools/lib/png.mjs';
import { loadFont, textPolys, coverage, canvas, blur, grow, shift } from './lib/vec2d.mjs';

const root = resolve(dirname(new URL(import.meta.url).pathname), '..');
const out = join(root, 'assets/textures');
mkdirSync(out, { recursive: true });
const titan = loadFont(join(root, 'assets/fonts/TitanOne-Regular.ttf'));
let seed = 20261109;
const rnd = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
const lin = (r, g, b) => [r, g, b].map((v) => (v / 255) ** 2.2);
const rect = (x0, y0, x1, y1) => [[[x0, y0], [x1, y0], [x1, y1], [x0, y1]]];

/** a banner: the gate mesh's 8.2 x 1.75 m cloth (item_mesh.c), 2048 x 437 */
function banner(word, field, file) {
  const W = 2048, H = 437, c = canvas(W, H);
  const ROWS = 6, cell = H / ROWS, COLS = 5, panel = COLS * cell;
  // cloth folds: a slow light and shade across the width (it hangs from the two poles), the same on every layer
  const fold = (x) => 0.86 + 0.14 * (0.5 + 0.5 * Math.sin(x / W * Math.PI * 7.3 + 0.6)) - 0.05 * Math.abs(Math.sin(x / W * Math.PI));
  const shade = (col) => (x, y) => { const f = fold(x) * (0.93 + 0.07 * Math.sin(y / H * Math.PI)); return [col[0] * f, col[1] * f, col[2] * f, 1]; };
  // the checkered panels
  c.fill(rect(0, 0, W, H), shade(lin(236, 238, 240)));
  const dark = [];
  for (let side = 0; side < 2; side++)
    for (let r = 0; r < ROWS; r++)
      for (let k = 0; k < COLS; k++)
        if ((r + k + side) % 2 === 0) { const x0 = side ? W - panel + k * cell : k * cell; dark.push(...rect(x0, r * cell, x0 + cell, (r + 1) * cell)); }
  c.fill(dark, shade(lin(18, 18, 20)));
  // the field between them, edged by a thin dark seam where it is sewn to the panels
  c.fill(rect(panel, 0, W - panel, H), shade(field));
  c.fill([...rect(panel - 4, 0, panel + 4, H), ...rect(W - panel - 4, 0, W - panel + 4, H)], shade(lin(8, 8, 8)));
  // the word: heavy yellow italic capitals, centred, a soft dark shadow under them
  const field_w = W - 2 * panel;
  let size = H * 0.8;
  const opts0 = { shear: 0.2, squeeze: 0.86, track: H * 0.02 };
  while (textPolys(titan, word, { ...opts0, size }).width > field_w * 0.86) size *= 0.97;
  const opts = { ...opts0, size };
  const probe = textPolys(titan, word, opts);
  const x = (W - probe.width) / 2 - size * 0.05, y = H * 0.5 + size * 0.36;
  const t = textPolys(titan, word, { ...opts, x, y });
  const cov = coverage(W, H, t.polys);
  c.paint(blur(shift(cov, W, H, 9, 11), W, H, 6).map((v) => v * 0.8), [0, 0, 0, 1]);
  c.paint(grow(cov, W, H, 6), shade(lin(40, 30, 0)));
  c.paint(cov, (px, py) => { const g = 0.82 + 0.18 * (1 - py / H); const f = fold(px); return [1.0 * g * f, 0.78 * g * f, 0.02 * f, 1]; });
  writePng(join(out, file), W, H, c.bytes());
  console.log(file);
}
banner('START', lin(14, 64, 34), 'start-banner.png');
banner('FINISH', lin(12, 12, 14), 'finish-banner.png');

/** the title logo, 1024 x 512 (drawn at 384 x 192 menu units) */
function logo() {
  const W = 1024, H = 512, c = canvas(W, H);
  // the frosty splash: a ragged blob of icy blue, lighter at its heart, crystals at its edge
  const blobs = [];
  for (let i = 0; i < 26; i++) {
    const cx = W * (0.1 + 0.8 * rnd()), cy = H * (0.22 + 0.56 * rnd()), r = H * (0.1 + 0.16 * rnd()), n = 40, poly = [];
    for (let k = 0; k < n; k++) { const a = (k / n) * Math.PI * 2, rr = r * (0.75 + 0.35 * rnd()); poly.push([cx + Math.cos(a) * rr * 1.5, cy + Math.sin(a) * rr]); }
    blobs.push(poly);
  }
  const splash = blur(coverage(W, H, blobs), W, H, 5).map((v) => Math.min(1, v * 1.6));
  c.paint(splash, (x, y) => { const d = Math.hypot((x - W / 2) / W, (y - H / 2) / H); const k = Math.max(0, 1 - d * 1.8); const n = 0.92 + 0.08 * rnd(); return [(0.32 + 0.5 * k) * n, (0.52 + 0.4 * k) * n, (0.9 + 0.1 * k) * n, 0.92]; });
  const frost = [];
  for (let i = 0; i < 900; i++) { const x = rnd() * W, y = rnd() * H, s = 1 + rnd() * 3; if (splash[Math.floor(y) * W + Math.floor(x)] > 0.2) frost.push([[x, y - s], [x + s * 0.6, y], [x, y + s], [x - s * 0.6, y]]); }
  c.fill(frost, [0.95, 0.98, 1.0, 0.7]);
  // "Tux": big yellow block letters, an orange-brown depth below and right of them, a dark edge
  const tOpts = { size: H * 0.74, squeeze: 1.0, track: H * 0.0 };
  const tw = textPolys(titan, 'Tux', tOpts).width, tx = W * 0.5 - tw * 0.5 - W * 0.05, ty = H * 0.6;
  const tux = coverage(W, H, textPolys(titan, 'Tux', { ...tOpts, x: tx, y: ty }).polys);
  for (let d = 16; d >= 1; d--) c.paint(shift(tux, W, H, d, d), lin(150 - d * 3, 84 - d * 2, 8));
  c.paint(grow(tux, W, H, 5), lin(60, 36, 0));
  c.paint(tux, (x, y) => { const k = (y - ty + H * 0.5) / (H * 0.55); return lin(255, 236 - 50 * Math.max(0, Math.min(1, k)), 40 + 20 * (1 - k)).concat(1); });
  // a glint across the letters' top
  const gl = tux.map((v, i) => { const y = Math.floor(i / W), x = i % W; return v * Math.max(0, 1 - Math.abs(y - (ty - H * 0.42) - (x - W / 2) * 0.05) / (H * 0.06)); });
  c.paint(gl, [1, 1, 0.85, 0.55]);
  // "RACER": white italic capitals with a deep blue edge, across the foot of "Tux"
  const rOpts = { size: H * 0.36, shear: 0.22, squeeze: 0.92, track: H * 0.01 };
  const rw = textPolys(titan, 'RACER', rOpts).width, rx = W * 0.56 - rw * 0.5, ry = H * 0.9;
  const racer = coverage(W, H, textPolys(titan, 'RACER', { ...rOpts, x: rx, y: ry }).polys);
  c.paint(blur(shift(racer, W, H, 6, 8), W, H, 5).map((v) => v * 0.7), lin(10, 20, 60).concat(1));
  c.paint(grow(racer, W, H, 9), lin(24, 60, 160));
  c.paint(racer, (x, y) => { const k = Math.max(0, Math.min(1, (y - (ry - H * 0.28)) / (H * 0.3))); return lin(255 - 60 * k, 255 - 40 * k, 255).concat(1); });
  writePng(join(out, 'logo.png'), W, H, c.bytes());
  console.log('logo.png');
}
logo();

/** the HUD herring (the original herringicon.png: a yellow fish, nose left, forked tail right, a dark outline), 256 x 160 */
function herringIcon() {
  const W = 256, H = 160, c = canvas(W, H);
  const body = [];
  const N = 80;
  for (let k = 0; k <= N; k++) { // the top edge, nose (x 22) to tail stalk (x 178): fat behind the head
    const t = k / N, x = 22 + t * 156, half = 38 * Math.pow(Math.sin(Math.PI * Math.min(1, t * 1.05)), 0.7) * (1 - 0.55 * t * t) + 7 * t;
    body.push([x, 82 - half * (t < 0.5 ? 1.0 : 1.0) - 6 * Math.sin(Math.PI * t)]);
  }
  for (let k = N; k >= 0; k--) {
    const t = k / N, x = 22 + t * 156, half = 32 * Math.pow(Math.sin(Math.PI * Math.min(1, t * 1.05)), 0.7) * (1 - 0.55 * t * t) + 7 * t;
    body.push([x, 82 + half]);
  }
  const tail = [[170, 74], [236, 22], [218, 80], [236, 140], [170, 92]];
  const fin = [[96, 52], [116, 22], [140, 48]];
  const cov = coverage(W, H, [body, tail, fin]);
  c.paint(grow(cov, W, H, 9), lin(20, 16, 4));
  c.paint(cov, (x, y) => { const k = (y - 30) / 100; return lin(255, 236 - 40 * Math.max(0, Math.min(1, k)), 20).concat(1); });
  const eye = coverage(W, H, [Array.from({ length: 16 }, (_, i) => [50 + 6 * Math.cos(i / 16 * Math.PI * 2), 70 + 6 * Math.sin(i / 16 * Math.PI * 2)])]);
  c.paint(eye, lin(20, 16, 4));
  writePng(join(out, 'herring-icon.png'), W, H, c.bytes());
  console.log('herring-icon.png');
}
herringIcon();
