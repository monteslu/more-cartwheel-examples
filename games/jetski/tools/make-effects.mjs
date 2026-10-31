// Procedural images: the mist sprite and the band of evening cloud around the horizon. Every pixel from our own code.
//   node games/jetski/tools/make-effects.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync, crc32 } from 'node:zlib';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const OUT = join(resolve(dirname(fileURLToPath(import.meta.url)), '..'), 'assets');
const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x), lerp = (a, b, t) => a + (b - a) * t;
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
function hash(x, y, s) { let h = (x * 374761393 + y * 668265263 + s * 1274126177) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }
function vnoise(x, y, s, perX) {
  const xi = Math.floor(x), yi = Math.floor(y), u = x - xi, v = y - yi, su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v);
  const wx = (q) => (perX ? ((q % perX) + perX) % perX : q);
  return lerp(lerp(hash(wx(xi), yi, s), hash(wx(xi + 1), yi, s), su), lerp(hash(wx(xi), yi + 1, s), hash(wx(xi + 1), yi + 1, s), su), sv);
}
const fbm = (x, y, oct, s, perX) => { let a = 0.5, f = 1, sum = 0, n = 0; for (let i = 0; i < oct; i++) { sum += a * vnoise(x * f, y * f, s + i, perX ? perX * f : 0); n += a; a *= 0.5; f *= 2; } return sum / n; };
function png(w, h, bytes) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; Buffer.from(bytes.buffer, y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1); }
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td) >>> 0); return Buffer.concat([l, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}
function save(rel, w, h, bytes) { const p = join(OUT, rel); mkdirSync(dirname(p), { recursive: true }); writeFileSync(p, png(w, h, bytes)); }

// mist: a soft torn puff, not a disc
{
  const N = 256, b = new Uint8Array(N * N * 4);
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    const x = (i + 0.5) / N * 2 - 1, y = (j + 0.5) / N * 2 - 1, r = Math.hypot(x, y);
    const n = fbm(i / N * 7, j / N * 7, 5, 3), n2 = fbm(i / N * 18 + 5, j / N * 18, 3, 9);
    const edge = 1 - smooth(0.25 + 0.5 * n, 0.65 + 0.42 * n, r);
    const a = clamp(edge * (0.45 + 0.75 * smooth(0.25, 0.75, n * 0.75 + n2 * 0.25)));
    const k = i * 4 + j * N * 4;
    b[k] = b[k + 1] = b[k + 2] = 255; b[k + 3] = Math.round(a * 255);
  }
  save('effects/mist.png', N, N, b);
}

// the sun's sprite: a small hot core in a wide glow
{
  const N = 256, b = new Uint8Array(N * N * 4);
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    const r = Math.hypot((i + 0.5) / N * 2 - 1, (j + 0.5) / N * 2 - 1);
    const v = 1 - smooth(0.0, 1.0, r);
    const a = clamp(0.0018 * Math.pow(v, 5) + 0.9 * (1 - smooth(0.05, 0.075, r)));
    const k = (i + j * N) * 4;
    b[k] = b[k + 1] = b[k + 2] = Math.round(a * 255); b[k + 3] = 255;
  }
  save('effects/sun.png', N, N, b);
}

