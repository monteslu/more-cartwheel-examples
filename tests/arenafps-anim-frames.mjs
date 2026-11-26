// arenafps: frame sequences of the soldiers to look at (stills cannot show motion: each sequence is every 4th frame of
// two to four seconds, laid out as a contact sheet, with an mp4 of the whole stretch). On romdev, the autopilot cart (the
// bots and player 1's stand-in play the match; its key-art camera follows a fighter from a spot in its own frame):
//   spectator   a bot from its side, 3.5 m off, through the Terraces (strafes, aim, stairs)
//   chase       a bot from behind and above (locomotion, turns, jumps)
//   first       player 1's own view in a match with bots (the enemies as a player sees them)
//   split       four players' split screen, team deathmatch with bots
//   node tests/arenafps-anim-frames.mjs [--cart build/cart-wgpu/carts/arenafps_auto.wasc] [--gpu webgpu|gl] [--out out/arenafps-anim-frames]
//        [--only spectator,chase,first,split]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { writeFileSync, mkdirSync, existsSync, unlinkSync, statSync, openSync, readSync, closeSync, readdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { deflateSync } from 'node:zlib';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const SERVER = process.env.ROMDEV_URL || ROMDEV_URL;
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/arenafps_auto.wasc')));
const GPU = arg('--gpu', 'webgpu');
const OUT = resolve(arg('--out', join(ROOT, 'out/arenafps-anim-frames')));
const ONLY = arg('--only', 'spectator,chase,first,split').split(',');
const HDR = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.ARENA_SESSION || 'arenafps-anim-frames', 'x-romdev-agent': 'arenafps-anim-frames' };
const bye = async () => { try { await fetch(`${SERVER}/tool/host`, { method: 'POST', headers: HDR, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {} };
const tool = async (name, a) => {
  const r = await fetch(`${SERVER}/tool/${name}`, { method: 'POST', headers: HDR, body: JSON.stringify(a), signal: AbortSignal.timeout(1800000) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(); process.exit(3); }
  return j;
};
const cmd = async (text) => (await tool('wasm', { op: 'command', text })).reply;
mkdirSync(OUT, { recursive: true });

// a PNG of rgb24 pixels (no ffmpeg on every lane: the frames and the sheet are written here)
const CRC = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc32 = (b) => { let c = -1; for (const x of b) c = CRC[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
const chunk = (t, d) => { const l = Buffer.alloc(4), c = Buffer.alloc(4), td = Buffer.concat([Buffer.from(t), d]); l.writeUInt32BE(d.length); c.writeUInt32BE(crc32(td)); return Buffer.concat([l, td, c]); };
function png(file, rgb, w, h) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 2;
  writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', deflateSync(raw, { level: 6 })), chunk('IEND', Buffer.alloc(0))]));
}
const HAVE_FFMPEG = spawnSync('bash', ['-c', 'command -v ffmpeg']).status === 0;

// a dump of n frames: every 4th as a png, a contact sheet of them (a third of the size, 5 by 6), an mp4 of all where
// the lane has ffmpeg
async function seq(name, frames) {
  const dump = join(OUT, `${name}.cwfd`), dir = join(OUT, name);
  mkdirSync(dir, { recursive: true });
  for (const f of readdirSync(dir)) unlinkSync(join(dir, f));
  await tool('frame', { op: 'record', frames, path: dump, downscale: 1, maxSeconds: 1200 });
  const fd = openSync(dump, 'r'), hd = Buffer.alloc(16);
  readSync(fd, hd, 0, 16, 0);
  closeSync(fd);
  const w = hd.readUInt32LE(4), h = hd.readUInt32LE(8);
  const sh = (c) => { const r = spawnSync('bash', ['-c', c]); if (r.status) throw new Error(`${c}: ${r.stderr}`); };
  if (HAVE_FFMPEG) sh(`tail -c +17 "${dump}" | ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s ${w}x${h} -r 60 -i - -c:v libx264 -crf 20 -pix_fmt yuv420p "${join(OUT, name + '.mp4')}"`);
  const fs = w * h * 3, sw = Math.floor(w / 3), shh = Math.floor(h / 3), sheet = Buffer.alloc(sw * 5 * shh * 6 * 3);
  const rd = openSync(dump, 'r'), buf = Buffer.alloc(fs);
  let n = 0, k = 0;
  for (; readSync(rd, buf, 0, fs, 16 + n * fs) === fs; n++) {
    if (n % 4) continue;
    png(join(dir, `f-${String(++k).padStart(3, '0')}.png`), buf, w, h);
    if (k > 30) continue;
    const ox = ((k - 1) % 5) * sw, oy = Math.floor((k - 1) / 5) * shh;
    for (let y = 0; y < shh; y++) for (let x = 0; x < sw; x++) for (let c = 0; c < 3; c++) {
      let t = 0;
      for (let j = 0; j < 3; j++) for (let i = 0; i < 3; i++) t += buf[((y * 3 + j) * w + x * 3 + i) * 3 + c];
      sheet[((oy + y) * sw * 5 + ox + x) * 3 + c] = Math.round(t / 9);
    }
  }
  closeSync(rd);
  png(join(OUT, name + '-sheet.png'), sheet, sw * 5, shh * 6);
  unlinkSync(dump);
  console.log(`${name}: ${frames} frames ${w}x${h} -> ${HAVE_FFMPEG ? name + '.mp4, ' : ''}${name}-sheet.png, ${name}/`);
}

try {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GPU === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); process.exit(3); }
  await tool('frame', { op: 'step', frames: 240 });
  if (ONLY.includes('spectator') || ONLY.includes('chase')) {
    await cmd('match 2 0 5 1');
    await tool('frame', { op: 'step', frames: 600 });
    if (ONLY.includes('spectator')) {
      await cmd('keyart 2,0,3.5,1.3,1.0 0'); await tool('frame', { op: 'step', frames: 2 });
      await seq('spectator', 180);
      await tool('frame', { op: 'step', frames: 300 });
      await seq('spectator2', 180);
    }
    if (ONLY.includes('chase')) {
      await cmd('keyart 3,-4,0.8,2.0,1.2 0'); await tool('frame', { op: 'step', frames: 2 });
      await seq('chase', 180);
    }
    await cmd('keyart off');
  }
  if (ONLY.includes('first')) {
    await cmd('match 0 0 6 1');
    await tool('frame', { op: 'step', frames: 900 });
    await seq('first', 240);
  }
  if (ONLY.includes('split')) {
    await cmd('match 1 1 6 4');
    await tool('frame', { op: 'step', frames: 900 });
    await seq('split', 180);
  }
} finally { await bye(); }
