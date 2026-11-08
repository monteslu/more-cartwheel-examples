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

// the music: one loop per course. Crater Atoll: an evening loop in D minor, 104 beats a minute, 16 bars: a warm pad, a plucked figure, soft bass and brushes
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


// ---- the race's other sounds (added after the first set, so the sounds above come out the same) ----
// wind past the rider's helmet: bright filtered noise with gusts on whole numbers of cycles, so the loop has no seam
{
  const n = SR * 4, L = new Float32Array(n), N = new Float32Array(n + 4000);
  for (let i = 0; i < N.length; i++) N[i] = rnd();
  let a = 0, b = 0, c = 0;
  const raw = new Float32Array(N.length);
  for (let i = 0; i < N.length; i++) { a += 0.5 * (N[i] - a); b += 0.08 * (a - b); c += 0.01 * (b - c); raw[i] = (a - b) * 0.6 + (b - c) * 0.5; }
  for (let i = 0; i < n; i++) {
    const t = i / n, gust = 0.7 + 0.2 * Math.sin(TAU * t * 2) + 0.1 * Math.sin(TAU * t * 5 + 1);
    const x = i < 4000 ? raw[i] * (i / 4000) + raw[i + n] * (1 - i / 4000) : raw[i];
    L[i] = x * gust;
  }
  wav('wind', L);
}
// the hull slapping chop: a short dull thump with a wet click on top
{
  const n = Math.round(SR * 0.22), L = new Float32Array(n);
  let lo = 0;
  for (let i = 0; i < n; i++) { const t = i / SR; lo += 0.3 * (rnd() - lo); L[i] = Math.sin(TAU * (95 - 160 * t) * t) * Math.exp(-t * 28) * 0.8 + lo * Math.exp(-t * 45) * 0.6; }
  wav('slap', L);
}
// a whoosh for a trick and an overtake: noise swept up through a moving band
function whoosh(name, dur, from, to, g) {
  const n = Math.round(SR * dur), L = new Float32Array(n);
  let a = 0, b = 0;
  for (let i = 0; i < n; i++) { const u = i / n, k = from + (to - from) * u; a += k * (rnd() - a); b += k * 0.15 * (a - b); L[i] = (a - b) * Math.sin(Math.PI * u) * g; }
  wav(name, L);
}
whoosh('whoosh', 0.45, 0.05, 0.5, 1.4);
whoosh('pass_by', 0.6, 0.3, 0.06, 1.2);
// stings: a bright chime voice, notes laid one after another
const chime = (name, notes, step, ring, gain = 0.5) => {
  const n = Math.round(SR * (step * notes.length + ring)), L = new Float32Array(n);
  notes.forEach((f, k) => { const at = Math.round(k * step * SR); for (let i = 0; at + i < n; i++) { const t = i / SR, a = Math.exp(-t * 3.2) * (1 - Math.exp(-t * 400)); L[at + i] += a * gain * (Math.sin(TAU * f * t) + 0.35 * Math.sin(TAU * f * 2 * t) * Math.exp(-t * 6) + 0.12 * Math.sin(TAU * f * 3.01 * t) * Math.exp(-t * 9)); } });
  wav(name, L);
};
const hz = (m) => 440 * Math.pow(2, (m - 69) / 12);
chime('lap', [72, 76, 79].map(hz), 0.09, 0.7);
chime('final_lap', [72, 76, 79, 84, 79, 84].map(hz), 0.085, 1.0);
chime('style', [84, 88].map(hz), 0.05, 0.25, 0.3);
// the finish: a held major chord that blooms, and a falling pair for a finish out of the top three
{
  const n = SR * 3, L = new Float32Array(n);
  [60, 64, 67, 72, 76].forEach((m, k) => { const f = hz(m), at = Math.round(k * 0.06 * SR); for (let i = 0; at + i < n; i++) { const t = i / SR, a = Math.min(1, t * 20) * Math.exp(-t * 0.9); L[at + i] += a * 0.22 * (Math.sin(TAU * f * t) + 0.3 * Math.sin(TAU * f * 2.002 * t) + 0.1 * Math.sin(TAU * f * 3 * t)); } });
  wav('finish', L);
}
chime('finish_low', [67, 64, 60].map(hz), 0.16, 1.2, 0.4);
// the menu: a soft tick to move, a pair up to accept, a pair down to go back
tone('menu_move', [1567.98], 0.05, 40);
tone('menu_ok', [1046.5, 1567.98], 0.06, 18);
tone('menu_back', [1174.66, 783.99], 0.06, 18);

