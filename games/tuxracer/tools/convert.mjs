#!/usr/bin/env node
// Converts the original Tux Racer 0.61 data (the repository's deps/tuxracer, fetched by
// scripts/fetch-tuxracer.sh) into this game's assets/. Deterministic: a
// second run changes nothing. Everything written here is GPL-2.0-or-later
// content from the original game (see NOTICE).
import { readFileSync, writeFileSync, mkdirSync, readdirSync, existsSync, copyFileSync, statSync } from 'node:fs';
import { dirname, resolve, join, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readSgi, toRgbaTopDown } from './lib/sgi-rgb.mjs';
import { TclMini } from './lib/tcl-mini.mjs';
import { writePng, readImage } from '../../../deps/cartwheel-engine/tools/lib/png.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const data = resolve(root, '../../deps/tuxracer/tuxracer-data-0.61.orig');
const out = resolve(root, 'assets');
if (!existsSync(data)) { console.error('run scripts/fetch-tuxracer.sh first'); process.exit(1); }
const SPACING = 1;   // metres between terrain samples
const GRID = 32;     // Terrain3D grid_quads: quad counts are rounded up to this
const wrote = [];
function emit(rel, buf) { const p = join(out, rel); mkdirSync(dirname(p), { recursive: true }); writeFileSync(p, buf); wrote.push(rel); }
function emitJson(rel, obj) { emit(rel, JSON.stringify(obj, null, 1) + '\n'); }
function png(rel, img) { const p = join(out, rel); mkdirSync(dirname(p), { recursive: true }); writePng(p, img.w, img.h, toRgbaTopDown(img)); wrote.push(rel); }

// ── object types (courses/common/courseinit.tcl) ─────────────────────────
const TREE_TYPES = [
  { name: 'tree3', kind: 'tree', diam: 1.4, height: 1.0, vary: 0.5, color: [0, 255, 48], texture: 'shrub' },
  { name: 'tree1', kind: 'tree', diam: 1.4, height: 2.5, vary: 0.5, color: [255, 255, 255], texture: 'tree' },
  { name: 'tree2', kind: 'tree', diam: 1.4, height: 2.5, vary: 0.5, color: [255, 96, 0], texture: 'tree_barren' },
];
const ITEM_TYPES = [
  { name: 'herring', kind: 'item', diam: 1.0, height: 1.0, color: [28, 185, 204], above_ground: 0.2, texture: 'fish' },
  { name: 'flag', kind: 'item', diam: 1.0, height: 1.0, color: [194, 40, 40], nocollision: true, texture: 'flag1' },
  { name: 'finish', kind: 'item', diam: 9.0, height: 6.0, color: [255, 255, 0], nocollision: true, normal: [0, 0, 1], texture: 'finish' },
  { name: 'start', kind: 'item', diam: 9.0, height: 6.0, color: [128, 128, 0], nocollision: true, normal: [0, 0, 1], texture: 'start' },
  { name: 'float', kind: 'item', diam: 2.0, height: 1.0, color: [255, 128, 255], nocollision: true, reset_point: true },
];
const FRICTION = { ice: 0.22, rock: 0.9, snow: 0.35 }; // tux_friction in courseinit.tcl

// ── courses ───────────────────────────────────────────────────────────────
function parseCourseTcl(path) {
  const c = { name: basename(dirname(path)), author: 'Unknown', base_height: 127, par_time: 120 };
  const tcl = new TclMini((cmd, a) => {
    switch (cmd) {
      case 'tux_course_name': c.title = a[0]; break;
      case 'tux_course_author': c.author = a[0]; break;
      case 'tux_course_dim': c.width = +a[0]; c.length = +a[1]; c.play_width = a[2] ? +a[2] : +a[0]; c.play_length = a[3] ? +a[3] : +a[1]; break;
      case 'tux_start_pt': c.start = [+a[0], +a[1]]; break;
      case 'tux_angle': c.angle = +a[0]; break;
      case 'tux_elev_scale': c.elev_scale = +a[0]; break;
      case 'tux_base_height_value': c.base_height = +a[0]; break;
      case 'tux_elev': c.elev = a[0]; break;
      case 'tux_terrain': c.terrain = a[0]; break;
      case 'tux_par_time': c.par_time = +a[0]; break;
      case 'tux_course_init': break;
      default: break;
    }
  });
  tcl.run(readFileSync(path, 'utf8'));
  // start_pt_cb: out-of-range coordinates fall back to 0
  if (!(c.start[0] > 0 && c.start[0] < c.width)) c.start[0] = 0;
  if (!(c.start[1] > 0 && c.start[1] < c.length)) c.start[1] = 0;
  return c;
}

