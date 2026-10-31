// The course: a volcanic atoll, from our own code. Writes the terrain's heights and control words, the wave mask,
// the palm density map and src/course_data.h (the track, the buoys, the start grid), and grades the CC0 terrain scans.
//   node games/jetski/tools/make-course.mjs
import { writeFileSync, readFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const HERE = dirname(fileURLToPath(import.meta.url)), GAME = resolve(HERE, '..'), OUT = join(GAME, 'assets');
mkdirSync(join(OUT, 'course'), { recursive: true });
export const QUADS = 896, SPACING = 2, SIZE = QUADS * SPACING, MINH = -48, MAXH = 170;
const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
const lerp = (a, b, t) => a + (b - a) * t;
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
function hash(x, y, s) { let h = (x * 374761393 + y * 668265263 + s * 1274126177) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }
function vnoise(x, y, s) {
  const xi = Math.floor(x), yi = Math.floor(y), u = x - xi, v = y - yi, su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v);
  return lerp(lerp(hash(xi, yi, s), hash(xi + 1, yi, s), su), lerp(hash(xi, yi + 1, s), hash(xi + 1, yi + 1, s), su), sv);
}
const fbm = (x, y, oct, s) => { let a = 0.5, f = 1, sum = 0, n = 0; for (let i = 0; i < oct; i++) { sum += a * vnoise(x * f, y * f, s + i); n += a; a *= 0.5; f *= 2.03; } return sum / n; };
const ridged = (x, y, oct, s) => { let a = 0.5, f = 1, sum = 0, n = 0; for (let i = 0; i < oct; i++) { sum += a * (1 - Math.abs(2 * vnoise(x * f, y * f, s + i) - 1)); n += a; a *= 0.5; f *= 2.1; } return sum / n; };

// The track: a closed loop, metres, x east, z south. Lagoon, out through the east gap into the open swell, round the
// islet's sea stacks, back in over the north pass.
const P = [[-140, 150], [-20, 195], [110, 170], [205, 95], [290, 25], [395, 5], [510, 15], [625, -40], [690, -160], [670, -300], [570, -400], [430, -440], [290, -445], [165, -395], [85, -300], [40, -200], [-40, -120], [-150, -115], [-250, -55], [-290, 45], [-245, 125]];
const catmull = (a, b, c, d, t) => 0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (3 * b - a - 3 * c + d) * t * t * t);
const track = [];
for (let i = 0; i < P.length; i++) {
  const a = P[(i + P.length - 1) % P.length], b = P[i], c = P[(i + 1) % P.length], d = P[(i + 2) % P.length];
  for (let k = 0; k < 16; k++) track.push([catmull(a[0], b[0], c[0], d[0], k / 16), catmull(a[1], b[1], c[1], d[1], k / 16)]);
}
const cum = [0];
for (let i = 0; i < track.length; i++) { const a = track[i], b = track[(i + 1) % track.length]; cum.push(cum[i] + Math.hypot(b[0] - a[0], b[1] - a[1])); }
const LEN = cum[track.length];
function trackDist(x, z) {
  let best = 1e9;
  for (let i = 0; i < track.length; i += 1) {
    const a = track[i], b = track[(i + 1) % track.length], dx = b[0] - a[0], dz = b[1] - a[1];
    const t = clamp(((x - a[0]) * dx + (z - a[1]) * dz) / (dx * dx + dz * dz));
    const d = Math.hypot(x - a[0] - dx * t, z - a[1] - dz * t);
    if (d < best) best = d;
  }
  return best;
}
const pointAt = (s) => {
  s = ((s % LEN) + LEN) % LEN;
  let i = 0; while (cum[i + 1] < s) i++;
  const a = track[i], b = track[(i + 1) % track.length], t = (s - cum[i]) / (cum[i + 1] - cum[i]);
  const dx = b[0] - a[0], dz = b[1] - a[1], l = Math.hypot(dx, dz);
  return { x: a[0] + dx * t, z: a[1] + dz * t, dx: dx / l, dz: dz / l };
};
// sea stacks: basalt pillars off the east and north shores
const STACKS = [[560, 130, 34, 15], [600, 165, 22, 10], [760, -60, 46, 18], [785, -20, 26, 11], [770, -300, 38, 16], [545, -320, 58, 26], [490, -280, 34, 15], [600, -270, 24, 11], [430, -540, 42, 17], [230, -540, 30, 14], [400, 110, 20, 10]];

