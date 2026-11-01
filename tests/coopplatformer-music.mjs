// coopplatformer's music: each track decodes in a second, independent player (ffmpeg's libopenmpt, not the engine's), runs
// its whole form, is never silent for a second, never clips, and its restart is not a click: the last and first samples of
// the loop sit together no worse than the track's ordinary sample to sample movement. Control: a track with its notes
// removed must fail. The cart's end of it (the voice playing and its clock moving, on the music bus) is the coopplatformer-
// music-cart gate.
//   node tests/coopplatformer-music.mjs
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const DIR = join(ROOT, 'games/coopplatformer/assets/music');
const OUT = mkdtempSync(join(ROOT, 'out', 'music-'));
const RATE = 24000;

/** @returns {Float32Array} the track, mono */
function decode(path) {
  const raw = join(OUT, 'd.raw');
  execFileSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', '-i', path, '-ac', '1', '-ar', String(RATE), '-f', 'f32le', raw]);
  const b = readFileSync(raw);
  return new Float32Array(b.buffer, b.byteOffset, b.length >> 2);
}
/** what is wrong with a track, as a list of words */
function faults(pcm, name) {
  const out = [];
  const secs = pcm.length / RATE;
  if (secs < 20) out.push(`only ${secs.toFixed(1)} s`);
  let peak = 0, sum = 0, moved = 0, clipped = 0, quiet = 0, longest = 0;
  for (let i = 0; i < pcm.length; i++) {
    const v = Math.abs(pcm[i]);
    if (v > peak) peak = v;
    if (v > 0.995) clipped++;
    sum += v * v;
    if (i) moved = Math.max(moved, Math.abs(pcm[i] - pcm[i - 1]));
  }
  // windows of 50 ms: any second with nothing in it
  const win = RATE / 20;
  for (let i = 0; i + win < pcm.length; i += win) {
    let e = 0;
    for (let k = 0; k < win; k++) e += pcm[i + k] * pcm[i + k];
    if (Math.sqrt(e / win) < 0.002) quiet++, longest = Math.max(longest, quiet);
    else quiet = 0;
  }
  const rms = Math.sqrt(sum / pcm.length);
  if (rms < 0.03) out.push(`too quiet (rms ${rms.toFixed(3)})`);
  if (clipped > pcm.length * 0.0005) out.push(`clips (${clipped} samples)`);
  if (longest >= 20) out.push(`silent for ${(longest / 20).toFixed(1)} s`);
  const seam = Math.abs(pcm[0] - pcm[pcm.length - 1]);
  if (seam > Math.max(0.05, moved)) out.push(`a click at the loop (${seam.toFixed(3)})`);
  console.log(`  ${name}: ${secs.toFixed(1)} s, rms ${rms.toFixed(3)}, peak ${peak.toFixed(2)}, longest quiet ${(longest / 20).toFixed(2)} s, loop seam ${seam.toFixed(3)} (largest step ${moved.toFixed(3)})`);
  return out;
}

let bad = 0;
const tracks = readdirSync(DIR).filter((f) => f.endsWith('.xm'));
if (tracks.length < 7) { console.log(`FAIL only ${tracks.length} tracks`); bad++; }
for (const t of tracks) {
  const f = faults(decode(join(DIR, t)), t);
  if (f.length) { console.log(`FAIL ${t}: ${f.join(', ')}`); bad++; }
}
// control: the same file with every note in its patterns blanked must fail
const src = readFileSync(join(DIR, tracks[0]));
const empty = Buffer.from(src);
let at = 60 + empty.readUInt32LE(60);
const npat = empty.readUInt16LE(70);
for (let p = 0; p < npat; p++) {
  const hl = empty.readUInt32LE(at), size = empty.readUInt16LE(at + 7);
  empty.fill(0x80, at + hl, at + hl + size);
  at += hl + size;
}
const ctl = join(OUT, 'control.xm');
writeFileSync(ctl, empty);
let ctlFaults = [];
try { ctlFaults = faults(decode(ctl), 'control (no notes)'); } catch { ctlFaults = ['the player refused it']; }
if (!ctlFaults.length) { console.log('FAIL control: a track with no notes passed'); bad++; }
else console.log(`ok   control fails as it must: ${ctlFaults.join(', ')}`);
rmSync(OUT, { recursive: true, force: true });
console.log(bad ? `FAIL coopplatformer-music: ${bad}` : `ok   coopplatformer-music: ${tracks.length} tracks`);
process.exit(bad ? 1 : 0);