function convertCourse(dir, rel, index) {
  const c = parseCourseTcl(join(dir, 'course.tcl'));
  const elev = readSgi(join(dir, c.elev)), terr = readSgi(join(dir, c.terrain));
  if (terr.w !== elev.w || terr.h !== elev.h) throw new Error(`${rel}: terrain bitmap size differs from elev`);
  const nx = elev.w, ny = elev.h;
  const slope = Math.tan(c.angle * Math.PI / 180);
  // course_load.c: ELEV(nx-1-x, ny-1-y) from image data (row 0 at the bottom)
  const ELEV = new Float64Array(nx * ny), TYPE = new Uint8Array(nx * ny);
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    const i = nx - 1 - x, j = ny - 1 - y, idx = i + nx * j;
    ELEV[idx] = (elev.data[(x + nx * y) * elev.ch] - c.base_height) / 255 * c.elev_scale - (ny - 1 - y) / ny * c.length * slope;
    const v = terr.data[(x + nx * y) * terr.ch];
    const d = [Math.abs(v - 0), Math.abs(v - 127), Math.abs(v - 255)]; // ice, rock, snow
    TYPE[idx] = d[0] <= d[1] && d[0] <= d[2] ? 0 : d[1] <= d[2] ? 1 : 2;
  }
  // phys_sim.c get_indices_for_point: xidx = x / W * (nx - 1), yidx = d / L * (ny - 1)
  const sampleHeight = (x, d) => {
    let xi = Math.min(Math.max(x / c.width * (nx - 1), 0), nx - 1), yi = Math.min(Math.max(d / c.length * (ny - 1), 0), ny - 1);
    let x0 = Math.floor(xi), y0 = Math.floor(yi), x1 = Math.min(x0 + 1, nx - 1), y1 = Math.min(y0 + 1, ny - 1);
    const fx = xi - x0, fy = yi - y0;
    const h00 = ELEV[x0 + nx * y0], h10 = ELEV[x1 + nx * y0], h01 = ELEV[x0 + nx * y1], h11 = ELEV[x1 + nx * y1];
    return (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy;
  };
  const sampleType = (x, d) => {
    const xi = Math.round(Math.min(Math.max(x / c.width * (nx - 1), 0), nx - 1)), yi = Math.round(Math.min(Math.max(d / c.length * (ny - 1), 0), ny - 1));
    return TYPE[xi + nx * yi];
  };
  const qx = Math.ceil(Math.ceil(c.width / SPACING) / GRID) * GRID, qz = Math.ceil(Math.ceil(c.length / SPACING) / GRID) * GRID;
  const sx = qx + 1, sz = qz + 1;
  const H = new Float64Array(sx * sz), T = new Uint8Array(sx * sz);
  let hmin = Infinity, hmax = -Infinity;
  for (let j = 0; j < sz; j++) for (let i = 0; i < sx; i++) {
    const d = (qz - j) * SPACING; // row 0 is the far end: rows run toward +z, the course runs toward -z
    const h = sampleHeight(i * SPACING, d);
    H[i + sx * j] = h; T[i + sx * j] = sampleType(i * SPACING, d);
    if (h < hmin) hmin = h; if (h > hmax) hmax = h;
  }
  hmin = Math.floor(hmin - 1); hmax = Math.ceil(hmax + 1);
  const Q = new Uint16Array(sx * sz);
  for (let k = 0; k < sx * sz; k++) Q[k] = Math.round((H[k] - hmin) / (hmax - hmin) * 65535);
  emit(`courses/${rel}/heights.u16`, Buffer.from(Q.buffer));
  emit(`courses/${rel}/types.u8`, Buffer.from(T.buffer));
  // objects (trees_cb): pos.x = (sx-x)/(sx-1)*W, d = (sy-y)/(sy-1)*L, row 0 at the bottom
  const objects = [];
  if (existsSync(join(dir, 'trees.rgb'))) {
    const tr = readSgi(join(dir, 'trees.rgb'));
    const nearest = (types, px) => {
      let best = px[0] + px[1] + px[2], which = null;
      for (const t of types) { const d = Math.abs(t.color[0] - px[0]) + Math.abs(t.color[1] - px[1]) + Math.abs(t.color[2] - px[2]); if (d < best) { best = d; which = t; } }
      return [best, which];
    };
    for (let y = 0; y < tr.h; y++) for (let x = 0; x < tr.w; x++) {
      const px = [0, 1, 2].map(k => tr.data[(x + y * tr.w) * tr.ch + (tr.ch >= 3 ? k : 0)]);
      const [td, tt] = nearest(TREE_TYPES, px), [id, it] = nearest(ITEM_TYPES, px);
      const wx = (tr.w - x) / (tr.w - 1) * c.width, wd = (tr.h - y) / (tr.h - 1) * c.length;
      if (td < id && tt) objects.push({ t: tt.name, x: +wx.toFixed(3), d: +wd.toFixed(3) });
      else if (it) objects.push({ t: it.name, x: +wx.toFixed(3), d: +wd.toFixed(3) });
    }
  }
  // the original lists trees type by type, then items, in image order
  objects.sort((a, b) => kindOrder(a.t) - kindOrder(b.t));
  emitJson(`courses/${rel}/objects.json`, objects);
  if (existsSync(join(dir, 'preview.rgb'))) png(`courses/${rel}/preview.png`, readSgi(join(dir, 'preview.rgb')));
  const info = {
    dir: rel, title: index?.name ?? c.title ?? c.name, author: c.author, description: index?.description ?? '',
    par_time: index?.par_time ?? c.par_time,
    width: c.width, length: c.length, play_width: c.play_width, play_length: c.play_length,
    start: c.start, angle: c.angle, elev_scale: c.elev_scale, base_height: c.base_height,
    spacing: SPACING, samples: [sx, sz], min_height: hmin, max_height: hmax,
    source: [nx, ny], objects: objects.length, friction: FRICTION,
  };
  emitJson(`courses/${rel}/course.json`, info);
  // course.txt: one "key value" per line for the C loader (strings last on their line)
  emit(`courses/${rel}/course.txt`, [
    `width ${info.width}`, `length ${info.length}`, `play_width ${info.play_width}`, `play_length ${info.play_length}`,
    `start ${info.start[0]} ${info.start[1]}`, `angle ${info.angle}`, `samples ${sx} ${sz}`, `spacing ${SPACING}`,
    `min_height ${hmin}`, `max_height ${hmax}`, `par_time ${info.par_time}`, `objects ${objects.length}`,
    `friction ${FRICTION.ice} ${FRICTION.rock} ${FRICTION.snow}`, `title ${info.title}`, `author ${info.author}`, `description ${info.description}`, ''].join('\n'));
  // objects.bin: uint8 type index (trees then items, object_types order), 3 pad bytes, float32 x, float32 d
  const ob = Buffer.alloc(objects.length * 12);
  objects.forEach((o, i) => { ob.writeUInt8(typeIndex(o.t), i * 12); ob.writeFloatLE(o.x, i * 12 + 4); ob.writeFloatLE(o.d, i * 12 + 8); });
  emit(`courses/${rel}/objects.bin`, ob);
  return info;
}
function typeIndex(t) { const i = TREE_TYPES.findIndex(x => x.name === t); return i >= 0 ? i : TREE_TYPES.length + ITEM_TYPES.findIndex(x => x.name === t); }
function kindOrder(t) { const i = TREE_TYPES.findIndex(x => x.name === t); return i >= 0 ? i : 10 + ITEM_TYPES.findIndex(x => x.name === t); }