function height(x, z) {
  const r0 = Math.hypot(x, z), a = Math.atan2(z, x);
  const wob = (fbm(Math.cos(a) * 2 + 7, Math.sin(a) * 2 + 3, 4, 11) - 0.5) * 120;
  const R = 345 + wob;
  // the shelf falling to the deep
  let y = lerp(-46, -9, smooth(1000, R + 30, r0));
  // lagoon: pale sand a few metres down, with shoals
  const lag = 1 - smooth(R - 95, R - 40, r0);
  const lagFloor = -3.6 - 3.2 * fbm(x * 0.012, z * 0.012, 3, 5) + 2.4 * Math.max(0, fbm(x * 0.03 + 9, z * 0.03, 3, 6) - 0.55) * 4;
  y = lerp(y, lagFloor, lag);
  // the crater's rim: walls to the west and south-west, low cays to the east
  const wall = Math.pow(0.5 + 0.5 * Math.cos(a - Math.PI * 0.95), 1.4);
  const rimH = 5 + 26 * smooth(0.4, 0.75, fbm(Math.cos(a) * 5 + 2, Math.sin(a) * 5, 3, 22)) + 150 * wall * (0.55 + 0.75 * fbm(Math.cos(a) * 3, Math.sin(a) * 3, 3, 21));
  const w = 60 + 125 * wall;
  const g = Math.exp(-Math.pow((r0 - R - 8 * wall) / w, 2));
  let rim = (rimH + 3) * g - 3;
  if (wall > 0.05) rim += g * wall * 34 * (ridged(x * 0.018, z * 0.018, 5, 31) - 0.5) * 2;
  rim += g * 3 * (fbm(x * 0.05, z * 0.05, 4, 33) - 0.5) * 2;
  y = Math.max(y, rim);
  // the land's skin: gullies running downhill and broken ground; beaches keep smooth
  if (y > 1) {
    const k = smooth(1.5, 12, y);
    y += k * ((ridged(x * 0.03, z * 0.03, 5, 61) - 0.55) * 13 * (0.35 + 0.65 * smooth(8, 50, y)) + (fbm(x * 0.14, z * 0.14, 3, 62) - 0.5) * 3.2);
  }
  // rock strata: ledges across the slope, so a wall reads as layers, not as streaks down it
  if (y > 6) { const bands = fbm(x * 0.01 + 2, z * 0.01, 2, 63) * 22; y += smooth(6, 30, y) * 1.7 * Math.sin((y + bands) * 0.42); }
  // sea stacks
  for (const [sx, sz, h, sw] of STACKS) {
    const d = Math.hypot(x - sx, z - sz) * (0.8 + 0.7 * fbm(x * 0.07 + sx, z * 0.07, 3, 41));
    const prof = 1 - smooth(sw * 0.72, sw * 0.98, d);
    const top = h * (0.72 + 0.5 * ridged(x * 0.045, z * 0.045, 4, 42)) * (1 - 0.35 * smooth(0, sw, d));
    y = Math.max(y, lerp(-30, top, prof));
  }
  // the channel: the course stays in water
  const d = trackDist(x, z);
  const inLagoon = lag;
  const channel = lerp(-7, -3.2, inLagoon);
  if (y > channel) y = lerp(channel, y, smooth(46, 92, d));
  return y;
}

const N = QUADS + 1, H = new Float32Array(N * N), toW = (i) => (i - QUADS / 2) * SPACING;
console.log('heights...');
for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) H[j * N + i] = height(toW(i), toW(j));
const r16 = new Uint16Array(N * N);
for (let k = 0; k < N * N; k++) r16[k] = Math.round(clamp((H[k] - MINH) / (MAXH - MINH)) * 65535);
writeFileSync(join(OUT, 'course/heights.r16'), Buffer.from(r16.buffer));

