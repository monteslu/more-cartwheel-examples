// A short check of each game's dual cart: its self-playing <game>_auto cart
// runs FRAMES frames headless in wasmcart's Node host (`wasmcart play
// --frames N --shot out.png`) and must exit cleanly, report every frame, and
// leave a picture that is the cart's size and not blank (a cart that renders
// nothing, or only a clear colour, fails). CI runs it on Mesa's software
// Vulkan (lavapipe), so the cart takes its WebGPU path.
//
//   node tests/cart-smoke.mjs [--frames N] [--carts <dir>] [--out <dir>] [game ...]
//
// WASMCART (default `npx --yes wasmcart@<pinned>`) names the player command.
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, existsSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const VALUED = new Set(['--frames', '--carts', '--out']);
const FRAMES = +opt('--frames', 60);
const CARTS = resolve(opt('--carts', join(ROOT, 'build/cart-wgpu/carts')));
const OUT = resolve(opt('--out', join(ROOT, 'out/cart-smoke')));
const PLAYER = (process.env.WASMCART || 'npx --yes wasmcart@0.36.2').split(' ');
const ALL = ['arenafps', 'brawler', 'coopplatformer', 'fighter', 'hordeshooter', 'partypack'];
const games = argv.filter((a, i) => !a.startsWith('--') && !VALUED.has(argv[i - 1]));

/** the distinct colours of an RGB PNG as wasmcart writes it (8 bit, filter none on every row) */
function colours(file) {
  const b = readFileSync(file);
  let w = 0, h = 0, type = 0;
  const idat = [];
  for (let p = 8; p < b.length;) {
    const len = b.readUInt32BE(p), kind = b.toString('latin1', p + 4, p + 8), data = b.subarray(p + 8, p + 8 + len);
    if (kind === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); type = data[9]; }
    if (kind === 'IDAT') idat.push(data);
    p += 12 + len;
  }
  if (type !== 2) throw new Error(`${file}: colour type ${type}, expected RGB`);
  const raw = inflateSync(Buffer.concat(idat));
  const seen = new Set();
  for (let y = 0; y < h; y++) {
    const row = y * (1 + w * 3);
    if (raw[row] !== 0) throw new Error(`${file}: row filter ${raw[row]}, expected none`);
    for (let x = 0; x < w; x += 2) seen.add((raw[row + 1 + x * 3] << 16) | (raw[row + 2 + x * 3] << 8) | raw[row + 3 + x * 3]);
  }
  return { w, h, n: seen.size };
}

mkdirSync(OUT, { recursive: true });
let failed = 0;
for (const game of games.length ? games : ALL) {
  const cart = join(CARTS, `${game}_auto.wasc`), shot = join(OUT, `${game}.png`);
  if (!existsSync(cart)) { console.error(`${game}: no ${cart}`); failed++; continue; }
  const t0 = Date.now();
  const r = spawnSync(PLAYER[0], [...PLAYER.slice(1), 'play', cart, '--frames', String(FRAMES), '--shot', shot],
    { encoding: 'utf8', timeout: +(process.env.CW_RUN_TIMEOUT_MS || 900000), maxBuffer: 64 << 20 });
  const secs = ((Date.now() - t0) / 1000).toFixed(1);
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  const problems = [];
  if (r.error || r.status !== 0) problems.push(r.error ? r.error.message : `exited ${r.status ?? r.signal}`);
  const ran = /ran (\d+) frames\s+(\d+)x(\d+)/.exec(r.stdout || '');
  if (!ran || +ran[1] !== FRAMES) problems.push(`did not report ${FRAMES} frames`);
  if (!/render pipeline \(WebGPU\)/.test(out)) problems.push('did not start its WebGPU pipeline');
  if (/no program and are not drawn|\[error\]/.test(out)) problems.push('logged an error');
  let pic = null;
  if (existsSync(shot)) {
    pic = colours(shot);
    if (ran && (pic.w !== +ran[2] || pic.h !== +ran[3])) problems.push(`picture ${pic.w}x${pic.h}, the cart ran at ${ran[2]}x${ran[3]}`);
    if (pic.n < 1000) problems.push(`picture has only ${pic.n} colours (blank or nearly)`);
  } else problems.push('wrote no picture');
  if (problems.length) {
    failed++;
    console.error(`${game}: FAIL (${secs} s)\n  ${problems.join('\n  ')}\n${out.split('\n').filter((l) => !/\[debug\]/.test(l)).slice(-15).join('\n')}`);
  } else console.log(`${game}: ok (${FRAMES} frames, ${pic.w}x${pic.h}, ${pic.n} colours, ${secs} s)`);
}
process.exit(failed ? 1 : 0);
