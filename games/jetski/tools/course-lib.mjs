// What every course shares: from a course definition (courses/<id>.mjs) to the files the game loads. A definition is a function
// of these helpers that returns:
//   id, name          the file prefix and the name on the menu
//   track             a closed loop of [x, z] control points, metres
//   height(x, z)      the ground, metres over the rest level of the sea
//   paint(p)          [base layer, over layer, blend 0..255] at p = { x, z, y, slope }
//   mask(p)           how free the swell is here, 0..1, p = { x, z, y, r0 }
//   maps              name: density 0..1 at p = { x, z, y, slope }, for the scatter
//   scatter           [model, map, density, scale min, scale max, visible range, seed, sink, tilt] per kind of thing
//   grades            [layer, scan, [r, g, b], uv scale]: four CC0 terrain scans graded to this place's palette
//   ramps             where on the track the ramps stand, metres; buoys { first, step, wobble, offset } optional
//   landmark          { model, where(x, z) }: stands on the highest ground where says
//   waves             [direction, wavelength, amplitude, amplitude per unit of swell, steepness] x 4
//   swell, tide       [start of the race, end of the race]
//   sky               the sky dome's painted image
//   props             [node, property, value as a scene file writes it]: the light, the weather, the water's colours
//   sun_disc          how bright the painted sun's disc is (1), for a sky that hides it
//   hook              what makes this place different (src/sim.c's hook_craft), optional; kind 1: an arch over a cut across the
//                     course, shut while the tide stands too high under its lintel, that takes a rider from s_in to s_out
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const HERE = dirname(fileURLToPath(import.meta.url)), GAME = resolve(HERE, '..'), OUT = join(GAME, 'assets');
export const QUADS = 896, SPACING = 2, SIZE = QUADS * SPACING, MINH = -48, MAXH = 170;
export const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
export function hash(x, y, s) { let h = (x * 374761393 + y * 668265263 + s * 1274126177) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }
function vnoise(x, y, s) {
  const xi = Math.floor(x), yi = Math.floor(y), u = x - xi, v = y - yi, su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v);
  return lerp(lerp(hash(xi, yi, s), hash(xi + 1, yi, s), su), lerp(hash(xi, yi + 1, s), hash(xi + 1, yi + 1, s), su), sv);
}
export const fbm = (x, y, oct, s) => { let a = 0.5, f = 1, sum = 0, n = 0; for (let i = 0; i < oct; i++) { sum += a * vnoise(x * f, y * f, s + i); n += a; a *= 0.5; f *= 2.03; } return sum / n; };
export const ridged = (x, y, oct, s) => { let a = 0.5, f = 1, sum = 0, n = 0; for (let i = 0; i < oct; i++) { sum += a * (1 - Math.abs(2 * vnoise(x * f, y * f, s + i) - 1)); n += a; a *= 0.5; f *= 2.1; } return sum / n; };

// the track from its control points: a closed Catmull-Rom curve, how far from it, a point along it
export function trackOf(P) {
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
  return { track, LEN, trackDist, pointAt };
}

const helpers = { clamp, lerp, smooth, hash, fbm, ridged, trackOf, SIZE };
export const load = async (id) => (await import(join(GAME, 'courses', id + '.mjs'))).default(helpers);
const q = (s) => JSON.stringify(s);