// control words (cw_terrain_control): layers 0 pale sand, 1 wet coast sand, 2 basalt, 3 jungle
const ctl = new Uint32Array(N * N);
const word = (base, over, blend, ang, scale) => ((base & 31) << 27 | (over & 31) << 22 | (blend & 255) << 14 | (ang & 15) << 10 | (scale & 7) << 7) >>> 0;
const hAt = (i, j) => H[clamp(j, 0, QUADS) * N + clamp(i, 0, QUADS)];
for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
  const y = hAt(i, j), x = toW(i), z = toW(j);
  const nx = (hAt(i + 1, j) - hAt(i - 1, j)) / (2 * SPACING), nz = (hAt(i, j + 1) - hAt(i, j - 1)) / (2 * SPACING), slope = Math.hypot(nx, nz);
  const n1 = fbm(x * 0.03, z * 0.03, 4, 51), n2 = fbm(x * 0.11, z * 0.11, 3, 52);
  const steep = smooth(0.95, 1.5, slope + (n2 - 0.5) * 0.7);
  const jungle = smooth(3.5 + n1 * 5, 8 + n1 * 8, y) * (1 - smooth(95 + n1 * 50, 135 + n1 * 50, y));
  let base = 0, over = 1, blend = 0;
  if (y < -1.2) { base = 0; over = 1; blend = smooth(-3, -7, y) * 140 + 100 * smooth(0.52, 0.72, n1); }
  else if (y < 1.0) { base = 0; over = 1; blend = 255 * smooth(0.9, -0.4, y); }
  else { base = 0; over = 3; blend = 255 * jungle; }
  if (y > 2 || y < -10) { const rocky = Math.max(steep, y > 20 ? 0.1 : 0); if (rocky > 0.35) { base = jungle > 0.5 ? 3 : base; over = 2; blend = 255 * smooth(0.35, 0.8, rocky); } }
  if (y >= 0.4 && slope > 0.95) { base = 2; over = 3; blend = 255 * jungle * (1 - steep); }
  const h = hash(i >> 2, j >> 2, 61);
  ctl[j * N + i] = word(base, over, Math.round(clamp(blend, 0, 255)), Math.floor(h * 16), Math.floor(hash(i >> 3, j >> 3, 62) * 3));
}
writeFileSync(join(OUT, 'course/control.u32'), Buffer.from(ctl.buffer));

// the wave mask: how free the swell is. Open sea 1; it dies in the lagoon's shelter and as the floor shoals.
const MN = 256, M = new Float32Array(MN * MN), hWorld = (x, z) => hAt(Math.round(x / SPACING + QUADS / 2), Math.round(z / SPACING + QUADS / 2));
const WATER_SIZE = SIZE;
for (let j = 0; j < MN; j++) for (let i = 0; i < MN; i++) {
  const x = ((i + 0.5) / MN - 0.5) * WATER_SIZE, z = ((j + 0.5) / MN - 0.5) * WATER_SIZE, r0 = Math.hypot(x, z);
  const y = hWorld(x, z);
  const open = smooth(250, 400, r0 + (fbm(x * 0.004, z * 0.004, 2, 71) - 0.5) * 60);
  const shoal = smooth(-0.8, -8, y);
  M[j * MN + i] = Math.max(0.05, Math.max(open, 0.26) * shoal);
}
for (let pass = 0; pass < 6; pass++) {
  const C = M.slice();
  for (let j = 0; j < MN; j++) for (let i = 0; i < MN; i++) {
    let s = 0;
    for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) s += C[clamp(j + b, 0, MN - 1) * MN + clamp(i + a, 0, MN - 1)] * (a ? 1 : 2) * (b ? 1 : 2);
    M[j * MN + i] = s / 16;
  }
}
writeFileSync(join(OUT, 'course/wavemask.f32'), Buffer.from(M.buffer));

// palms: on low ground that is not too steep, in clumps
const DN = 448, D = new Uint8Array(DN * DN);
for (let j = 0; j < DN; j++) for (let i = 0; i < DN; i++) {
  const x = ((i + 0.5) / DN - 0.5) * SIZE, z = ((j + 0.5) / DN - 0.5) * SIZE, y = hWorld(x, z);
  const ii = Math.round(x / SPACING + QUADS / 2), jj = Math.round(z / SPACING + QUADS / 2);
  const slope = Math.hypot(hAt(ii + 2, jj) - hAt(ii - 2, jj), hAt(ii, jj + 2) - hAt(ii, jj - 2)) / (4 * SPACING);
  const v = smooth(1.3, 2.4, y) * (1 - smooth(55, 80, y)) * (1 - smooth(0.45, 0.7, slope)) * smooth(0.42, 0.62, fbm(x * 0.02, z * 0.02, 3, 81));
  D[j * DN + i] = Math.round(v * 255);
}
writeFileSync(join(OUT, 'course/palms.u8'), Buffer.from(D));

// rocks at the waterline and at the foot of the cliffs; shrubs that break up the hills
{
  const Rk = new Uint8Array(DN * DN), Sh = new Uint8Array(DN * DN);
  for (let j = 0; j < DN; j++) for (let i = 0; i < DN; i++) {
    const x = ((i + 0.5) / DN - 0.5) * SIZE, z = ((j + 0.5) / DN - 0.5) * SIZE, y = hWorld(x, z);
    const ii = Math.round(x / SPACING + QUADS / 2), jj = Math.round(z / SPACING + QUADS / 2);
    const slope = Math.hypot(hAt(ii + 2, jj) - hAt(ii - 2, jj), hAt(ii, jj + 2) - hAt(ii, jj - 2)) / (4 * SPACING);
    const band = smooth(-1.6, -0.5, y) * (1 - smooth(1.2, 4.5, y));
    const clump = smooth(0.4, 0.62, fbm(x * 0.05 + 3, z * 0.05, 3, 91));
    const away = smooth(24, 40, trackDist(x, z));
    Rk[j * DN + i] = Math.round(clamp(band * clump * (0.25 + slope) * 2.2) * away * 255);
    Sh[j * DN + i] = Math.round(smooth(3, 8, y) * (1 - smooth(85, 120, y)) * (1 - smooth(0.9, 1.5, slope)) * smooth(0.3, 0.55, fbm(x * 0.04, z * 0.04, 3, 92)) * 255);
  }
  writeFileSync(join(OUT, 'course/rocks.u8'), Buffer.from(Rk));
  writeFileSync(join(OUT, 'course/shrubs.u8'), Buffer.from(Sh));
}

