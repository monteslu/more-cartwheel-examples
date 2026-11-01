// The game's music: original tracks written here, note by note, with every instrument synthesized by this script, saved
// as FastTracker 2 modules (.xm) that the engine's module player streams and loops on its own (the song restarts on a bar
// line with no seam, there is no audio file end to click at). Nothing is downloaded.
//   node games/coopplatformer/tools/gen-music.mjs      writes games/coopplatformer/assets/music/*.xm
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'music');
const RATE = 33452; // 8363 x 4: with relative note +24 an instrument plays at this rate on XM note 49, written C-4
const CYCLE = 128; // samples per cycle of a held tone: 261.3 Hz, within 3 cents of middle C, so a loop of whole cycles has no seam
const TAU = Math.PI * 2;

/** a seedable noise source, so every run writes the same bytes */
function rng(seed) { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 2147483648 - 1; }; }

/** a held tone: attack and then a looped run of whole cycles of a harmonic series (partials: [harmonic, amplitude]) */
function held(partials, attackCycles = 4, loopCycles = 16, wobble = 0) {
  const n = CYCLE * (attackCycles + loopCycles), s = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const ph = (i % (CYCLE * loopCycles)) / CYCLE; // whole cycles repeat
    let v = 0;
    for (const [h, a] of partials) v += a * Math.sin(TAU * h * ph);
    if (wobble) v *= 1 + wobble * Math.sin(TAU * ph / loopCycles * 4);
    s[i] = v * Math.min(1, i / (CYCLE * attackCycles * 0.5));
  }
  return { pcm: s, loopStart: CYCLE * attackCycles, loopLen: CYCLE * loopCycles };
}
/** a struck tone with no loop: partials decaying at their own rates */
function struck(partials, secs, click = 0, seed = 7) {
  const n = Math.round(secs * RATE), s = new Float64Array(n), r = rng(seed);
  for (let i = 0; i < n; i++) {
    const t = i / RATE;
    let v = 0;
    for (const [h, a, d] of partials) v += a * Math.exp(-t * d) * Math.sin(TAU * h * (RATE / CYCLE) * t);
    if (click && t < 0.012) v += click * r() * (1 - t / 0.012);
    s[i] = v * Math.min(1, i / 24);
  }
  return { pcm: s };
}
/** a drum: pitch sweep plus noise */
function drum({ secs, f0 = 0, f1 = 0, sweep = 30, body = 0, noise = 0, decay = 12, ndecay = 20, hp = 0, seed = 3 }) {
  const n = Math.round(secs * RATE), s = new Float64Array(n), r = rng(seed);
  let ph = 0, last = 0, lastOut = 0;
  for (let i = 0; i < n; i++) {
    const t = i / RATE, f = f1 + (f0 - f1) * Math.exp(-t * sweep);
    ph += f / RATE;
    let w = noise * Math.exp(-t * ndecay) * r();
    if (hp) { const o = hp * (lastOut + w - last); last = w; lastOut = o; w = o; }
    s[i] = body * Math.exp(-t * decay) * Math.sin(TAU * ph) + w;
  }
  return { pcm: s };
}