// one loop per course, from a recipe: tempo, chords, the feel of each part
function song(name, { bpm, bars, chords, pad, bass, pluck, drums, swing = 0, bright = 1 }) {
  const beat = 60 / bpm, n = Math.round(SR * beat * 4 * bars), L = new Float32Array(n), R = new Float32Array(n);
  const note = hz;
  const add = (at, dur, fn, pan = 0) => { const a = Math.round(at * SR), d = Math.round(dur * SR); for (let i = 0; i < d; i++) { const v = fn(i / SR, i / d), k = (a + i) % n; L[k] += v * (1 - pan * 0.5); R[k] += v * (1 + pan * 0.5); } };
  for (let b = 0; b < bars; b++) {
    const c = chords[b % chords.length], t0 = b * 4 * beat;
    if (pad) c.forEach((m, j) => add(t0, 4 * beat * 1.12, (t, u) => { const f = note(m + 12 * pad.oct); const env = Math.sin(Math.PI * Math.min(u * 1.05, 1)) * pad.g; let v = Math.sin(TAU * f * t) + Math.sin(TAU * f * 1.005 * t) * 0.8; if (pad.saw) for (let h = 2; h <= 6; h++) v += Math.sin(TAU * f * h * t) / h * pad.saw; return env * v; }, j % 2 ? 0.5 : -0.5));
    if (bass) bass.at.forEach((q) => add(t0 + q * beat, beat * bass.len, (t, u) => Math.pow(1 - u, 1.3) * bass.g * Math.sin(TAU * note(c[0] - 12) * t + Math.sin(TAU * note(c[0]) * t) * bass.fm)));
    if (pluck && b >= pluck.from) pluck.fig(b).forEach((k, i) => { if (k < 0) return; const m = c[k % c.length] + 12 * pluck.oct + (k >= c.length ? 12 : 0); add(t0 + (i + (i % 2 ? swing : 0)) * beat * pluck.step, beat * 0.9, (t) => Math.exp(-t * pluck.decay) * pluck.g * (Math.sin(TAU * note(m) * t) + 0.4 * bright * Math.sin(TAU * note(m) * 2 * t) * Math.exp(-t * 14)), i % 2 ? 0.6 : -0.6); });
    if (drums && b >= drums.from) for (let q = 0; q < 16; q++) {
      const at = t0 + (q + (q % 2 ? swing : 0)) * beat * 0.25;
      if (drums.kick.includes(q)) add(at, 0.24, (t, u) => Math.sin(TAU * (46 + 70 * Math.pow(1 - u, 6)) * t) * Math.pow(1 - u, 2) * drums.kg);
      if (drums.snare.includes(q)) add(at, 0.16, (t, u) => (rnd() * 0.8 + Math.sin(TAU * 190 * t) * 0.4) * Math.pow(1 - u, 3) * drums.sg, 0.1);
      if (drums.hat.includes(q)) add(at, 0.05, (t, u) => rnd() * Math.pow(1 - u, 4) * drums.hg, 0.35);
    }
  }
  wav(name, L, R);
}
// Red Reach: noon in the canyon, a bright driving loop in A mixolydian, 118 beats a minute
song('music_canyon', { bpm: 118, bars: 16, chords: [[57, 61, 64, 69], [55, 59, 62, 67], [62, 66, 69, 74], [57, 61, 64, 69]],
  pad: { oct: 0, g: 0.05, saw: 0.3 }, bass: { at: [0, 0.75, 1.5, 2.5, 3], len: 0.6, g: 0.3, fm: 0.8 },
  pluck: { from: 2, oct: 1, step: 0.5, decay: 8, g: 0.1, fig: (b) => b % 2 ? [0, 2, 1, 3, 2, 4, 3, 2] : [0, 1, 2, 4, 2, 1, 3, 1] },
  drums: { from: 4, kick: [0, 6, 8, 11], snare: [4, 12], hat: [0, 2, 4, 6, 8, 10, 12, 14, 15], kg: 0.32, sg: 0.12, hg: 0.05 }, swing: 0.08 });
// Iron Quay: a night port, low and pulsing in E minor, 96 beats a minute
song('music_harbour', { bpm: 96, bars: 16, chords: [[52, 55, 59, 62], [48, 52, 55, 59], [50, 54, 57, 62], [47, 50, 54, 59]],
  pad: { oct: 0, g: 0.07, saw: 0.5 }, bass: { at: [0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5], len: 0.45, g: 0.22, fm: 0.4 },
  pluck: { from: 4, oct: 1, step: 0.25, decay: 12, g: 0.06, fig: () => [0, -1, 2, -1, 1, -1, 3, -1, 0, -1, 2, 4, -1, 1, -1, -1] },
  drums: { from: 8, kick: [0, 8, 10], snare: [4, 12], hat: [2, 6, 10, 14], kg: 0.34, sg: 0.07, hg: 0.035 }, bright: 0.5 });
// Gale Bank: a squall, fast and urgent in C minor, 132 beats a minute
song('music_storm', { bpm: 132, bars: 16, chords: [[48, 51, 55, 60], [44, 48, 51, 56], [46, 50, 53, 58], [43, 47, 50, 55]],
  pad: { oct: 0, g: 0.055, saw: 0.7 }, bass: { at: [0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5], len: 0.4, g: 0.26, fm: 1.2 },
  pluck: { from: 2, oct: 1, step: 0.25, decay: 10, g: 0.07, fig: (b) => b % 2 ? [0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 4, 3, 2, 1, 2, 3] : [3, 2, 1, 0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 3, 2] },
  drums: { from: 4, kick: [0, 3, 6, 8, 11, 14], snare: [4, 12], hat: [0, 2, 4, 6, 8, 10, 12, 14], kg: 0.34, sg: 0.13, hg: 0.05 } });
// Cold Sound: a grey morning in the fjord, open and slow in G, 90 beats a minute
song('music_sound', { bpm: 90, bars: 12, chords: [[55, 59, 62, 69], [52, 55, 59, 66], [48, 52, 55, 62], [50, 54, 57, 64]],
  pad: { oct: 0, g: 0.08 }, bass: { at: [0, 2.5], len: 1.6, g: 0.24, fm: 0.3 },
  pluck: { from: 2, oct: 2, step: 0.5, decay: 4, g: 0.07, fig: (b) => b % 3 ? [0, -1, 2, -1, 3, -1, 1, -1] : [3, -1, 2, -1, 0, -1, -1, 1] },
  drums: { from: 6, kick: [0, 10], snare: [], hat: [4, 12], kg: 0.24, sg: 0, hg: 0.03 } });