// course_idx.tcl: the open courses and the cup, by regex (the file is a Tcl program)
function parseCourseIndex() {
  const src = readFileSync(join(data, 'courses/course_idx.tcl'), 'utf8').replace(/\\\n/g, ' ');
  const open = [];
  const openBlock = src.slice(src.indexOf('tux_open_courses'), src.indexOf('tux_events'));
  for (const m of openBlock.matchAll(/-course\s+(\S+)\s+-name\s+"([^"]*)"\s+-description\s+"([^"]*)"\s+-par_time\s+([\d.]+)/g))
    open.push({ dir: m[1], name: m[2], description: m[3], par_time: +m[4] });
  const cups = [];
  const evBlock = src.slice(src.indexOf('tux_events'));
  for (const cm of evBlock.matchAll(/-name\s+"([^"]*)"\s+-icon\s+cup_icon\s+-races\s+\{([\s\S]*?)\}\s*\}\s*\}/g)) {
    const races = [];
    for (const r of cm[2].matchAll(/-course\s+(\S+)\s+-name\s+"([^"]*)"\s+-description\s+"([^"]*)"\s+-herring\s+\{([^}]*)\}\s+-time\s+\{([^}]*)\}\s+-score\s+\{([^}]*)\}\s+-mirrored\s+(\S+)\s+-conditions\s+(\S+)\s+-windy\s+(\S+)\s+-snowing\s+(\S+)/g))
      races.push({ dir: r[1], name: r[2], description: r[3], herring: r[4].trim().split(/\s+/).map(Number), time: r[5].trim().split(/\s+/).map(Number),
        score: r[6].trim().split(/\s+/).map(Number), mirrored: r[7] === 'yes', conditions: r[8], windy: r[9] === 'yes', snowing: r[10] === 'yes' });
    cups.push({ name: cm[1], races });
  }
  return { open, cups };
}

