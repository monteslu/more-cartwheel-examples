// Every sound from our own code: the engine, the water under the hull, splashes, buoy signals, the countdown and a loop
// of music. Plain 16-bit WAVs into assets/audio.   node games/jetski/tools/make-audio.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const OUT = join(resolve(dirname(fileURLToPath(import.meta.url)), '..'), 'assets/audio');
mkdirSync(OUT, { recursive: true });
const SR = 32000, TAU = Math.PI * 2;
let seed = 12345;
const rnd = () => { seed = (Math.imul(seed, 1664525) + 1013904223) | 0; return ((seed >>> 8) / 16777216) * 2 - 1; };
function wav(name, L, R) {
  const ch = R ? 2 : 1, n = L.length, b = Buffer.alloc(44 + n * 2 * ch);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2 * ch, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(ch, 22);
  b.writeUInt32LE(SR, 24); b.writeUInt32LE(SR * 2 * ch, 28); b.writeUInt16LE(2 * ch, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2 * ch, 40);
  let peak = 0; for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(L[i]), R ? Math.abs(R[i]) : 0);
  const g = peak > 0.95 ? 0.95 / peak : 1;
  for (let i = 0; i < n; i++) { b.writeInt16LE(Math.round(L[i] * g * 32767), 44 + i * 2 * ch); if (R) b.writeInt16LE(Math.round(R[i] * g * 32767), 46 + i * 4); }
  writeFileSync(join(OUT, name + '.wav'), b);
}
const lp = (x, a) => { let y = 0; const o = new Float32Array(x.length); for (let pass = 0; pass < 2; pass++) for (let i = 0; i < x.length; i++) { y += a * (x[i] - y); o[i] = y; } return o; };

// the engine: a two-stroke's rasp on a whole number of cycles, so the loop has no seam. The game pitches it with speed.
{
  const f0 = 55, cycles = 110, n = Math.round(SR * cycles / f0), L = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const t = i / n * cycles * TAU;
    let s = 0;
    for (let h = 1; h <= 22; h++) s += Math.sin(t * h + h * h * 0.7) / Math.pow(h, 0.85) * (h % 2 ? 1 : 0.6);
    const firing = Math.pow(0.5 + 0.5 * Math.cos(t), 6);
    L[i] = (s * 0.14 * (0.6 + firing)) + 0.05 * Math.sin(t * 0.5);
  }
  wav('engine', L);
}
// the water under the hull: filtered noise, wrapped so it loops
{
  const n = SR * 3, L = new Float32Array(n), N = new Float32Array(n + 4000);
  for (let i = 0; i < N.length; i++) N[i] = rnd();
  let lo = 0, hi = 0;
  for (let i = 0; i < N.length; i++) { lo += 0.24 * (N[i] - lo); hi += 0.02 * (lo - hi); const v = lo - hi; if (i < n) L[i] = v * 0.7; else L[i - n] = L[i - n] * (1 - (i - n) / 4000) + v * 0.7 * ((i - n) / 4000); }
  wav('hull', L);
}
function splash(name, dur, bright, g) {
  const n = Math.round(SR * dur), L = new Float32Array(n);
  let lo = 0, lo2 = 0;
  for (let i = 0; i < n; i++) { const t = i / n, a = Math.pow(1 - t, 2.2) * (1 - Math.exp(-i / 90)); lo += bright * (rnd() - lo); lo2 += 0.02 * (lo - lo2); L[i] = (lo - lo2 * 0.6) * a * g + Math.sin(TAU * 62 * i / SR * (1 - t * 0.5)) * Math.exp(-t * 14) * 0.5 * g; }
  wav(name, L);
}
splash('splash_big', 1.3, 0.34, 1.0);
splash('splash_small', 0.55, 0.42, 0.7);
const tone = (name, notes, dur, shape = 3) => {
  const n = Math.round(SR * dur * notes.length), L = new Float32Array(n);
  notes.forEach((f, k) => { for (let i = 0; i < SR * dur * 1.8 && i + Math.round(k * dur * SR) < n; i++) { const t = i / SR, a = Math.exp(-t * shape) * (1 - Math.exp(-t * 300)); L[i + Math.round(k * dur * SR)] += a * (Math.sin(TAU * f * t) + 0.3 * Math.sin(TAU * f * 2.01 * t)) * 0.5; } });
  wav(name, L);
};
tone('buoy_ok', [880, 1318.5], 0.09, 9);
tone('buoy_top', [880, 1318.5, 1760], 0.08, 7);
tone('buoy_miss', [196, 146.8], 0.15, 9);
tone('count', [660], 0.32, 8);
tone('go', [1320], 0.7, 4);

// the music: an evening loop in D minor, 104 beats a minute, 16 bars: a warm pad, a plucked figure, soft bass and brushes
{
  const bpm = 104, beat = 60 / bpm, bars = 16, n = Math.round(SR * beat * 4 * bars), L = new Float32Array(n), R = new Float32Array(n);
  const note = (m) => 440 * Math.pow(2, (m - 69) / 12);
  const chords = [[50, 57, 60, 65], [46, 53, 58, 65], [41, 48, 57, 64], [48, 55, 60, 64]]; // Dm7 Bb F C
  const add = (at, dur, fn, pan = 0) => { const a = Math.round(at * SR), d = Math.round(dur * SR); for (let i = 0; i < d; i++) { const v = fn(i / SR, i / d), k = (a + i) % n; L[k] += v * (1 - pan * 0.5); R[k] += v * (1 + pan * 0.5); } };
  for (let b = 0; b < bars; b++) {
    const c = chords[b % 4], t0 = b * 4 * beat;
    // pad: detuned sines breathing over the bar
    c.forEach((m, j) => add(t0, 4 * beat * 1.15, (t, u) => { const f = note(m + 12); const env = Math.sin(Math.PI * Math.min(u * 1.05, 1)) * 0.075; return env * (Math.sin(TAU * f * t) + Math.sin(TAU * f * 1.004 * t) * 0.8 + Math.sin(TAU * f * 2.003 * t) * 0.18); }, j % 2 ? 0.5 : -0.5));
    // bass
    for (const q of [0, 1.5, 3]) add(t0 + q * beat, beat * 0.9, (t, u) => Math.pow(1 - u, 1.5) * 0.3 * Math.sin(TAU * note(c[0] - 12) * t + Math.sin(TAU * note(c[0]) * t) * 0.6));
    // the plucks, climbing every other bar
    const fig = b % 2 ? [3, 2, 1, 2, 3, 1, 2, 3] : [1, 2, 3, 2, 1, 3, 2, 1];
    if (b >= 4) fig.forEach((k, i) => { const m = c[k] + 24; add(t0 + i * beat * 0.5, beat * 0.9, (t) => Math.exp(-t * 6) * 0.1 * (Math.sin(TAU * note(m) * t) + 0.4 * Math.sin(TAU * note(m) * 2 * t) * Math.exp(-t * 14)), i % 2 ? 0.6 : -0.6); });
    // brushes and a soft kick after the intro
    if (b >= 8) for (let q = 0; q < 8; q++) { add(t0 + q * beat * 0.5, 0.09, (t, u) => rnd() * Math.pow(1 - u, 3) * (q % 2 ? 0.05 : 0.025), 0.3); if (q % 4 === 0) add(t0 + q * beat * 0.5, 0.22, (t, u) => Math.sin(TAU * (48 + 60 * Math.pow(1 - u, 6)) * t) * Math.pow(1 - u, 2) * 0.32); }
  }
  wav('music', L, R);
}