/** Everything a course's definition makes: its files under assets/, its data as C, a line of numbers. */
export function makeCourse(def) {
  const dir = join(OUT, 'course', def.id);
  mkdirSync(dir, { recursive: true });
  const { LEN, pointAt } = trackOf(def.track);
  const N = QUADS + 1, H = new Float32Array(N * N), toW = (i) => (i - QUADS / 2) * SPACING;
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) H[j * N + i] = def.height(toW(i), toW(j));
  const r16 = new Uint16Array(N * N);
  for (let k = 0; k < N * N; k++) r16[k] = Math.round(clamp((H[k] - MINH) / (MAXH - MINH)) * 65535);
  writeFileSync(join(dir, 'heights.r16'), Buffer.from(r16.buffer));

  // control words (cw_terrain_control)
  const ctl = new Uint32Array(N * N);
  const word = (base, over, blend, ang, scale) => ((base & 31) << 27 | (over & 31) << 22 | (blend & 255) << 14 | (ang & 15) << 10 | (scale & 7) << 7) >>> 0;
  const hAt = (i, j) => H[clamp(j, 0, QUADS) * N + clamp(i, 0, QUADS)];
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    const y = hAt(i, j), x = toW(i), z = toW(j);
    const nx = (hAt(i + 1, j) - hAt(i - 1, j)) / (2 * SPACING), nz = (hAt(i, j + 1) - hAt(i, j - 1)) / (2 * SPACING), slope = Math.hypot(nx, nz);
    const [base, over, blend] = def.paint({ x, z, y, slope });
    const h = hash(i >> 2, j >> 2, 61);
    ctl[j * N + i] = word(base, over, Math.round(clamp(blend, 0, 255)), Math.floor(h * 16), Math.floor(hash(i >> 3, j >> 3, 62) * 3));
  }
  writeFileSync(join(dir, 'control.u32'), Buffer.from(ctl.buffer));

  // the wave mask, softened
  const MN = 256, M = new Float32Array(MN * MN), hWorld = (x, z) => hAt(Math.round(x / SPACING + QUADS / 2), Math.round(z / SPACING + QUADS / 2));
  for (let j = 0; j < MN; j++) for (let i = 0; i < MN; i++) {
    const x = ((i + 0.5) / MN - 0.5) * SIZE, z = ((j + 0.5) / MN - 0.5) * SIZE;
    M[j * MN + i] = def.mask({ x, z, y: hWorld(x, z), r0: Math.hypot(x, z) });
  }
  for (let pass = 0; pass < 6; pass++) {
    const C = M.slice();
    for (let j = 0; j < MN; j++) for (let i = 0; i < MN; i++) {
      let s = 0;
      for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) s += C[clamp(j + b, 0, MN - 1) * MN + clamp(i + a, 0, MN - 1)] * (a ? 1 : 2) * (b ? 1 : 2);
      M[j * MN + i] = s / 16;
    }
  }
  writeFileSync(join(dir, 'wavemask.f32'), Buffer.from(M.buffer));

  // the scatter's density maps
  const DN = 448;
  for (const [name, fn] of Object.entries(def.maps)) {
    const D = new Uint8Array(DN * DN);
    for (let j = 0; j < DN; j++) for (let i = 0; i < DN; i++) {
      const x = ((i + 0.5) / DN - 0.5) * SIZE, z = ((j + 0.5) / DN - 0.5) * SIZE, y = hWorld(x, z);
      const ii = Math.round(x / SPACING + QUADS / 2), jj = Math.round(z / SPACING + QUADS / 2);
      const slope = Math.hypot(hAt(ii + 2, jj) - hAt(ii - 2, jj), hAt(ii, jj + 2) - hAt(ii, jj - 2)) / (4 * SPACING);
      D[j * DN + i] = Math.round(fn({ x, z, y, slope }) * 255);
    }
    writeFileSync(join(dir, name + '.u8'), Buffer.from(D));
  }

  // buoys: pairs of slaloms, alternating sides, spaced so the rhythm reads
  const bd = { first: 70, step: 88, wobble: 14, offset: 8.5, ...(def.buoys || {}) };
  const buoys = [];
  let side = 1;
  for (let s = bd.first; s < LEN - 60;) {
    const p = pointAt(s);
    buoys.push({ x: p.x - p.dz * side * bd.offset, z: p.z + p.dx * side * bd.offset, side, s });
    side = -side;
    s += bd.step + bd.wobble * Math.sin(s * 0.013);
  }
  // the landmark: on the highest ground its place allows
  let lh = [0, -100, 0];
  if (def.landmark) for (let j = 0; j < N; j += 2) for (let i = 0; i < N; i += 2) { const x = toW(i), z = toW(j), y = H[j * N + i]; if (def.landmark.where(x, z) && y > lh[1]) lh = [x, y, z]; }
  const start = pointAt(LEN - 28);
  const TK = []; for (let s = 0; s < LEN - 0.01; s += LEN / 256) { const p = pointAt(s); TK.push(p); }

  // this place's palette from the CC0 scans (albedo RGB height A; the normals are used as they are)
  const SRC = resolve(GAME, '../skyfighter/assets/terrain');
  for (const [name, src, g] of def.grades) {
    const a = readFileSync(join(SRC, src + '-albedo.rgba')), n = readFileSync(join(SRC, src + '-normal.rgba'));
    for (let k = 0; k < a.length; k += 4) for (let c = 0; c < 3; c++) a[k + c] = Math.min(255, a[k + c] * g[c]);
    writeFileSync(join(dir, name + '-albedo.rgba'), a);
    writeFileSync(join(dir, name + '-normal.rgba'), n);
  }

  // the data as C
  const id = def.id;
  const f = (v) => (Number.isInteger(v) ? v + '.0f' : v + 'f');
  let c = `/* generated by tools/make-course.mjs from courses/${id}.mjs */\n`;
  c += `static const float ${id}_track[][4] = {\n` + TK.map((p) => `{${p.x.toFixed(2)}f,${p.z.toFixed(2)}f,${p.dx.toFixed(4)}f,${p.dz.toFixed(4)}f}`).join(',\n') + '\n};\n';
  c += `static const float ${id}_buoys[][4] = { /* x, z, side (+1: pass with the buoy on your right), s */\n` + buoys.map((b) => `{${b.x.toFixed(2)}f,${b.z.toFixed(2)}f,${b.side}.0f,${b.s.toFixed(1)}f}`).join(',\n') + '\n};\n';
  c += `static const float ${id}_ramps[] = { ${def.ramps.map(f).join(', ')} };\n`;
  c += `static const course_scatter ${id}_scatter[] = {\n` + def.scatter.map(([m, map, d, a, b, r, seed, sink, tilt]) => `{ ${q(`res://models/${m}.glb`)}, ${q(`course/${id}/${map}.u8`)}, ${f(d)}, ${f(a)}, ${f(b)}, ${f(r)}, ${seed}, ${sink}, ${f(tilt)} }`).join(',\n') + '\n};\n';
  c += `static const char *const ${id}_props[][3] = {\n` + (def.props.length ? def.props.map((p) => `{ ${q(p[0])}, ${q(p[1])}, ${q(String(p[2]))} }`).join(',\n') : '{ "", "", "" }') + '\n};\n';
  c += `static const course_def ${id}_course = {\n  .id = ${q(id)}, .name = ${q(def.name)}, .quads = ${QUADS}, .spacing = ${f(SPACING)}, .minh = ${f(MINH)}, .maxh = ${f(MAXH)}, .len = ${LEN.toFixed(2)}f,\n`;
  c += `  .track_n = ${TK.length}, .track = ${id}_track, .buoy_n = ${buoys.length}, .buoys = ${id}_buoys, .ramp_n = ${def.ramps.length}, .ramp_s = ${id}_ramps,\n`;
  c += `  .scatter_n = ${def.scatter.length}, .scatter = ${id}_scatter, .prop_n = ${def.props.length}, .props = ${id}_props,\n`;
  c += `  .layer = { ${def.grades.map((g) => q(`course/${id}/${g[0]}`)).join(', ')} }, .layer_uv = { ${def.grades.map((g) => f(g[3])).join(', ')} },\n`;
  c += `  .heights = ${q(`course/${id}/heights.r16`)}, .control = ${q(`course/${id}/control.u32`)}, .mask = ${q(`course/${id}/wavemask.f32`)}, .mask_n = ${MN}, .map_n = ${DN}, .sky = ${q(def.sky)},\n`;
  c += `  .landmark = ${def.landmark ? q(`res://models/${def.landmark.model}.glb`) : 'NULL'}, .landmark_at = { ${lh[0].toFixed(2)}f, ${(lh[1] - 0.4).toFixed(2)}f, ${lh[2].toFixed(2)}f },\n`;
  c += `  .start = { ${start.x.toFixed(2)}f, ${start.z.toFixed(2)}f, ${start.dx.toFixed(4)}f, ${start.dz.toFixed(4)}f },\n`;
  c += `  .waves = { ${def.waves.map((w) => `{ ${w.map(f).join(', ')} }`).join(', ')} }, .swell = { ${f(def.swell[0])}, ${f(def.swell[1])} }, .tide = { ${f(def.tide[0])}, ${f(def.tide[1])} },\n`;
  const hk = def.hook;
  c += `  .sun_disc = ${f(def.sun_disc ?? 1)},\n`;
  if (hk) c += `  .hook = { .kind = ${hk.kind}, .x = ${hk.x.toFixed(2)}f, .z = ${hk.z.toFixed(2)}f, .dirx = ${hk.dirx.toFixed(4)}f, .dirz = ${hk.dirz.toFixed(4)}f, .half_w = ${f(hk.half_w)}, .half_len = ${hk.half_len.toFixed(2)}f, .lintel = ${f(hk.lintel)}, .s_in = ${f(hk.s_in)}, .s_out = ${f(hk.s_out)}, .model = ${q(`res://models/${hk.model}.glb`)} },\n`;
  c += '};\n';
  mkdirSync(join(GAME, 'src/courses'), { recursive: true });
  writeFileSync(join(GAME, 'src/courses', id + '.h'), c);
  const land = H.filter((v) => v > 0).length / H.length * 100;
  console.log(`${id}: track ${LEN.toFixed(0)} m, ${buoys.length} buoys, ${def.ramps.length} ramps, land ${land.toFixed(1)}%`);
  return { H, LEN, buoys, pointAt };
}