// The band: index + 1 is the XM instrument. Every track picks from this one set.
const saw = (k, tilt = 1) => Array.from({ length: k }, (_, i) => [i + 1, 1 / Math.pow(i + 1, tilt)]);
const odd = (k, tilt = 1) => Array.from({ length: k }, (_, i) => [i * 2 + 1, 1 / Math.pow(i * 2 + 1, tilt)]);
const BAND = {
  tuba: { ...held([[1, 1], [2, 0.5], [3, 0.22], [4, 0.1]]), vol: 54, env: [[0, 64], [6, 56], [24, 40], [60, 0]] },
  kazoo: { ...held(saw(14, 0.85), 2, 16, 0.18), vol: 36, env: [[0, 50], [3, 64], [40, 54], [90, 0]], vib: [0, 24, 5] },
  clarinet: { ...held(odd(7, 1.1), 3, 16), vol: 44, env: [[0, 40], [4, 64], [50, 54], [90, 0]], vib: [0, 20, 3] },
  accordion: { ...held(saw(10, 1.05).map(([h, a]) => [h, a * (h % 2 ? 1 : 0.6)]), 2, 32, 0.08), vol: 36, env: [[0, 44], [4, 64], [60, 56], [100, 0]] },
  organ: { ...held([[1, 1], [2, 0.8], [3, 0.5], [4, 0.3], [6, 0.2]], 2, 16), vol: 30, env: [[0, 64], [80, 60], [100, 0]] },
  uke: { ...struck([[1, 1, 7], [2, 0.5, 10], [3, 0.3, 15], [4, 0.14, 22]], 0.8, 0.2), vol: 46 },
  marimba: { ...struck([[1, 1, 9], [4, 0.35, 28], [10, 0.08, 60]], 0.6, 0.1), vol: 52 },
  glock: { ...struck([[4, 1, 5], [11, 0.3, 14], [17, 0.1, 30]], 1.0, 0.05), vol: 28 },
  vibes: { ...struck([[1, 1, 2.6], [4, 0.22, 9]], 1.6), vol: 40, vib: [0, 28, 4] },
  pluck: { ...struck([[1, 1, 11], [2, 0.7, 13], [3, 0.3, 20]], 0.6, 0.1), vol: 56 },
  surf: { ...struck(saw(10, 1).map(([h, a]) => [h, a, 3 + h * 1.6]), 1.2, 0.15), vol: 42 },
  kick: { ...drum({ secs: 0.28, f0: 150, f1: 46, sweep: 34, body: 1, decay: 12, noise: 0.15, ndecay: 90 }), vol: 62 },
  snare: { ...drum({ secs: 0.22, f0: 230, f1: 170, sweep: 40, body: 0.45, decay: 18, noise: 0.8, ndecay: 17, seed: 11 }), vol: 48 },
  hat: { ...drum({ secs: 0.07, noise: 1, ndecay: 60, hp: 0.92, seed: 21 }), vol: 26 },
  shaker: { ...drum({ secs: 0.1, noise: 0.8, ndecay: 40, hp: 0.8, seed: 31 }), vol: 18 },
  rim: { ...drum({ secs: 0.05, f0: 1500, f1: 900, sweep: 60, body: 0.7, decay: 70, noise: 0.3, ndecay: 110, seed: 41 }), vol: 30 },
};
const NAMES = Object.keys(BAND);
const INST = Object.fromEntries(NAMES.map((n, i) => [n, i + 1]));

