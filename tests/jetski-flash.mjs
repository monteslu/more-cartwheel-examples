// No strobing in the race's busiest light: on the cart through romdev (WebGPU), frame by frame, a rocket start off the
// grid into a turbo at top speed on Red Reach (speed lines, the turbo's wider view, spray lit by the sun), then the
// finish (its camera swing and words) into the results. Every frame is measured against WCAG 2.3.1's general flash
// rule: the screen is cut into a 6 x 6 grid and every 2 x 2 block of it (a ninth of the screen) is followed through
// time; a transition is a change of the block's relative luminance of 0.1 or more against its last extreme with the
// darker side under 0.8, and a flash is a pair of opposing transitions. Bound: no block flashes more than twice in any
// second (WCAG allows 3; the finish camera swinging past a bright mark is 2). The squall's lightning is left to its own check.
// CONTROL, which must fail: the same frames with a quarter of the screen brightened on every other 6 frames (5 Hz).
//   [JETSKI_CRAFT=1] node tests/jetski-flash.mjs [--cart <path>]      draws the frames into out/jetski-flash, then measures
//   node tests/jetski-flash.mjs --dir <d>           measures f<n>.png frames already in d
import { mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const H = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski-flash', 'x-romdev-agent': process.env.JETSKI_AGENT || 'jetski-flash' };
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};

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
  return { w, h, ch, px: out };
}
const lin = (v) => { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
const LIN = Array.from({ length: 256 }, (_, i) => lin(i));
/** the 6 x 6 grid's mean relative luminance */
function cells(img, boost) {
  const G = 6, sum = new Float64Array(G * G), n = new Float64Array(G * G), { w, h, ch, px } = img;
  for (let y = 0; y < h; y += 4) for (let x = 0; x < w; x += 4) {
    const i = (y * w + x) * ch;
    let L = 0.2126 * LIN[px[i]] + 0.7152 * LIN[px[i + 1]] + 0.0722 * LIN[px[i + 2]];
    if (boost && x < w / 2 && y < h / 2) L = Math.min(1, L + 0.35);
    const k = Math.min(G - 1, Math.floor((y * G) / h)) * G + Math.min(G - 1, Math.floor((x * G) / w));
    sum[k] += L; n[k]++;
  }
  return Array.from(sum, (v, k) => v / n[k]);
}
/** the most flashes any 2 x 2 block makes in any one second */
function worst(series) {
  let most = 0;
  for (let by = 0; by < 5; by++) for (let bx = 0; bx < 5; bx++) {
    const L = series.map((c) => (c[by * 6 + bx] + c[by * 6 + bx + 1] + c[(by + 1) * 6 + bx] + c[(by + 1) * 6 + bx + 1]) / 4);
    const trans = []; let ext = L[0], dir = 0;
    for (let f = 1; f < L.length; f++) {
      const d = L[f] - ext, dark = Math.min(L[f], ext);
      if (Math.abs(d) >= 0.1 && dark < 0.8 && Math.sign(d) !== dir) { trans.push({ f, s: Math.sign(d) }); dir = Math.sign(d); ext = L[f]; }
      else if ((dir > 0 && L[f] > ext) || (dir < 0 && L[f] < ext)) ext = L[f]; /* still going the same way: the extreme moves on */
    }
    const flashes = []; for (let i = 1; i < trans.length; i++) if (trans[i].s !== trans[i - 1].s) { flashes.push(trans[i].f); i++; }
    for (const f of flashes) most = Math.max(most, flashes.filter((g) => g >= f && g < f + 60).length);
  }
  return most;
}

let dir = arg('--dir', null);
if (!dir) {
  dir = join(ROOT, 'out/jetski-flash');
  mkdirSync(dir, { recursive: true });
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power' });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 30 });
  /* JETSKI_CRAFT=1: the whole field on stand-ups */
  for (const c of ['course 2', `craft ${process.env.JETSKI_CRAFT || 0}`, 'no_dq 1', 'race']) await tool('wasm', { op: 'command', text: c });
  let n = 0;
  const got = [];
  /* each frame is measured as it comes and only every 30th picture is kept (630 full frames would be about 2 GB) */
  const shots = async (count) => { for (let k = 0; k < count; k++, n++) { const p = join(dir, `f${n}.png`); await tool('frame', { op: 'stepAndShot', frames: 1, path: p }); got.push(cells(png(p), false)); if (n % 30) rmSync(p); } };
  await tool('frame', { op: 'step', frames: 180 });                 /* the countdown's first beats */
  await tool('input', { op: 'set', ports: [{ south: true }] });     /* the last beat: a rocket start */
  await shots(150);
  for (const c of ['sw meter 5', 'sw stock 2', 'sw turbo 3']) await tool('wasm', { op: 'command', text: c });
  await shots(150);                                                  /* the turbo at top speed */
  await tool('wasm', { op: 'command', text: 'finish' });
  await shots(330);                                                  /* the finish, into the results */
  await tool('input', { op: 'set', ports: [{}] });
  await tool('host', { op: 'shutdown' });
  writeFileSync(join(dir, 'cells.json'), JSON.stringify(got));
}
/* the grids of the frames (cells.json, written by the capture), or of every f<n>.png in a --dir */
let real;
try { real = JSON.parse(readFileSync(join(dir, 'cells.json'), 'utf8')); }
catch { real = readdirSync(dir).filter((f) => /^f\d+\.png$/.test(f)).sort((a, b) => +a.slice(1, -4) - +b.slice(1, -4)).map((f) => cells(png(join(dir, f)), false)); }
const files = real;
/* the control: the top-left quarter (9 cells) brightened by 0.35 on every other 6 frames */
const ctl = real.map((c, i) => c.map((v, k) => (Math.floor(i / 6) % 2 === 1 && k % 6 < 3 && Math.floor(k / 6) < 3 ? Math.min(1, v + 0.35) : v)));
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const a = worst(real), b = worst(ctl);
check('rocket start, turbo and finish: no block flashes more than twice a second', files.length >= 600 && a <= 2, `${files.length} frames, at most ${a} flashes a second in any block`);
check('control: a quarter of the screen blinking at 5 Hz is caught', b > 3, `at most ${b} flashes a second`);
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
