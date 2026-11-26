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
import { mkdirSync, existsSync, unlinkSync, statSync, openSync, readSync, closeSync, readdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

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

// a dump of n frames: every 4th as a png, a contact sheet of them, an mp4 of all
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
  sh(`tail -c +17 "${dump}" | ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s ${w}x${h} -r 60 -i - -c:v libx264 -crf 20 -pix_fmt yuv420p "${join(OUT, name + '.mp4')}"`);
  sh(`tail -c +17 "${dump}" | ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s ${w}x${h} -r 60 -i - -vf "select=not(mod(n\\,4))" -vsync vfr "${join(dir, 'f-%03d.png')}"`);
  sh(`ffmpeg -y -loglevel error -i "${join(dir, 'f-%03d.png')}" -vf "scale=iw/3:-1,tile=5x6" -frames:v 1 "${join(OUT, name + '-sheet.png')}"`);
  unlinkSync(dump);
  console.log(`${name}: ${frames} frames ${w}x${h} -> ${name}.mp4, ${name}-sheet.png, ${name}/`);
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