// buoys: pairs of slaloms, alternating sides, spaced so the rhythm reads
const buoys = [];
let side = 1;
for (let s = 70; s < LEN - 60;) {
  const p = pointAt(s);
  const off = 8.5;
  buoys.push({ x: p.x - p.dz * side * off, z: p.z + p.dx * side * off, side, s });
  side = -side;
  s += 88 + 14 * Math.sin(s * 0.013);
}
// the lighthouse: on the top of the islet in the north-east
let lh = [0, -100, 0];
for (let j = 0; j < N; j += 2) for (let i = 0; i < N; i += 2) { const x = toW(i), z = toW(j), y = H[j * N + i]; if (x > 120 && x < 420 && z > -340 && z < -80 && y > lh[1]) lh = [x, y, z]; }
const start = pointAt(LEN - 28);
let out = `/* generated by tools/make-course.mjs */\n#define COURSE_QUADS ${QUADS}\n#define COURSE_SPACING ${SPACING}.0f\n#define COURSE_MINH ${MINH}.0f\n#define COURSE_MAXH ${MAXH}.0f\n#define COURSE_LEN ${LEN.toFixed(2)}f\n#define MASK_N ${MN}\n#define PALMS_N ${DN}\n`;
const TK = []; for (let s = 0; s < LEN - 0.01; s += LEN / 256) { const p = pointAt(s); TK.push(p); }
out += `#define TRACK_N ${TK.length}\nstatic const float TRACK[TRACK_N][4] = {\n` + TK.map((p) => `{${p.x.toFixed(2)}f,${p.z.toFixed(2)}f,${p.dx.toFixed(4)}f,${p.dz.toFixed(4)}f}`).join(',\n') + '\n};\n';
out += `#define BUOY_N ${buoys.length}\nstatic const float BUOYS[BUOY_N][4] = { /* x, z, side (+1: pass with the buoy on your right), s */\n` + buoys.map((b) => `{${b.x.toFixed(2)}f,${b.z.toFixed(2)}f,${b.side}.0f,${b.s.toFixed(1)}f}`).join(',\n') + '\n};\n';
out += `#define LIGHT_X ${lh[0].toFixed(2)}f\n#define LIGHT_Y ${(lh[1] - 0.4).toFixed(2)}f\n#define LIGHT_Z ${lh[2].toFixed(2)}f\n`;
out += `#define START_X ${start.x.toFixed(2)}f\n#define START_Z ${start.z.toFixed(2)}f\n#define START_DX ${start.dx.toFixed(4)}f\n#define START_DZ ${start.dz.toFixed(4)}f\n`;
writeFileSync(join(GAME, 'src/course_data.h'), out);
console.log(`track ${LEN.toFixed(0)} m, ${buoys.length} buoys, land ${(H.filter((v) => v > 0).length / H.length * 100).toFixed(1)}%`);

// grade the CC0 scans into this world's palette (albedo RGB height A; the normals are used as they are)
const SRC = resolve(GAME, '../skyfighter/assets/terrain');
const grades = { 'sand': ['sand', [1.28, 1.2, 1.02]], 'coast': ['coast', [0.72, 0.66, 0.56]], 'rock': ['rock', [0.52, 0.47, 0.45]], 'grass': ['grass', [0.42, 0.74, 0.32]] };
for (const [name, [src, g]] of Object.entries(grades)) {
  const a = readFileSync(join(SRC, src + '-albedo.rgba')), n = readFileSync(join(SRC, src + '-normal.rgba'));
  for (let k = 0; k < a.length; k += 4) for (let c = 0; c < 3; c++) a[k + c] = Math.min(255, a[k + c] * g[c]);
  writeFileSync(join(OUT, 'terrain', name + '-albedo.rgba'), a);
  writeFileSync(join(OUT, 'terrain', name + '-normal.rgba'), n);
}