// ── writing notes ───────────────────────────────────────────────────
const KEY = { C: 0, 'C#': 1, Db: 1, D: 2, Eb: 3, E: 4, F: 5, 'F#': 6, G: 7, Ab: 8, A: 9, Bb: 10, B: 11 };
/** semitones of a chord name over C: 'F', 'Am', 'G7', 'Bdim' */
function chord(name) {
  const m = /^([A-G](?:b|#)?)(m7|m|7|dim|maj7)?$/.exec(name);
  if (!m) throw new Error(`chord ${name}`);
  const root = KEY[m[1]], q = m[2] || '';
  const t = q === 'm' ? [0, 3, 7] : q === 'm7' ? [0, 3, 7, 10] : q === '7' ? [0, 4, 7, 10] : q === 'dim' ? [0, 3, 6] : q === 'maj7' ? [0, 4, 7, 11] : [0, 4, 7];
  return { root, tones: t };
}
/** XM note number: octave 4 C = 49 (real middle C) */
const xmNote = (oct, semi) => 49 + (oct - 4) * 12 + semi;

/** A melody line: "c5 . e5 g5 - r": a note, '.' next step holds on silently (nothing written), '-' also holds, 'r' a note cut */
function line(text) {
  return text.trim().split(/[\s|]+/).map((w) => {
    if (w === '.' || w === '-') return null;
    if (w === 'r') return 'cut';
    const m = /^([a-g])(#|b)?(\d)$/.exec(w);
    if (!m) throw new Error(`note ${w}`);
    const semi = KEY[m[1].toUpperCase()] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0);
    return xmNote(+m[3], semi);
  });
}

class Song {
  constructor(name, bpm, beatsPerBar, rowsPerBeat, channels) {
    Object.assign(this, { name, bpm, beatsPerBar, rowsPerBeat, channels, rows: [], ticks: 6 });
    this.barRows = beatsPerBar * rowsPerBeat;
  }
  /** room for this bar */
  grow(bar) { const need = (bar + 1) * this.barRows; while (this.rows.length < need) this.rows.push(Array.from({ length: this.channels }, () => null)); }
  put(bar, row, ch, inst, note, vol) {
    this.grow(bar);
    this.rows[bar * this.barRows + row][ch] = { note, inst: INST[inst], vol };
  }
  /** a melody over bars, each step one row at the given stride */
  melody(bar, ch, inst, text, stride = 1, vol) {
    let r = 0;
    for (const n of line(text)) {
      if (n === 'cut') { this.grow(bar + Math.floor(r / this.barRows)); this.rows[bar * this.barRows + r][ch] = { note: 97 }; }
      else if (n) this.put(bar + Math.floor(r / this.barRows), r % this.barRows, ch, inst, n, vol);
      r += stride;
    }
  }
  /** a drum pattern per bar: { kick: 'x...x...', ... } one char a step, the step a row divided evenly over the bar */
  beat(fromBar, bars, ch, inst, pat, vol) {
    const stride = this.barRows / pat.length;
    for (let b = fromBar; b < fromBar + bars; b++)
      for (let i = 0; i < pat.length; i++)
        if (pat[i] !== '.') this.put(b, i * stride, ch, inst, 49, pat[i] === 'o' ? Math.round((vol ?? 64) * 0.55) : vol);
  }
  /** a bass line following chords, one chord a bar: pattern of 'R' root, '5' fifth, '3' third, 'O' root an octave up, '.' rest */
  bass(fromBar, chords, ch, inst, pat, oct = 2, vol) {
    this.formBars = fromBar + chords.length;
    const stride = this.barRows / pat.length;
    chords.forEach((c, k) => {
      const { root, tones } = chord(c);
      for (let i = 0; i < pat.length; i++) {
        const p = pat[i];
        if (p === '.') continue;
        const semi = root + (p === '5' ? tones[2] : p === '3' ? tones[1] : p === 'O' ? 12 : p === 'L' ? tones[2] - 12 : 0);
        this.put(fromBar + k, i * stride, ch, inst, xmNote(oct, semi), vol);
      }
    });
  }
  /** chord stabs on a run of channels: the pattern says which steps sound */
  chords(fromBar, chords, ch, inst, pat, oct = 4, vol) {
    const stride = this.barRows / pat.length;
    chords.forEach((c, k) => {
      const { root, tones } = chord(c);
      for (let i = 0; i < pat.length; i++) {
        if (pat[i] !== 'x') continue;
        tones.slice(0, 3).forEach((t, j) => this.put(fromBar + k, i * stride, ch + j, inst, xmNote(oct, root + t), vol));
      }
    });
  }
  /** a 16-row-max split of rows into patterns, 64 rows each */
  patterns() {
    const out = [];
    for (let i = 0; i < this.rows.length; i += 64) out.push(this.rows.slice(i, i + 64));
    return out;
  }
}

// ── the tracks ──────────────────────────────────────────────────────
const SONGS = [];

// the title: a town-band march played with far too much conviction, for nothing
{
  const s = new Song('March of the Unimpressed', 112, 4, 4, 9);
  const A = ['F', 'F', 'C7', 'F', 'Bb', 'F', 'C7', 'F'], B = ['Dm', 'Dm', 'A', 'Dm', 'Bb', 'F', 'G7', 'C7'];
  const form = [...A, ...A, ...B, ...A];
  s.bass(0, form, 0, 'tuba', 'R.5.R.5.', 2);
  s.chords(0, form, 1, 'accordion', '..x...x.', 4, 34);
  s.beat(0, 32, 4, 'kick', 'x...x...');
  s.beat(0, 32, 5, 'snare', '..o...o.', 40);
  s.beat(0, 32, 8, 'hat', 'x.x.x.x.');
  const mA = 'c5 . . a4 c5 . f5 . | e5 . c5 . g4 . . . | bb4 . . g4 bb4 . e5 . | f5 . c5 . a4 . . . | d5 . . bb4 d5 . f5 . | c5 . a4 . f4 . a4 . | g4 . bb4 . e5 . g5 . | f5 . . . r . . .';
  const mB = 'a4 . d5 . f5 . d5 . | a5 . f5 . d5 . . . | c#5 . e5 . a5 . e5 . | f5 . e5 . d5 . . . | d5 . f5 . bb5 . f5 . | a5 . f5 . c5 . . . | b4 . d5 . g5 . f5 . | e5 . g5 . bb5 . . .';
  const row = (t) => t.replaceAll('|', ' ');
  s.melody(0, 6, 'clarinet', row(mA), 2);
  s.melody(8, 6, 'kazoo', row(mA), 2);
  s.melody(16, 6, 'clarinet', row(mB), 2);
  s.melody(24, 6, 'kazoo', row(mA), 2);
  s.melody(8, 7, 'glock', 'f5 . . . a5 . . . | c6 . . . . . . . | e5 . . . g5 . . . | f5 . . . . . . . | f5 . . . bb5 . . . | c6 . . . a5 . . . | g5 . . . e5 . . . | f5 . . . . . . .'.replaceAll('|', ' '), 2);
  SONGS.push(['title', s]);
}
// world 1: bouncy, cheerful beyond reason
{
  const s = new Song('Cheerful Beyond Reason', 138, 4, 4, 8);
  const v = ['C', 'Am', 'F', 'G7', 'C', 'Am', 'Dm', 'G7'], c = ['F', 'G7', 'Em', 'Am', 'F', 'G7', 'C', 'C'];
  const form = [...v, ...v, ...c, ...v];
  s.bass(0, form, 0, 'pluck', 'R..R.5.3', 2);
  s.chords(0, form, 1, 'uke', '..x..x.x', 4, 44);
  s.beat(0, 32, 4, 'kick', 'x..x..x.');
  s.beat(0, 32, 5, 'rim', '....x...');
  s.beat(0, 32, 7, 'shaker', 'xoxoxoxo');
  const m1 = 'e5 . g5 . c6 . g5 . a5 . . g5 e5 . c5 . f5 . a5 . f5 . c5 . d5 . g5 . b5 . g5 . e5 . g5 . c6 . e6 . d6 . c6 . a5 . e5 . f5 . a5 . d6 . c6 . b5 . g5 a5 b5 . r . . .';
  const m2 = 'a5 . . c6 a5 . f5 . b5 . . d6 b5 . g5 . g5 . e5 . g5 . b5 . c6 . a5 . e5 . . . a5 . . c6 a5 . f5 . b5 . . d6 f6 . d6 . e6 . . . c6 . g5 . c6 . . . r . . .';
  s.melody(0, 6, 'marimba', m1, 2);
  s.melody(8, 6, 'kazoo', m1, 2);
  s.melody(16, 6, 'kazoo', m2, 2);
  s.melody(24, 6, 'marimba', m1, 2);
  SONGS.push(['world1', s]);
}
// world 2: caves, something is dripping and it is a bookkeeper
{
  const s = new Song('Something Is Dripping', 96, 4, 4, 8);
  const v = ['Am', 'Am', 'F', 'E', 'Am', 'Am', 'Dm', 'E'], c = ['Dm', 'Am', 'E', 'Am', 'F', 'Dm', 'E', 'E'];
  const form = [...v, ...c, ...v, ...c];
  s.bass(0, form, 0, 'pluck', 'R...5...R.L.5...', 2);
  s.chords(0, form, 1, 'organ', 'x...............', 3, 16);
  s.beat(0, 32, 4, 'rim', '..x...x...x.x...', 24);
  s.beat(0, 32, 5, 'kick', 'x.......x.......', 40);
  const m1 = 'a5 . . . c6 . . . e6 . . . d6 . c6 . b5 . . . g#5 . . . a5 . . . . . . . f5 . . . a5 . . . c6 . . . b5 . a5 . g#5 . . . e5 . . . g#5 . b5 . . . . . a5 . . . c6 . . . e6 . . . f6 . e6 . d6 . . . c6 . . . a5 . . . . . . . f5 . a5 . d6 . c6 . a5 . f5 . d5 . . . e5 . g#5 . b5 . e6 . d6 . . . r . . . . . . .';
  const m2 = 'f5 . . . a5 . . . d6 . . . c6 . a5 . e5 . . . a5 . . . c6 . . . b5 . a5 . g#5 . . . b5 . . . e6 . . . d6 . b5 . a5 . . . . . . . r . . . . . . . f5 . . . c6 . . . a5 . . . f5 . . . d5 . f5 . a5 . d6 . c6 . a5 . f5 . d5 . e5 . . . g#5 . . . b5 . . . e6 . . . d6 . . . b5 . . . g#5 . . . e5 . . .';
  s.melody(0, 6, 'marimba', m1, 2);
  s.melody(8, 6, 'clarinet', m2, 2);
  s.melody(16, 6, 'marimba', m1, 2);
  s.melody(24, 6, 'clarinet', m2, 2);
  s.melody(4, 7, 'glock', 'e6 . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . a6 . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .', 1, 22);
  s.melody(20, 7, 'glock', 'e6 . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . a6 . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .', 1, 22);
  SONGS.push(['world2', s]);
}
// world 3: flooded ruins, a soggy waltz
{
  const s = new Song('Soggy Waltz', 168, 3, 4, 8);
  const v = ['Dm', 'Dm', 'A7', 'A7', 'A7', 'A7', 'Dm', 'Dm', 'Dm', 'Dm', 'Gm', 'Gm', 'Dm', 'A7', 'Dm', 'Dm'];
  const c = ['F', 'F', 'C7', 'C7', 'C7', 'C7', 'F', 'F', 'Bb', 'Bb', 'F', 'F', 'Gm', 'A7', 'Dm', 'A7'];
  const form = [...v, ...c, ...v, ...c];
  s.bass(0, form, 0, 'tuba', 'R..', 2);
  s.chords(0, form, 1, 'accordion', '.xx', 4, 30);
  s.beat(0, 64, 4, 'kick', 'x..', 44);
  s.beat(0, 64, 5, 'shaker', '.xx');
  const m1 = 'a5 . . . . . . . d6 . . . f6 . . . e6 . d6 . | c#6 . . . . . . . a5 . . . e6 . . . . . . . g6 . . . f6 . . . e6 . . . c#6 . . . . . . . . . . . r . . . . . . . . . . . | a5 . . . . . . . d6 . . . f6 . . . a6 . . . g6 . . . f6 . . . e6 . . . bb5 . . . d6 . . . g6 . . . f6 . . . . . . . r . . . a5 . . . e6 . . . d6 . c#6 . d6 . . . . . . . . . . . r . . . . . . .'.replaceAll('|', ' ');
  const m2 = 'c6 . . . . . . . a5 . . . f5 . . . a5 . c6 . f6 . . . . . . . r . . . e6 . . . g6 . . . bb6 . . . g6 . . . e6 . . . c6 . . . . . . . r . . . f6 . . . a6 . . . c7 . . . a6 . . . f6 . . . d6 . . . . . . . r . . . f6 . . . a6 . . . f6 . . . c6 . . . a5 . . . g5 . bb5 . d6 . . . g6 . . . e6 . . . c#6 . . . d6 . . . . . . . a5 . . . c#6 . . . e6 . . .'.replaceAll('|', ' ');
  s.melody(0, 6, 'clarinet', m1, 1);
  s.melody(16, 6, 'accordion', m2, 1, 44);
  s.melody(32, 6, 'kazoo', m1, 1);
  s.melody(48, 6, 'clarinet', m2, 1);
  SONGS.push(['world3', s]);
}
// world 4: the frozen head office, elevator music that has been left on since the founding of the company
{
  const s = new Song('Please Hold Your Pelt', 104, 4, 4, 8);
  const v = ['Cmaj7', 'Am7', 'Dm7'.replace('7', ''), 'G7', 'Em', 'Am', 'Dm', 'G7'], c = ['Fmaj7', 'Em', 'Dm', 'Cmaj7', 'Fmaj7', 'Em', 'D', 'G7'];
  const form = [...v, ...v, ...c, ...v];
  s.bass(0, form, 0, 'pluck', 'R..5..3.R..5.R.3', 2, 50);
  s.chords(0, form, 1, 'vibes', 'x.....x.', 4, 32);
  s.beat(0, 32, 4, 'rim', '...x..x.', 18);
  s.beat(0, 32, 5, 'shaker', 'xxoxxxox');
  s.beat(0, 32, 7, 'kick', 'x.......', 34);
  const m1 = 'e5 . g5 . b5 . . . a5 . c6 . e6 . . . d6 . c6 . a5 . f5 . g5 . b5 . d6 . . . b5 . g5 . e5 . . . c6 . a5 . e5 . . . f5 . a5 . c6 . a5 . g5 . . . r . . .';
  const m2 = 'a5 . c6 . e6 . . . g5 . b5 . e6 . . . f5 . a5 . d6 . c6 . b5 . . . g5 . . . a5 . c6 . e6 . . . g5 . b5 . e6 . g6 . f#6 . e6 . d6 . a5 . g5 . b5 . d6 . f6 .';
  s.melody(0, 6, 'glock', m1, 2);
  s.melody(8, 6, 'clarinet', m1, 2, 40);
  s.melody(16, 6, 'kazoo', m2, 2, 40);
  s.melody(24, 6, 'glock', m1, 2);
  SONGS.push(['world4', s]);
}
// world 5: the volcano, a surf band that has noticed the lava and decided to carry on
{
  const s = new Song('Surf Report: Lava', 172, 4, 4, 8);
  const v = ['Em', 'Em', 'C', 'B7', 'Em', 'Em', 'Am', 'B7'], c = ['Am', 'Am', 'Em', 'Em', 'C', 'D', 'B7', 'B7'];
  const form = [...v, ...v, ...c, ...v];
  s.bass(0, form, 0, 'pluck', 'R.R.5.R.', 2, 60);
  s.chords(0, form, 1, 'surf', 'x..x..x.', 3, 30);
  s.beat(0, 32, 4, 'kick', 'x..x..x.');
  s.beat(0, 32, 5, 'snare', '..x...x.', 44);
  s.beat(0, 32, 7, 'hat', 'xoxoxoxo');
  const m1 = 'e5 . g5 e5 b5 . g5 e5 e6 . b5 . g5 . e5 . c6 . e6 . g6 . e6 c6 b5 . d#6 . f#6 . b6 . e5 . g5 e5 b5 . g5 e5 e6 . g6 . e6 . b5 . a5 . c6 . e6 . a6 . b5 . d#6 . f#6 . r . . .';
  const m2 = 'a5 . . c6 e6 . . a6 g6 . e6 . c6 . a5 . e5 . . g5 b5 . . e6 d6 . b5 . g5 . e5 . c6 . e6 . g6 . e6 . d6 . f#6 . a6 . f#6 . b5 . d#6 . f#6 . b6 . a6 . f#6 . d#6 . b5 .';
  s.melody(0, 6, 'surf', m1, 2, 60);
  s.melody(8, 6, 'surf', m1, 2, 60);
  s.melody(16, 6, 'kazoo', m2, 2);
  s.melody(24, 6, 'surf', m1, 2, 60);
  SONGS.push(['world5', s]);
}
// a boss: a polka in a hurry, the one tune the whole company agrees on
{
  const s = new Song('Mandatory Polka', 200, 2, 4, 8);
  const v = ['Am', 'E', 'E', 'Am', 'Am', 'E', 'E', 'Am'], c = ['C', 'G', 'G', 'C', 'F', 'C', 'E', 'Am'];
  const form = [...v, ...v, ...c, ...v, ...v, ...c];
  s.bass(0, form, 0, 'tuba', 'R.5.', 2);
  s.chords(0, form, 1, 'accordion', '.x.x', 4, 34);
  s.beat(0, 48, 4, 'kick', 'x.x.');
  s.beat(0, 48, 5, 'snare', '.x.x', 38);
  const m1 = 'e5 a5 c6 e6 d6 c6 b5 a5 g#5 b5 e6 b5 g#5 e5 g#5 b5 d6 c6 b5 a5 g#5 a5 b5 g#5 a5 . e5 . a5 . . . e5 a5 c6 e6 d6 c6 b5 a5 g#5 b5 e6 b5 g#5 e5 g#5 b5 d6 c6 b5 a5 g#5 a5 b5 g#5 a5 . . . r . . .';
  const m2 = 'e5 g5 c6 e6 g6 e6 c6 g5 d5 g5 b5 d6 g6 d6 b5 g5 f5 g5 a5 b5 g5 a5 b5 c6 d6 c6 b5 c6 e6 . . . f5 a5 c6 f6 a6 f6 c6 a5 e5 g5 c6 e6 g6 e6 c6 g5 e5 g#5 b5 e6 g#6 e6 b5 g#5 a5 b5 c6 e6 a6 . . .';
  s.melody(0, 6, 'kazoo', m1, 1);
  s.melody(16, 6, 'clarinet', m1, 1);
  s.melody(32, 6, 'kazoo', m2, 1);
  s.melody(40, 6, 'clarinet', m1, 1);
  s.melody(48 - 16 + 16 - 16, 7, 'glock', m2, 1, 24);
  SONGS.push(['boss', s]);
}

// ── the file ────────────────────────────────────────────────────────
function writeXm(song) {
  const parts = [];
  const pats = song.patterns();
  const head = Buffer.alloc(60 + 20 + 256);
  head.write('Extended Module: ', 0, 'latin1');
  head.write(song.name.slice(0, 20).padEnd(20, ' '), 17, 'latin1');
  head[37] = 0x1a;
  head.write('coopplatformer'.padEnd(20, ' '), 38, 'latin1');
  head.writeUInt16LE(0x0104, 58);
  head.writeUInt32LE(20 + 256, 60);
  head.writeUInt16LE(pats.length, 64); // song length
  head.writeUInt16LE(0, 66); // restart at the top: the loop
  head.writeUInt16LE(song.channels, 68);
  head.writeUInt16LE(pats.length, 70);
  head.writeUInt16LE(NAMES.length, 72);
  head.writeUInt16LE(1, 74); // linear frequency table
  head.writeUInt16LE(song.ticks, 76);
  head.writeUInt16LE(song.bpm, 78);
  for (let i = 0; i < pats.length; i++) head[80 + i] = i;
  parts.push(head);
  for (const pat of pats) {
    const body = [];
    for (const row of pat)
      for (const c of row) {
        if (!c) { body.push(0x80); continue; }
        let mask = 0x80;
        if (c.note) mask |= 1;
        if (c.inst) mask |= 2;
        if (c.vol !== undefined) mask |= 4;
        body.push(mask);
        if (c.note) body.push(c.note);
        if (c.inst) body.push(c.inst);
        if (c.vol !== undefined) body.push(0x10 + Math.max(0, Math.min(64, c.vol)));
      }
    const h = Buffer.alloc(9);
    h.writeUInt32LE(9, 0);
    h[4] = 0;
    h.writeUInt16LE(pat.length, 5);
    h.writeUInt16LE(body.length, 7);
    parts.push(h, Buffer.from(body));
  }
  for (const name of NAMES) {
    const b = BAND[name], n = b.pcm.length;
    let peak = 0;
    for (const v of b.pcm) peak = Math.max(peak, Math.abs(v));
    const pcm = new Int16Array(n);
    for (let i = 0; i < n; i++) pcm[i] = Math.round((b.pcm[i] / peak) * 30000);
    const ih = Buffer.alloc(263);
    ih.writeUInt32LE(263, 0);
    ih.write(name.padEnd(22, ' '), 4, 'latin1');
    ih[26] = 0;
    ih.writeUInt16LE(1, 27); // samples
    ih.writeUInt32LE(40, 29); // sample header size
    // volume envelope: points of (frame, volume), so a held note fades after the key is let go
    const env = b.env || [];
    env.slice(0, 12).forEach(([x, y], i) => { ih.writeUInt16LE(x, 129 + i * 4); ih.writeUInt16LE(y, 131 + i * 4); });
    ih[225] = Math.min(12, env.length);
    ih[227] = env.length ? 1 : 0; // sustain on the middle point
    ih[233] = env.length ? 1 | 2 : 0;
    const [vt, vs, vd] = b.vib || [0, 0, 0];
    ih[235] = vt, ih[236] = vs, ih[237] = vd, ih[238] = 0;
    ih.writeUInt16LE(b.env ? 800 : 0, 239); // fade after key off
    parts.push(ih);
    const sh = Buffer.alloc(40);
    sh.writeUInt32LE(n * 2, 0);
    sh.writeUInt32LE((b.loopStart || 0) * 2, 4);
    sh.writeUInt32LE((b.loopLen || 0) * 2, 8);
    sh[12] = b.vol;
    sh[13] = 0;
    sh[14] = (b.loopLen ? 1 : 0) | 16; // forward loop, 16 bits
    sh[15] = 128;
    sh[16] = 24; // relative note: C-4 plays at 33452 Hz
    sh.write(name.padEnd(22, ' '), 18, 'latin1');
    const d = Buffer.alloc(n * 2);
    let prev = 0;
    for (let i = 0; i < n; i++) { d.writeInt16LE(((pcm[i] - prev) << 16) >> 16, i * 2); prev = pcm[i]; }
    parts.push(sh, d);
  }
  return Buffer.concat(parts);
}

mkdirSync(OUT, { recursive: true });
for (const [file, song] of SONGS) {
  if (song.rows.length % song.barRows) throw new Error(`${file}: not whole bars`);
  /* the form is what the bass plays: a melody that runs on past it would leave bars with nothing under them */
  const bars = song.formBars ?? song.rows.length / song.barRows;
  song.rows.length = bars * song.barRows;
  const b = writeXm(song), secs = (song.rows.length * (song.ticks * 2.5 / song.bpm));
  writeFileSync(join(OUT, `${file}.xm`), b);
  console.log(`${file}.xm  "${song.name}"  ${song.rows.length / song.barRows} bars, ${secs.toFixed(0)} s a loop, ${(b.length / 1024).toFixed(0)} KB`);
}