// ── Tux (tux.tcl): a scene graph of transforms and unit spheres ───────────
// Matrices are GL column-major (e[col*4+row]); every command post-multiplies
// the node's matrix as hier.c does, so the product is in command order.
const I4 = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
function mul(a, b) { const r = new Array(16).fill(0); for (let c = 0; c < 4; c++) for (let rr = 0; rr < 4; rr++) for (let k = 0; k < 4; k++) r[c * 4 + rr] += a[k * 4 + rr] * b[c * 4 + k]; return r; }
function rot(axis, deg) { const s = Math.sin(deg * Math.PI / 180), c = Math.cos(deg * Math.PI / 180), m = I4();
  if (axis === 'x') { m[5] = c; m[9] = -s; m[6] = s; m[10] = c; }
  else if (axis === 'y') { m[0] = c; m[8] = s; m[2] = -s; m[10] = c; }
  else { m[0] = c; m[4] = -s; m[1] = s; m[5] = c; }
  return m; }
function trans(x, y, z) { const m = I4(); m[12] = x; m[13] = y; m[14] = z; return m; }
function scl(x, y, z) { const m = I4(); m[0] = x; m[5] = y; m[10] = z; return m; }
const nums = s => s.trim().split(/\s+/).map(Number);

function convertTux() {
  const nodes = new Map(), order = [], materials = {}, joints = {};
  let rootName = null;
  const node = n => { const x = nodes.get(n); if (!x) throw new Error(`tux.tcl: unknown node ${n}`); return x; };
  const tcl = new TclMini((cmd, a) => {
    switch (cmd) {
      case 'tux_root_node': rootName = a[0]; break;
      case 'tux_material': materials[a[0]] = { diffuse: nums(a[1]), specular: nums(a[2]), exp: +a[3] }; break;
      case 'tux_transform': case 'tux_sphere': {
        const name = (a[0] === ':' ? '' : a[0]) + ':' + a[1];
        const n = { name, parent: a[0] === ':' ? null : a[0], kind: cmd === 'tux_sphere' ? 'sphere' : 'transform', matrix: I4(), material: null, shadow: true, resolution: cmd === 'tux_sphere' ? +a[2] : 0 };
        nodes.set(name, n); order.push(n); break; }
      case 'tux_rotate': { const n = node(a[0]); n.matrix = mul(n.matrix, rot(a[1], +a[2])); break; }
      case 'tux_translate': { const n = node(a[0]); const [x, y, z] = nums(a[1]); n.matrix = mul(n.matrix, trans(x, y, z)); break; }
      case 'tux_scale': { const n = node(a[0]); const [cx, cy, cz] = nums(a[1]), [sx, sy, sz] = nums(a[2]);
        n.matrix = mul(mul(mul(n.matrix, trans(-cx, -cy, -cz)), scl(sx, sy, sz)), trans(cx, cy, cz)); break; }
      case 'tux_surfaceproperty': node(a[0]).material = a[1]; break;
      case 'tux_shadow': node(a[0]).shadow = a[1] === 'on'; break;
      case 'tux_eye': node(a[0]).eye = a[1]; break;
      case 'tux_neck': case 'tux_head': case 'tux_tail': case 'tux_left_shoulder': case 'tux_right_shoulder':
      case 'tux_left_hip': case 'tux_right_hip': case 'tux_left_knee': case 'tux_right_knee': case 'tux_left_ankle': case 'tux_right_ankle':
        joints[cmd.slice(4)] = a[0]; break;
      default: throw new Error(`tux.tcl: unhandled ${cmd}`);
    }
  });
  tcl.run(readFileSync(join(data, 'tux.tcl'), 'utf8'));
  // materials inherit down the tree (hier_util.c draw)
  for (const n of order) if (!n.material && n.parent) n.material = nodes.get(n.parent)?.material ?? null;
  const round = m => m.map(v => +v.toFixed(6) + 0);
  emitJson('model/tux.json', { root: rootName, joints, materials, nodes: order.map(n => ({ name: n.name, parent: n.parent, kind: n.kind, matrix: round(n.matrix), material: n.material, shadow: n.shadow, resolution: n.resolution, eye: n.eye })) });
  // tux_walk.tcl: time x y z yaw pitch l_shldr r_shldr l_hip r_hip (keyframe.c)
  const frames = [];
  for (const m of readFileSync(join(data, 'courses/common/tux_walk.tcl'), 'utf8').matchAll(/^\s*tux_key_frame\s+(.*)$/gm)) {
    const v = nums(m[1]); frames.push({ time: v[0], pos: [v[1], v[2], v[3]], yaw: v[4], pitch: v[5], l_shoulder: v[6], r_shoulder: v[7], l_hip: v[8], r_hip: v[9] });
  }
  emitJson('model/tux_walk.json', frames);
  // tux.txt for the C loader: "material <name> dr dg db sr sg sb exp" lines, then
  // "node <index> <parent index|-1> <sphere|transform> <material index|-1> <shadow 0|1> <16 matrix values>",
  // then "joint <name> <node index>"
  const matNames = Object.keys(materials);
  const idx = new Map(order.map((n, i) => [n.name, i]));
  const lines = [];
  for (const m of matNames) { const v = materials[m]; lines.push(`material ${m} ${v.diffuse.join(' ')} ${v.specular.join(' ')} ${v.exp}`); }
  order.forEach((n, i) => lines.push(`node ${i} ${n.parent ? idx.get(n.parent) : -1} ${n.kind} ${n.material ? matNames.indexOf(n.material) : -1} ${n.shadow ? 1 : 0} ${round(n.matrix).join(' ')}`));
  for (const [j, name] of Object.entries(joints)) lines.push(`joint ${j} ${idx.get(name)}`);
  emit('model/tux.txt', lines.join('\n') + '\n');
  emit('model/tux_walk.txt', frames.map(f => [f.time, ...f.pos, f.yaw, f.pitch, f.l_shoulder, f.r_shoulder, f.l_hip, f.r_hip].join(' ')).join('\n') + '\n');
  return order.length;
}