/** The list of courses for the game and the pack's file list: src/courses/all.c, and assets/roots.txt's course lines. */
export function writeIndex(defs) {
  let c = '/* generated by tools/make-course.mjs */\n#include "../game.h"\n#include <stddef.h>\n';
  for (const d of defs) c += `#include "${d.id}.h"\n`;
  c += `const course_def *const COURSES[LEVELS] = { ${defs.map((d) => '&' + d.id + '_course').join(', ')} };\n`;
  writeFileSync(join(GAME, 'src/courses/all.c'), c);
  const rootsPath = join(OUT, 'roots.txt');
  const keep = readFileSync(rootsPath, 'utf8').split('\n').filter((l) => l && !l.startsWith('course/') && !/^terrain\//.test(l));
  for (const d of defs) {
    for (const n of ['heights.r16', 'control.u32', 'wavemask.f32', ...Object.keys(d.maps).map((m) => m + '.u8'), ...d.grades.flatMap((g) => [g[0] + '-albedo.rgba', g[0] + '-normal.rgba'])]) keep.push(`course/${d.id}/${n}`);
    { const sky = d.sky.replace('res://', ''); if (!keep.includes(sky)) keep.push(sky); }
    if (d.landmark && !keep.includes(`models/${d.landmark.model}.glb`)) keep.push(`models/${d.landmark.model}.glb`);
    for (const s of d.scatter) if (!keep.includes(`models/${s[0]}.glb`)) keep.push(`models/${s[0]}.glb`);
    if (d.hook && !keep.includes(`models/${d.hook.model}.glb`)) keep.push(`models/${d.hook.model}.glb`);
  }
  writeFileSync(rootsPath, keep.join('\n') + '\n');
}
