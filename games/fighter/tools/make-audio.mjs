// Regenerates the fighter's audio, all synthesized here (no third-party content):
// hit, block, whoosh, fireball, KO and super sounds as WAV, and one music loop per
// stage from a small step sequencer (Ogg Vorbis through ffmpeg).
//   node tools/make-audio.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const D = join(HERE, '..', 'assets', 'audio');
const TMP = join(HERE, '..', '..', '..', 'out', 'fighter-audio');
mkdirSync(D, { recursive: true });
mkdirSync(TMP, { recursive: true });
const SR = 44100, TAU = 2 * Math.PI;
let seed = 0xf17;
const noise = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return ((seed >>> 8) / 8388608) - 1; };

function wavBuf(a) {
  const n = a.length, b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVE', 8); b.write('fmt ', 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(SR, 24);
  b.writeUInt32LE(SR * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(Math.max(-1, Math.min(1, a[i])) * 32767), 44 + i * 2);
  return b;
}
const sfx = (name, secs, fn) => {
  const a = new Float32Array(Math.floor(secs * SR));
  for (let i = 0; i < a.length; i++) a[i] = fn(i / SR, i);
  writeFileSync(join(D, name + '.wav'), wavBuf(a));
};
const lp = (k) => { let y = 0; return (x) => (y += k * (x - y)); };
const env = (t, a, d) => (t < a ? t / a : Math.exp(-(t - a) / d));

for (const [name, body, len, click] of [['hit_l', 150, 0.22, 0.6], ['hit_m', 110, 0.3, 0.8], ['hit_h', 70, 0.5, 1.0]]) {
  const f = lp(0.2);
  sfx(name, len, (t) => {
    const thump = Math.sin(TAU * (body * (1 + 1.5 * Math.exp(-t * 30))) * t) * env(t, 0.001, 0.07);
    const crack = f(noise()) * env(t, 0.0005, 0.03) * click * 1.6;
    return Math.tanh((thump * 1.1 + crack) * 1.4) * 0.9;
  });
}
{ const f = lp(0.5); sfx('block', 0.2, (t) => (Math.sin(TAU * 520 * t) * 0.3 + f(noise()) * 0.7) * env(t, 0.0005, 0.04) * 0.9); }
for (const [name, len, f0, f1] of [['whoosh_l', 0.22, 700, 1500], ['whoosh_h', 0.4, 350, 1100]]) {
  let y = 0, y2 = 0;
  sfx(name, len, (t) => {
    const fc = f0 + (f1 - f0) * (t / len), k = Math.min(0.9, 2 * Math.sin(Math.PI * fc / SR));
    y += k * (noise() - y); y2 += k * (y - y2);
    return y2 * 2.2 * Math.sin(Math.PI * t / len) ** 2 * 0.7;
  });
}
{ const f = lp(0.18); sfx('fire', 0.55, (t) => (f(noise()) * 1.2 * env(t, 0.02, 0.18) + Math.sin(TAU * (180 + 260 * t) * t) * 0.3 * env(t, 0.01, 0.2)) * 0.8); }
{ const f = lp(0.1); sfx('ko', 1.4, (t) => Math.tanh((Math.sin(TAU * (55 * (1 + 1.2 * Math.exp(-t * 5))) * t) * 1.6 + f(noise()) * 1.4) * env(t, 0.002, 0.35)) * 0.95); }
{ sfx('super', 1.1, (t) => { const r = Math.sin(TAU * (120 + 900 * t * t) * t) * 0.45 * Math.min(1, t * 6); const f = noise() * 0.18 * Math.exp(-t * 2.5); return Math.tanh((r + f) * 1.5) * 0.85 * (1 - Math.max(0, t - 0.8) / 0.3); }); }
{ const f = lp(0.12); sfx('land', 0.2, (t) => f(noise()) * env(t, 0.001, 0.05) * 1.8); }
{ sfx('bell', 1.6, (t) => (Math.sin(TAU * 660 * t) + 0.5 * Math.sin(TAU * 1320 * 1.01 * t) + 0.3 * Math.sin(TAU * 1980 * t)) * env(t, 0.002, 0.5) * 0.35); }
{ sfx('round', 0.5, (t) => Math.sin(TAU * (t < 0.12 ? 440 : t < 0.24 ? 550 : 660) * t) * env(t % 0.12, 0.003, 0.06) * 0.4); }

// music: bass + chord stabs + hat, 8 bars each
function music(name, bpm, root, scale, seedv) {
  const beat = 60 / bpm, bars = 8, n = Math.floor(bars * 4 * beat * SR);
  const a = new Float32Array(n);
  let s = seedv;
  const rnd = () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
  const note = (m) => 440 * Math.pow(2, (m - 69) / 12);
  const prog = [0, 5, 3, 4, 0, 5, 7, 4];
  for (let b = 0; b < bars; b++) {
    const deg = scale[prog[b] % scale.length];
    for (let st = 0; st < 8; st++) {
      const t0 = (b * 4 + st * 0.5) * beat, i0 = Math.floor(t0 * SR);
      const bass = root - 24 + deg + (st % 4 === 3 ? 7 : 0);
      for (let i = 0; i < 0.45 * beat * SR && i0 + i < n; i++) {
        const t = i / SR, f = note(bass);
        a[i0 + i] += (Math.sign(Math.sin(TAU * f * t)) * 0.5 + Math.sin(TAU * f * t)) * 0.17 * Math.exp(-t * 6);
      }
      if (st % 2 === 0) for (const k of [0, 4, 7]) for (let i = 0; i < 0.6 * beat * SR && i0 + i < n; i++) {
        const t = i / SR; a[i0 + i] += Math.sin(TAU * note(root + deg + k) * t) * 0.045 * Math.exp(-t * 4);
      }
      let hy = 0;
      for (let i = 0; i < 0.06 * SR && i0 + i < n; i++) { const x = noise(); a[i0 + i] += (x - hy) * 0.05 * Math.exp(-i / SR * 70) * (st % 2 ? 1.0 : 0.6); hy = x; }
      if (st === 0 || st === 4) for (let i = 0; i < 0.2 * SR && i0 + i < n; i++) { const t = i / SR; a[i0 + i] += Math.sin(TAU * (50 + 90 * Math.exp(-t * 40)) * t) * 0.42 * Math.exp(-t * 14); }
      if (st === 2 || st === 6) for (let i = 0; i < 0.12 * SR && i0 + i < n; i++) { const t = i / SR; a[i0 + i] += noise() * 0.16 * Math.exp(-t * 28); }
    }
  }
  for (let i = 0; i < n; i++) a[i] = Math.tanh(a[i] * 1.6) * 0.8;
  const wp = join(TMP, name + '.wav');
  writeFileSync(wp, wavBuf(a));
  const r = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', wp, '-c:a', 'libvorbis', '-q:a', '3', join(D, name + '.ogg')]);
  if (r.status !== 0) console.error('ffmpeg failed for', name, String(r.stderr || r.error));
}
music('music_dusk', 104, 57, [0, 3, 5, 7, 10, 12, 8, 2], 11);
music('music_neon', 128, 60, [0, 3, 7, 10, 12, 5, 8, 2], 22);
music('music_dojo', 92, 62, [0, 2, 5, 7, 9, 12, 10, 4], 33);
music('music_title', 96, 57, [0, 5, 3, 7, 10, 12, 8, 2], 44);
rmSync(TMP, { recursive: true, force: true });
console.log('audio written to', D);