// ── run ───────────────────────────────────────────────────────────────────
const index = parseCourseIndex();
const byDir = Object.fromEntries(index.open.map(o => [o.dir, o]));
const courses = [];
const stock = readdirSync(join(data, 'courses')).filter(d => !['common', 'contrib'].includes(d) && statSync(join(data, 'courses', d)).isDirectory());
for (const d of stock) courses.push(convertCourse(join(data, 'courses', d), d, byDir[d]));
for (const d of readdirSync(join(data, 'courses/contrib')).filter(d => statSync(join(data, 'courses/contrib', d)).isDirectory()))
  courses.push(convertCourse(join(data, 'courses/contrib', d), `contrib/${d}`, byDir[`contrib/${d}`]));
// the menu order: the open list first (as course_idx.tcl lists it), then the rest
const orderOf = dir => { const i = index.open.findIndex(o => o.dir === dir); return i >= 0 ? i : 100; };
courses.sort((a, b) => orderOf(a.dir) - orderOf(b.dir) || a.dir.localeCompare(b.dir));
emitJson('courses/courses.json', courses.map(c => ({ dir: c.dir, title: c.title, author: c.author, description: c.description, par_time: c.par_time, length: c.length, width: c.width, objects: c.objects, contrib: c.dir.startsWith('contrib/') })));
emitJson('courses/cups.json', index.cups);
// courses.txt: "course <dir> <par> <contrib 0|1> <length> | <title> | <description>"; cups.txt: "cup <name>" then "race <dir> <h0 h1 h2 h3> <t0 t1 t2 t3> <s0..s3> <mirrored> <conditions> <windy> <snowing>"
emit('courses/courses.txt', courses.map(c => `course ${c.dir} ${c.par_time} ${c.dir.startsWith('contrib/') ? 1 : 0} ${c.length} | ${c.title} | ${c.description}`).join('\n') + '\n');
emit('courses/cups.txt', index.cups.flatMap(cup => [`cup ${cup.name}`, ...cup.races.map(r => `race ${r.dir} ${r.herring.join(' ')} ${r.time.join(' ')} ${r.score.join(' ')} ${r.mirrored ? 1 : 0} ${r.conditions} ${r.windy ? 1 : 0} ${r.snowing ? 1 : 0}`)]).join('\n') + '\n');
emitJson('courses/object_types.json', { trees: TREE_TYPES, items: ITEM_TYPES });
const tuxNodes = convertTux();
{
  // tree_polyhedron.tcl: set tree_poly {{vertices} {polygons}}; the engine builds a convex hull from the vertices
  const src = readFileSync(join(data, 'courses/common/tree_polyhedron.tcl'), 'utf8');
  const verts = [];
  const body = src.slice(src.indexOf('{{') + 2);
  const vblock = body.slice(0, body.indexOf('} {'));
  for (const m of vblock.matchAll(/\{\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\}/g)) verts.push([+m[1], +m[2], +m[3]]);
  emit('model/tree_poly.txt', verts.map(v => v.join(' ')).join('\n') + '\n');
}
// images: the common course art, HUD art and sky cubes as PNG references
for (const f of readdirSync(join(data, 'courses/common')).filter(f => f.endsWith('.rgb'))) png(`original/common/${f.replace(/\.rgb$/, '.png')}`, readSgi(join(data, 'courses/common', f)));
for (const f of readdirSync(join(data, 'textures')).filter(f => f.endsWith('.rgb'))) png(`original/textures/${f.replace(/\.rgb$/, '.png')}`, readSgi(join(data, 'textures', f)));
// sky cubes: the six faces of sunny / cloudy / night resampled to one size, in three.js
// order (+x right, -x left, +y top, -y bottom, +z back, -z front), raw RGBA for t3_cube_texture_new
{
  const SIZE = 256;
  const resample = (img, size) => {
    const src = toRgbaTopDown(img), out = new Uint8Array(size * size * 4);
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
      const sx = (size - 1 - x + 0.5) / size * img.w - 0.5, sy = (y + 0.5) / size * img.h - 0.5; // x mirrored: CubeTexture's convention
      const x0 = Math.max(0, Math.floor(sx)), y0 = Math.max(0, Math.floor(sy)), x1 = Math.min(img.w - 1, x0 + 1), y1 = Math.min(img.h - 1, y0 + 1);
      const fx = Math.min(1, Math.max(0, sx - x0)), fy = Math.min(1, Math.max(0, sy - y0));
      for (let c = 0; c < 4; c++) {
        const a = src[(x0 + y0 * img.w) * 4 + c], b = src[(x1 + y0 * img.w) * 4 + c], cc = src[(x0 + y1 * img.w) * 4 + c], d = src[(x1 + y1 * img.w) * 4 + c];
        out[(x + y * size) * 4 + c] = Math.round((a * (1 - fx) + b * fx) * (1 - fy) + (cc * (1 - fx) + d * fx) * fy);
      }
    }
    return out;
  };
  for (const cond of ['sunny', 'cloudy', 'night']) {
    const faces = ['right', 'left', 'top', 'bottom', 'back', 'front'].map((f) => resample(readSgi(join(data, `courses/common/${cond}${f}.rgb`)), SIZE));
    emit(`sky/${cond}.cube`, Buffer.concat(faces.map((f) => Buffer.from(f.buffer))));
  }
}
// terrain tiles: the original snow / ice / rock tiles plus maps derived from
// their own luminance (Terrain3D's packing): albedo RGB + height A, and a
// normal map RGB (+Y up) + roughness A. Derived data, not new art.
{
  const TILES = { ice: { rough: 0.10, vary: 0.08, relief: 2.0, blur: 1 }, rock: { rough: 0.88, vary: 0.08, relief: 6.0, blur: 1 }, snow: { rough: 0.55, vary: 0.15, relief: 3.0, blur: 2 } };
  for (const [name, t] of Object.entries(TILES)) {
    const img = readImage(join(out, `original/common/${name}.png`));
    const { w, h, rgba } = img;
    const lum = new Float32Array(w * h);
    for (let i = 0; i < w * h; i++) lum[i] = (0.299 * rgba[i * 4] + 0.587 * rgba[i * 4 + 1] + 0.114 * rgba[i * 4 + 2]) / 255;
    // a little blur (tiling) so the normals follow shapes, not film grain
    let hgt = lum;
    for (let pass = 0; pass < t.blur; pass++) {
      const nxt = new Float32Array(w * h);
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        let sum = 0;
        for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) sum += hgt[((x + dx + w) % w) + ((y + dy + h) % h) * w];
        nxt[x + y * w] = sum / 9;
      }
      hgt = nxt;
    }
    let lo = 1, hi = 0;
    for (const v of hgt) { if (v < lo) lo = v; if (v > hi) hi = v; }
    const ah = new Uint8Array(w * h * 4), nr = new Uint8Array(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = x + y * w, H = (hgt[i] - lo) / Math.max(1e-6, hi - lo);
      ah[i * 4] = rgba[i * 4], ah[i * 4 + 1] = rgba[i * 4 + 1], ah[i * 4 + 2] = rgba[i * 4 + 2], ah[i * 4 + 3] = Math.round(H * 255);
      const gx = hgt[((x + 1) % w) + y * w] - hgt[((x + w - 1) % w) + y * w], gy = hgt[x + ((y + 1) % h) * w] - hgt[x + ((y + h - 1) % h) * w];
      let nx = -gx * t.relief, ny = gy * t.relief, nz = 1; // +Y up in the OpenGL convention (image rows run down)
      const inv = 1 / Math.sqrt(nx * nx + ny * ny + nz * nz);
      nx *= inv, ny *= inv, nz *= inv;
      nr[i * 4] = Math.round((nx * 0.5 + 0.5) * 255), nr[i * 4 + 1] = Math.round((ny * 0.5 + 0.5) * 255), nr[i * 4 + 2] = Math.round((nz * 0.5 + 0.5) * 255);
      nr[i * 4 + 3] = Math.round(Math.min(1, Math.max(0, t.rough + (H - 0.5) * 2 * t.vary)) * 255);
    }
    mkdirSync(join(out, 'tiles'), { recursive: true });
    writePng(join(out, `tiles/${name}_ah.png`), w, h, ah); wrote.push(`tiles/${name}_ah.png`);
    writePng(join(out, `tiles/${name}_nr.png`), w, h, nr); wrote.push(`tiles/${name}_nr.png`);
  }
}
// sounds and music as they are (the engine plays .wav and .it)
for (const f of readdirSync(join(data, 'sounds')).filter(f => f.endsWith('.wav'))) { mkdirSync(join(out, 'sounds'), { recursive: true }); copyFileSync(join(data, 'sounds', f), join(out, 'sounds', f)); wrote.push(`sounds/${f}`); }
for (const f of readdirSync(join(data, 'music')).filter(f => f.endsWith('.it'))) { mkdirSync(join(out, 'music'), { recursive: true }); copyFileSync(join(data, 'music', f), join(out, 'music', f)); wrote.push(`music/${f}`); }
// roots.txt: what cwpack packs (everything the game reads at run time)
// plus the enhancement phase's own assets (not from the original data): the solid trees
// (tools/extract-trees.mjs from sky-mesa) and their textures
const extra = ['model/trees.bin', 'textures/tree-snow-v1.jpg', 'textures/tree-bark-v1.jpg', 'luts/sunny.cube', 'luts/cloudy.cube', 'luts/night.cube', 'sky/sunny_ring.png', 'sky/clouds.png', 'sky/snowdetail.png', 'textures/sprig.png'];
const packed = ['project.cwproj', 'credits.txt', ...readdirSync(join(out, 'fonts')).map(f => `fonts/${f}`),
  ...wrote.filter(f => !f.endsWith('.json')), ...extra];
emit('roots.txt', '# tuxracer: what cwpack packs. Generated by tools/convert.mjs from the original\n# Tux Racer 0.61 data (GPL v2, see NOTICE) plus the fonts (OFL, see CREDITS).\n' + packed.join('\n') + '\n');
console.log(`${courses.length} courses, ${tuxNodes} tux nodes, ${wrote.length} files`);
for (const c of courses) console.log(`  ${c.dir.padEnd(28)} ${c.width}x${c.length} m  ${c.samples[0]}x${c.samples[1]} samples  h ${c.min_height}..${c.max_height}  ${c.objects} objects  par ${c.par_time}`);