// a sky, painted: u = azimuth (the sun's way at u = 0.5), v from the top of the sky (0) to the horizon (1) with
// el = (1 - v)^1.7 of 90 degrees, so most of the texels lie low down where the eye looks
function paintSky(path, S) {
  const W = 4096, H = 1024, b = new Uint8Array(W * H * 4);
  const PER = 32;
  const billow = (x, y, oct, s) => { let a = 0.5, f = 1, sum = 0, n = 0; for (let i = 0; i < oct; i++) { sum += a * (1 - Math.abs(2 * vnoise(x * f, y * f, s + i, PER * f) - 1)); n += a; a *= 0.52; f *= 2; } return sum / n; };
  const dens = (u, el) => {
    const X = u * PER, e = el * 5.5; // el here 0 horizon .. 1 at about 16 degrees
    const wx = (fbm(X * 0.5, e * 1.2, 3, 21, PER * 0.5) - 0.5) * 1.6, wy = (fbm(X * 0.5 + 9, e * 1.2 + 5, 3, 22, PER * 0.5) - 0.5) * 0.5;
    const towers = fbm(X * 0.12, 3.3, 3, 33, Math.round(PER * 0.12));
    const top = 0.16 + 0.5 * smooth(0.44, 0.68, towers) + 0.14 * fbm(X * 0.6, 9.1, 2, 34, Math.round(PER * 0.6));
    const puff = billow(X + wx, e * 3 + wy, 6, 31);
    return smooth(0.5, 0.63, puff + 0.42 * (1 - smooth(top * 0.2, top, el)) - 0.36);
  };
  const mix3 = (A, B, t) => [lerp(A[0], B[0], t), lerp(A[1], B[1], t), lerp(A[2], B[2], t)];
  const { ZEN, MID, HIGHW, LOWW, AWAY, AWAYLOW, HAZE0, HAZE1, CIR0, CIR1, SHADE0, SHADE1, LIGHT0, LIGHT1, RIM, cover } = S;
  for (let j = 0; j < H; j++) {
    const el = Math.pow(1 - (j + 0.5) / H, 1.7); // 0 horizon, 1 zenith
    for (let i = 0; i < W; i++) {
      const u = (i + 0.5) / W;
      const az = Math.abs(u - 0.5) * 2, near = Math.pow(1 - az, 1.6);
      // the sky's own light, warmest low down on the sun's side
      const band = mix3(mix3(AWAYLOW, LOWW, near), mix3(AWAY, HIGHW, near), smooth(0.0, 0.1, el));
      let c = mix3(band, MID, smooth(0.05, 0.3 - 0.06 * near, el));
      c = mix3(c, ZEN, smooth(0.28, 0.85, el));
      const haze = (1 - smooth(0.0, 0.035, el));
      c = mix3(c, mix3(HAZE0, HAZE1, near), haze * 0.75);
      // high streaks, lit from below, thinning away from the sun
      const wob = fbm(u * 10, el * 3, 2, 42, 10);
      const cirrus = smooth(0.55, 0.82, fbm(u * PER * 1.5 + 3 * wob, el * 34, 5, 41, PER * 1.5)) * smooth(0.1, 0.22, el) * (1 - smooth(0.55, 0.85, el)) * 0.5;
      c = mix3(c, mix3(CIR0, CIR1, near), cirrus);
      // a bank of cumulus on the horizon, its near side in shade, lit along the edge toward the sun
      if (el < (cover > 1 ? 1.01 : 0.42)) {
        const e0 = el / (0.18 * cover);
        const d = dens(u, e0) * smooth(0, 0.006, el);
        if (d > 0.002) {
          const sd = u > 0.5 ? -1 : 1;
          let od = 0;
          for (let q = 1; q <= 4; q++) od += dens(u + sd * q * 0.0024, e0 + q * 0.016);
          const lit = Math.exp(-od * 0.7);
          const shade = mix3(SHADE0, SHADE1, near), light = mix3(LIGHT0, LIGHT1, near);
          let k = mix3(shade, light, lit);
          const rim = (1 - smooth(0.12, 0.65, d)) * Math.pow(near, 1.5); k = mix3(k, RIM, rim);
          c = mix3(c, k, d * (1 - 0.5 * Math.pow(near, 8) * (1 - smooth(0, 0.08, el))));
        }
      }
      const q = (i + j * W) * 4;
      b[q] = Math.round(clamp(Math.pow(c[0], 1 / 2.2)) * 255); b[q + 1] = Math.round(clamp(Math.pow(c[1], 1 / 2.2)) * 255); b[q + 2] = Math.round(clamp(Math.pow(c[2], 1 / 2.2)) * 255); b[q + 3] = 255;
    }
  }
  save(path, W, H, b);
}
// Crater Atoll's evening
paintSky('textures/sky.png', {
  ZEN: [0.03, 0.085, 0.24], MID: [0.1, 0.22, 0.48], HIGHW: [0.95, 0.42, 0.2], LOWW: [1.0, 0.6, 0.22], AWAY: [0.34, 0.26, 0.44], AWAYLOW: [0.62, 0.38, 0.44],
  HAZE0: [0.9, 0.72, 0.68], HAZE1: [1.0, 0.8, 0.5], CIR0: [1.0, 0.82, 0.8], CIR1: [1.0, 0.66, 0.42],
  SHADE0: [0.3, 0.27, 0.38], SHADE1: [0.42, 0.26, 0.28], LIGHT0: [1.0, 0.78, 0.72], LIGHT1: [1.0, 0.68, 0.34], RIM: [1.0, 0.86, 0.56], cover: 1,
});
// Cold Sound's overcast morning: cloud to the top of the sky, a pale break low on the sun's side
paintSky('textures/sky-sound.png', {
  ZEN: [0.26, 0.31, 0.38], MID: [0.34, 0.4, 0.47], HIGHW: [0.62, 0.64, 0.64], LOWW: [0.82, 0.8, 0.72], AWAY: [0.36, 0.41, 0.47], AWAYLOW: [0.46, 0.51, 0.56],
  HAZE0: [0.56, 0.6, 0.64], HAZE1: [0.8, 0.78, 0.72], CIR0: [0.62, 0.66, 0.7], CIR1: [0.82, 0.8, 0.74],
  SHADE0: [0.2, 0.23, 0.28], SHADE1: [0.28, 0.3, 0.33], LIGHT0: [0.58, 0.61, 0.65], LIGHT1: [0.86, 0.84, 0.78], RIM: [0.95, 0.92, 0.84], cover: 5,
});
