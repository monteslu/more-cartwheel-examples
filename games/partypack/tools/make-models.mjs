// Generates the 3D models as textured glb files with PBR materials into assets/models/. Run after make-textures.mjs:
//   node tools/make-models.mjs [name ...]
// Every model here is made by this code (nothing downloaded). Characters face +Z; units are metres.
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb } from './gltf-writer.mjs';
import { Mesher } from './mesher.mjs';
import { SkinMesh } from './charmesh.mjs';
import { fbm, hash2, smooth, clamp } from './texlib.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const TEX = join(ROOT, 'assets', 'textures'), OUT = join(ROOT, 'assets', 'models');
const MAIN = process.argv[1] && import.meta.url.endsWith(process.argv[1].split('/').pop());
const only = MAIN ? process.argv.slice(2) : ['none'];
const want = (n) => MAIN && (only.length === 0 || only.includes(n));

const PCOL = [[0.93, 0.28, 0.30], [0.25, 0.55, 0.95], [0.30, 0.80, 0.45], [0.98, 0.78, 0.22]];
const lin = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const linc = (c) => c.map(lin); // baseColorFactor is linear

/** material definitions: set = texture set name (null = factor only), tint = linear multiplier, rough, emissive factor */
const DEFS = {
  steel: { set: 'steel', emissive: [1.6, 1.6, 1.6], rough: 1 },
  metal: { set: 'metal', tint: [0.9, 0.92, 1] },
  metal_dark: { set: 'metal', tint: [0.3, 0.32, 0.38], rough: 1.1 },
  lava: { set: 'lava', emissive: [1.0, 1.0, 1.0] },
  wood: { set: 'wood' },
  wood_dark: { set: 'wood', tint: [0.55, 0.5, 0.48] },
  rock: { set: 'rock' },
  sand: { set: 'sand' },
  grass: { set: 'grass' },
  asphalt: { set: 'asphalt' },
  kerb: { set: 'kerb' },
  stand: { set: 'stand', emissive: [0.0, 0.0, 0.0] },
  boardtile: { set: 'boardtile' },
  felt: { set: 'felt' },
  glow_cyan: { set: null, emissive: [0.4, 2.6, 3.4] },
  glow_orange: { set: null, emissive: [4, 1.5, 0.3] },
  glow_red: { set: null, emissive: [6, 0.3, 0.2] },
  lamp: { set: null, emissive: [9, 8, 6] },
  rubber: { set: null, rough: 0.85, tint: [0.03, 0.03, 0.035] },
  white: { set: null, rough: 0.5, tint: [0.8, 0.8, 0.82] },
};
class Mats {
  constructor(glb) { this.glb = glb; this.idx = {}; }
  get(name) {
    if (this.idx[name] !== undefined) return this.idx[name];
    let d = DEFS[name];
    if (!d) throw new Error('no material ' + name);
    const m = this.glb.pbr(name, TEX, d.set, { ...d, rough: d.rough ?? 1, metal: d.set ? 1 : (d.metal ?? 0) });
    const mat = this.glb.materials[m];
    if (!d.set) { mat.pbrMetallicRoughness.roughnessFactor = d.rough ?? 0.6; mat.pbrMetallicRoughness.metallicFactor = d.metal ?? 0; if (d.emissive) mat.emissiveFactor = d.emissive; }
    return (this.idx[name] = m);
  }
  /** a one-off plain material (player colours) */
  plain(name, rgbLinear, rough, metal = 0, emissive = null, strength = 1) {
    if (this.idx[name] !== undefined) return this.idx[name];
    const m = this.glb.material(name, rough, metal, emissive);
    this.glb.materials[m].pbrMetallicRoughness.baseColorFactor = [...rgbLinear, 1];
    if (emissive) this.glb.materials[m].emissiveFactor = emissive.map((x) => x * strength);
    return (this.idx[name] = m);
  }
  /** a textured material tinted by a colour (the velvet of a character) */
  tinted(name, set, rgbLinear, rough = 1, extra = {}) {
    if (this.idx[name] !== undefined) return this.idx[name];
    const m = this.glb.pbr(name, TEX, set, { tint: rgbLinear, rough, metal: 0, ...extra });
    this.glb.materials[m].pbrMetallicRoughness.metallicFactor = 0;
    return (this.idx[name] = m);
  }
}
const withExt = () => {};

function writeMesher(name, fn, extraMats) {
  const glb = new Glb(), M = new Mats(glb), m = new Mesher();
  fn(m, M);
  const mi = {};
  for (const k of m.groups.keys()) mi[k] = M.idx[k] ?? M.get(k);
  glb.sceneNodes.push(glb.node({ name, mesh: glb.mesh(name, m.prims(mi)) }));
  withExt(glb);
  glb.write(join(OUT, `${name}.glb`));
  const tris = [...m.groups].reduce((a, [, g]) => a + g.idx.length / 3, 0);
  console.log(name, tris, 'tris');
}
/** a SkinMesh used as static geometry (joints and weights dropped) */
function writeSkin(name, build, matSetup) {
  const glb = new Glb(), M = new Mats(glb), s = new SkinMesh();
  matSetup?.(M);
  build(s, M);
  const prims = [];
  for (const [mat, g] of s.groups) { delete g.joints; delete g.weights; prims.push([g, M.idx[mat] ?? M.get(mat)]); }
  glb.sceneNodes.push(glb.node({ name, mesh: glb.mesh(name, prims) }));
  withExt(glb);
  glb.write(join(OUT, `${name}.glb`));
  console.log(name, prims.reduce((a, [g]) => a + g.idx.length / 3, 0), 'tris');
}

// ── the party character ─────────────────────────────────────────────────
function blob(p) {
  const c = linc(PCOL[p]);
  writeSkin(`blob_${p}`, (s, M) => {
    M.tinted('body', 'velvet', c, 0.8, { ao: 1 });
    M.tinted('belly', 'velvet', [0.95, 0.85, 0.68].map(lin), 0.85);
    M.tinted('foot', 'velvet', c.map((x) => x * 0.55), 0.7);
    s.ellipsoid('body', [0, 0.02, 0], 0.5, 0.47, 0.49, { steps: 14, n: 28, tile: 0.7 });
    s.ellipsoid('body', [0, -0.12, 0], 0.5, 0.36, 0.5, { steps: 10, n: 28, tile: 0.7 }); // the heavier bottom
    s.ellipsoid('belly', [0, -0.12, 0.31], 0.27, 0.27, 0.2, { steps: 10, n: 20, tile: 0.7 });
    for (const sx of [-1, 1]) {
      s.ellipsoid('foot', [sx * 0.21, -0.44, 0.1], 0.17, 0.1, 0.24, { steps: 8, n: 16, tile: 0.4 });
      s.ellipsoid('body', [sx * 0.51, -0.06, 0.04], 0.1, 0.12, 0.1, { steps: 8, n: 14, tile: 0.4 });
    }
  });
}
function face() {
  writeSkin('face', (s, M) => {
    M.plain('eye', lin_(1, 1, 1), 0.08, 0);
    M.plain('pupil', lin_(0.02, 0.02, 0.04), 0.05, 0);
    M.plain('mouth', lin_(0.18, 0.04, 0.08), 0.5, 0);
    M.plain('cheek', lin_(1, 0.45, 0.5), 0.9, 0);
    for (const sx of [-1, 1]) {
      s.ellipsoid('eye', [sx * 0.17, 0.14, 0.4], 0.115, 0.15, 0.08, { steps: 8, n: 18, tile: 0.4 });
      s.ellipsoid('pupil', [sx * 0.17, 0.13, 0.455], 0.068, 0.088, 0.04, { steps: 8, n: 14, tile: 0.4 });
      s.ellipsoid('eye', [sx * 0.15, 0.17, 0.49], 0.022, 0.022, 0.012, { steps: 5, n: 8, tile: 0.4 }); // the catchlight
      s.ellipsoid('cheek', [sx * 0.29, -0.04, 0.38], 0.07, 0.04, 0.03, { steps: 6, n: 12, tile: 0.4 });
    }
    // a smile: a thin dark arc of small ellipsoids
    for (let i = -4; i <= 4; i++) { const a = i / 4; s.ellipsoid('mouth', [a * 0.1, -0.06 - (1 - a * a) * 0.0 + a * a * 0.04 - 0.02, 0.47 - a * a * 0.01], 0.022, 0.018, 0.015, { steps: 4, n: 6, tile: 0.4 }); }
  });
}
function lin_(r, g, b) { return [lin(r), lin(g), lin(b)]; }
function stalk() {
  writeSkin('stalk', (s, M) => {
    M.plain('wire', lin_(0.75, 0.77, 0.85), 0.3, 0.9);
    M.plain('bead', lin_(0.5, 1, 0.6), 0.2, 0, [0.25, 0.9, 0.35], 4);
    s.capsule('wire', [0, 0.46, 0], [0, 0.72, 0], 0.018, 0.018, { n: 8 });
    s.ellipsoid('bead', [0, 0.78, 0], 0.075, 0.075, 0.075, { steps: 8, n: 14 });
  });
}

// ── Blast Ring ──────────────────────────────────────────────────────────
function arena() {
  writeMesher('arena', (m) => {
    const S = 13, h = 0.5;
    m.box({ top: 'steel', side: 'metal_dark', bottom: 'metal_dark' }, [0, -0.5, 0], [S, 1, S], { uv: 6.5 });
    // a raised lip all round with a glowing channel in its top
    for (const [cx, cz, sx, sz] of [[0, S / 2 - 0.1, S, 0.2], [0, -S / 2 + 0.1, S, 0.2], [S / 2 - 0.1, 0, 0.2, S], [-S / 2 + 0.1, 0, 0.2, S]]) m.box('metal', [cx, 0.05, cz], [sx, 0.1, sz], { uv: 1 });
  });
}
function pillar() {
  writeMesher('pillar', (m) => {
    m.cyl('metal', [0, 0, 0], 0.7, 9, { n: 28, uv: 2, caps: true });
    for (const y of [-3.8, -1.2, 1.4, 3.9]) m.cyl('metal_dark', [0, y, 0], 0.82, 0.22, { n: 28, uv: 1, caps: true });
    for (const y of [-2.5, 2.7]) m.cyl('glow_cyan', [0, y, 0], 0.725, 0.07, { n: 28, uv: 1, caps: false });
    m.box('metal_dark', [0, -4.6, 0], [1.9, 0.3, 1.9], { uv: 1 });
    m.box('metal_dark', [0, 4.65, 0], [1.9, 0.3, 1.9], { uv: 1 });
  });
}
function lavaPlane() {
  writeMesher('lava', (m) => {
    const S = 45;
    m.quad('lava', [-S, 0, S], [S, 0, S], [S, 0, -S], [-S, 0, -S], [0, 1, 0], [0, 0], [10, 0], [10, 10], [0, 10]);
  });
}

// ── Raft Rush ───────────────────────────────────────────────────────────
function raft() {
  writeMesher('raft', (m) => {
    // deck 2 x 0.4 x 2: planks along z, log rim, rope posts
    m.box({ top: 'wood', side: 'wood_dark', bottom: 'wood_dark' }, [0, 0, 0], [2, 0.4, 2], { uv: 1.4 });
    for (const [x, z, sx, sz] of [[0, 1.02, 2.12, 0.14], [0, -1.02, 2.12, 0.14], [1.02, 0, 0.14, 2.0], [-1.02, 0, 0.14, 2.0]]) m.box('wood_dark', [x, 0.2, z], [sx, 0.16, sz], { uv: 0.6 });
    for (const sx of [-1, 1]) for (const sz of [-1, 1]) m.cyl('wood_dark', [sx * 1.0, 0.33, sz * 1.0], 0.07, 0.34, { n: 10, uv: 0.5 });
    m.cyl('wood_dark', [-0.7, 0.95, -0.7], 0.045, 1.5, { n: 10, uv: 0.5 });
    m.cyl('wood', [0.78, 0.26, 0.7], 0.05, 1.0, { n: 8, uv: 0.5, axis: 'x' });
  });
}
function buoy(name, gold) {
  writeSkin(name, (s, M) => {
    M.plain('b_body', gold ? lin_(0.95, 0.62, 0.04) : lin_(0.9, 0.12, 0.08), 0.28, gold ? 0.9 : 0);
    M.plain('b_band', lin_(0.92, 0.92, 0.92), 0.4, 0);
    M.plain('b_lamp', lin_(1, 0.9, 0.6), 0.2, 0, gold ? [1, 0.7, 0.2] : [1, 0.3, 0.15], 5);
    s.ellipsoid('b_body', [0, 0, 0], 0.5, 0.5, 0.5, { steps: 14, n: 28 });
    s.ellipsoid('b_band', [0, 0, 0], 0.508, 0.14, 0.508, { steps: 8, n: 28 });
    s.ellipsoid('b_lamp', [0, 0.55, 0], 0.09, 0.09, 0.09, { steps: 6, n: 12 });
    s.capsule('b_band', [0, 0.45, 0], [0, 0.52, 0], 0.05, 0.05, { n: 10 });
  });
}
/** a rocky wall: a box whose faces are subdivided and pushed in and out with noise so it reads as a cliff, with crisp rock UVs */
function cliff(name, w, h, d, seed) {
  const glb = new Glb(), M = new Mats(glb);
  const mi = M.get('rock');
  const pos = [], nrm = [], uv = [], idx = [];
  const disp = (x, y, z) => 0.55 * (fbm((x + 50) / 17, (y + 50) / 17, 7, 4, seed) - 0.5) + 0.2 * (fbm((x + 50) / 6, (z + 50) / 6 + y / 9, 12, 4, seed + 3) - 0.5);
  const faces = [
    { n: [0, 0, 1], u: [1, 0, 0], v: [0, 1, 0], su: w, sv: h, c: [0, 0, d / 2] }, { n: [0, 0, -1], u: [-1, 0, 0], v: [0, 1, 0], su: w, sv: h, c: [0, 0, -d / 2] },
    { n: [1, 0, 0], u: [0, 0, -1], v: [0, 1, 0], su: d, sv: h, c: [w / 2, 0, 0] }, { n: [-1, 0, 0], u: [0, 0, 1], v: [0, 1, 0], su: d, sv: h, c: [-w / 2, 0, 0] },
    { n: [0, 1, 0], u: [1, 0, 0], v: [0, 0, -1], su: w, sv: d, c: [0, h / 2, 0] },
  ];
  const NSU = 24;
  for (const f of faces) {
    const nu = Math.max(4, Math.round(f.su / (f.su > f.sv ? Math.max(f.su, f.sv) : f.su) * NSU)), nv = Math.max(4, Math.round(f.sv / Math.max(f.su, f.sv) * NSU));
    const base = pos.length / 3;
    for (let j = 0; j <= nv; j++) for (let i = 0; i <= nu; i++) {
      const a = (i / nu - 0.5) * f.su, b = (j / nv - 0.5) * f.sv;
      let p = [f.c[0] + f.u[0] * a + f.v[0] * b, f.c[1] + f.u[1] * a + f.v[1] * b, f.c[2] + f.u[2] * a + f.v[2] * b];
      const edge = Math.min(i, nu - i, j, nv - j) === 0 ? 0 : 1; // keep the silhouette edges straight so cliffs meet cleanly
      const dd = disp(p[0], p[1], p[2]) * 2.2 * edge;
      p = [p[0] + f.n[0] * dd, p[1] + f.n[1] * dd, p[2] + f.n[2] * dd];
      pos.push(...p); nrm.push(...f.n);
      uv.push((a + f.su / 2) / 5 + seed, (b + f.sv / 2) / 5);
    }
    for (let j = 0; j < nv; j++) for (let i = 0; i < nu; i++) { const q = base + j * (nu + 1) + i; idx.push(q, q + 1, q + nu + 2, q, q + nu + 2, q + nu + 1); }
  }
  // smooth normals from the displaced geometry
  const N = new Float32Array(pos.length);
  for (let t = 0; t < idx.length; t += 3) {
    const [a, b, c] = [idx[t], idx[t + 1], idx[t + 2]];
    const e1 = [pos[b * 3] - pos[a * 3], pos[b * 3 + 1] - pos[a * 3 + 1], pos[b * 3 + 2] - pos[a * 3 + 2]], e2 = [pos[c * 3] - pos[a * 3], pos[c * 3 + 1] - pos[a * 3 + 1], pos[c * 3 + 2] - pos[a * 3 + 2]];
    const fn = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    for (const v of [a, b, c]) { N[v * 3] += fn[0]; N[v * 3 + 1] += fn[1]; N[v * 3 + 2] += fn[2]; }
  }
  for (let v = 0; v < pos.length / 3; v++) { const l = Math.hypot(N[v * 3], N[v * 3 + 1], N[v * 3 + 2]) || 1; nrm[v * 3] = N[v * 3] / l; nrm[v * 3 + 1] = N[v * 3 + 1] / l; nrm[v * 3 + 2] = N[v * 3 + 2] / l; }
  // flip winding if the face normals disagree with the outward direction
  for (let t = 0; t < idx.length; t += 3) {
    const [a, b, c] = [idx[t], idx[t + 1], idx[t + 2]];
    const e1 = [pos[b * 3] - pos[a * 3], pos[b * 3 + 1] - pos[a * 3 + 1], pos[b * 3 + 2] - pos[a * 3 + 2]], e2 = [pos[c * 3] - pos[a * 3], pos[c * 3 + 1] - pos[a * 3 + 1], pos[c * 3 + 2] - pos[a * 3 + 2]];
    const fn = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    const ctr = [pos[a * 3], pos[a * 3 + 1], pos[a * 3 + 2]];
    if (fn[0] * ctr[0] + fn[1] * ctr[1] + fn[2] * ctr[2] < 0) { idx[t + 1] = c; idx[t + 2] = b; }
  }
  glb.sceneNodes.push(glb.node({ name, mesh: glb.mesh(name, { pos, nrm, uv, idx }, mi) }));
  glb.write(join(OUT, `${name}.glb`));
  console.log(name, idx.length / 3, 'tris');
}
function seabed() {
  writeMesher('seabed', (m) => { const S = 45; m.quad('sand', [-S, 0, S], [S, 0, S], [S, 0, -S], [-S, 0, -S], [0, 1, 0], [0, 0], [11, 0], [11, 11], [0, 11]); });
}

// ── Loop Racers ─────────────────────────────────────────────────────────
const SLEN = 22, RAD = 8, TW = 9.5, PI = Math.PI, LTOT = 2 * SLEN + 2 * PI * RAD;
function trackAt(s) {
  s = ((s % LTOT) + LTOT) % LTOT;
  if (s < SLEN) return { x: -SLEN / 2 + s, z: RAD, nx: 0, nz: 1 };
  if (s < SLEN + PI * RAD) { const th = (s - SLEN) / RAD, sn = Math.sin(th), cs = Math.cos(th); return { x: SLEN / 2 + RAD * sn, z: RAD * cs, nx: sn, nz: cs }; }
  if (s < 2 * SLEN + PI * RAD) { const t = s - SLEN - PI * RAD; return { x: SLEN / 2 - t, z: -RAD, nx: 0, nz: -1 }; }
  const th = (s - 2 * SLEN - PI * RAD) / RAD, sn = Math.sin(th), cs = Math.cos(th); return { x: -SLEN / 2 - RAD * sn, z: -RAD * cs, nx: -sn, nz: -cs };
}
function track() {
  writeMesher('track', (m) => {
    const N = 220, ds = LTOT / N;
    const P = (s, off) => { const t = trackAt(s); return [t.x + t.nx * off, 0, t.z + t.nz * off]; };
    const ribbon = (mat, o0, o1, y0, y1, uvAcross, uvAlong, ny = 1, outward = 0) => {
      for (let i = 0; i < N; i++) {
        const s0 = i * ds, s1 = (i + 1) * ds;
        const a = P(s0, o0), b = P(s1, o0), c = P(s1, o1), d = P(s0, o1);
        a[1] = b[1] = y0; c[1] = d[1] = y1;
        m.quad(mat, a, b, c, d, [0, ny, 0], [s0 / uvAlong, 0], [s1 / uvAlong, 0], [s1 / uvAlong, (o1 - o0) / uvAcross], [s0 / uvAlong, (o1 - o0) / uvAcross]);
      }
    };
    const wall = (mat, off, y0, y1, side, uvh) => { // a vertical band at an offset, facing toward the track centre when side < 0 and away when side > 0
      for (let i = 0; i < N; i++) {
        const s0 = i * ds, s1 = (i + 1) * ds;
        const a = P(s0, off), b = P(s1, off), c = P(s1, off), d = P(s0, off);
        a[1] = b[1] = y0; c[1] = d[1] = y1;
        const t = trackAt((s0 + s1) / 2);
        m.quad(mat, a, b, c, d, [t.nx * side, 0, t.nz * side], [s0 / uvh, y0 / uvh], [s1 / uvh, y0 / uvh], [s1 / uvh, y1 / uvh], [s0 / uvh, y1 / uvh]);
      }
    };
    ribbon('asphalt', -TW / 2, TW / 2, 0.06, 0.06, 5, 5);
    for (const side of [-1, 1]) {
      const o0 = side * (TW / 2), o1 = side * (TW / 2 + 0.7);
      const a = Math.min(o0, o1), b = Math.max(o0, o1);
      ribbon('kerb', a, b, 0.14, 0.14, 0.7, 3.2);
      wall('kerb', side * (TW / 2), 0.06, 0.14, -side, 3.2); wall('kerb', side * (TW / 2 + 0.7), 0.06, 0.14, side, 3.2);
      // tyre wall (a low dark barrier) beyond the kerb
      const bo = side * (TW / 2 + 1.5);
      ribbon('metal_dark', Math.min(bo, bo + side * 0.5), Math.max(bo, bo + side * 0.5), 0.55, 0.55, 0.5, 2);
      wall('metal_dark', bo, 0.0, 0.55, -side, 1.5); wall('metal_dark', bo + side * 0.5, 0.0, 0.55, side, 1.5);
      for (let k = 0; k < 44; k++) { const t = trackAt(k * LTOT / 44), px = t.x + t.nx * (bo + side * 0.25), pz = t.z + t.nz * (bo + side * 0.25); m.cyl('rubber', [px, 0.3, pz], 0.22, 0.6, { n: 10, uv: 0.8 }); }
    }
    // the ground: a big grass plane under everything
    m.quad('grass', [-60, -0.05, 45], [60, -0.05, 45], [60, -0.05, -45], [-60, -0.05, -45], [0, 1, 0], [0, 0], [15, 0], [15, 11], [0, 11]);
    // start line: a chequered band across the road
    { const t = trackAt(0); const a = [t.x - 0.3, 0.075, t.z + TW / 2], b = [t.x + 0.3, 0.075, t.z + TW / 2], c = [t.x + 0.3, 0.075, t.z - TW / 2], d = [t.x - 0.3, 0.075, t.z - TW / 2]; m.quad('white', a, b, c, d, [0, 1, 0], [0, 0], [1, 0], [1, 1], [0, 1]); }
    // the grandstands beyond the far side, a roof, lamp towers
    for (let i = 0; i < 5; i++) {
      const x = -19 + i * 9.5, z = -(RAD + TW / 2 + 6);
      m.box({ front: 'stand', side: 'metal_dark', top: 'metal_dark' }, [x, 1.9, z], [8.6, 3.8, 3.6], { uv: 3, facade: { tile: 8.6 / 2.2 } });
      m.box('metal_dark', [x, 4.1, z + 0.4], [9, 0.18, 4.4], { uv: 2 });
      for (const sx of [-1, 1]) m.cyl('metal', [x + sx * 4.3, 2.5, z + 2.1], 0.09, 4.2, { n: 8, uv: 1 });
    }
    for (const [x, z] of [[-24, -14], [24, -14], [-24, 14], [24, 14], [0, 20], [0, -20]]) {
      m.cyl('metal', [x, 3.8, z], 0.2, 7.6, { n: 10, uv: 1 });
      m.box('metal_dark', [x, 7.75, z], [1.6, 0.25, 0.7], { uv: 1 });
      m.box('lamp', [x, 7.58, z], [1.4, 0.06, 0.5], { uv: 1 });
    }
    // infield: a pit building and signs
    m.box('metal_dark', [0, 0.9, 0], [8, 1.8, 3], { uv: 2 });
    m.box('glow_cyan', [0, 1.6, 1.52], [6, 0.12, 0.05], { uv: 1 });
  });
}
function kart(p) {
  const c = linc(PCOL[p]);
  writeSkin(`kart_${p}`, (s, M) => {
    M.plain('paint', c, 0.28, 0.1);
    M.plain('dark', lin_(0.05, 0.05, 0.06), 0.45, 0.5);
    M.plain('tail', lin_(0.5, 0.02, 0.02), 0.4, 0, [1, 0.1, 0.05], 5);
    M.plain('chrome', lin_(0.8, 0.82, 0.88), 0.18, 1);
    s.ellipsoid('paint', [0, 0.36, -0.05], 0.52, 0.2, 0.95, { steps: 10, n: 24 });
    s.ellipsoid('paint', [0, 0.3, 0.9], 0.34, 0.14, 0.42, { steps: 8, n: 20 });
    s.ellipsoid('dark', [0, 0.52, -0.2], 0.3, 0.1, 0.4, { steps: 8, n: 18 }); // seat tub
    s.ellipsoid('dark', [0, 0.72, -0.95], 0.65, 0.035, 0.16, { steps: 6, n: 16 }); // wing
    for (const sx of [-1, 1]) { s.ellipsoid('dark', [sx * 0.62, 0.62, -0.95], 0.03, 0.2, 0.18, { steps: 6, n: 12 }); s.capsule('chrome', [sx * 0.2, 0.4, -0.7], [sx * 0.2, 0.7, -0.92], 0.025, 0.025, { n: 8 }); }
    s.ellipsoid('tail', [0, 0.38, -1.0], 0.3, 0.05, 0.035, { steps: 6, n: 14 });
    s.capsule('chrome', [-0.3, 0.2, 1.2], [0.3, 0.2, 1.2], 0.03, 0.03, { n: 8 });
    s.ellipsoid('dark', [0, 0.2, 0.05], 0.5, 0.06, 0.9, { steps: 6, n: 18 }); // floor
  });
}
function wheel() {
  writeMesher('wheel', (m) => {
    m.cyl('rubber', [0, 0, 0], 0.26, 0.28, { n: 24, uv: 0.8, axis: 'x' });
    m.cyl('metal', [0, 0, 0], 0.15, 0.3, { n: 16, uv: 0.5, axis: 'x' });
    m.cyl('metal_dark', [0, 0, 0], 0.06, 0.32, { n: 8, uv: 0.5, axis: 'x' });
  });
}
function barrel() {
  writeMesher('barrel', (m) => {
    m.cyl('metal', [0, 0, 0], 0.5, 1, { n: 24, uv: 1.2 });
    for (const y of [-0.34, 0.34]) m.cyl('metal_dark', [y * 0 , y, 0], 0.52, 0.08, { n: 24, uv: 1, caps: false });
    m.cyl('glow_red', [0, 0.1, 0], 0.512, 0.1, { n: 24, uv: 1, caps: false });
  });
}

// ── the party hub ───────────────────────────────────────────────────────
function table() {
  writeMesher('table', (m) => {
    // a round game table seen from above: a felt top, a wooden rim, a carved edge
    const R = 15;
    m.cyl('felt', [0, -0.3, 0], R, 0.6, { n: 64, uv: 3 });
    for (let i = 0; i < 64; i++) { /* a rim torus approximated by a ring of boxes */
      const a = (i / 64) * Math.PI * 2;
      m.box('wood', [Math.cos(a) * R, 0.05, Math.sin(a) * R], [0.9, 0.5, 0.9], { uv: 0.9 });
    }
    m.cyl('wood_dark', [0, -1.3, 0], R * 0.55, 1.6, { n: 24, uv: 2 });
  });
}
function boardtile(name, tint, glow) {
  const glb = new Glb(), M = new Mats(glb), m = new Mesher();
  const mat = M.tinted('tile', 'boardtile', tint, 0.9, { ao: 1 });
  M.idx.tile = mat;
  m.box('tile', [0, 0, 0], [1.4, 0.3, 1.4], { uv: 1.4 });
  const mi = { tile: mat };
  const prims = m.prims(mi);
  if (glow) { const gm = M.plain('glow', glow, 0.4, 0, glow.map((x) => Math.min(1, x * 1.6)), 3); const m2 = new Mesher(); m2.box('g', [0, 0.16, 0], [1.0, 0.02, 1.0], { uv: 1 }); prims.push(...m2.prims({ g: gm })); }
  glb.sceneNodes.push(glb.node({ name, mesh: glb.mesh(name, prims) }));
  withExt(glb);
  glb.write(join(OUT, `${name}.glb`));
  console.log(name);
}
function podium() {
  writeSkin('podium', (s, M) => {
    M.plain('gold', lin_(1, 0.74, 0.16), 0.25, 1);
    M.plain('silver', lin_(0.82, 0.84, 0.9), 0.28, 1);
    M.plain('bronze', lin_(0.8, 0.45, 0.2), 0.32, 1);
    M.plain('slate', lin_(0.22, 0.2, 0.3), 0.5, 0.2);
    const step = (mat, x, h) => { s.capsule(mat, [x - 0.62, h / 2, 0], [x + 0.62, h / 2, 0], 0.001, 0.001, { n: 4 }); };
    const slab = (mat, x, h, w) => { // a rounded block from stacked ellipsoid rings
      const rings = [{ c: [x, 0, 0], U: [1, 0, 0], V: [0, 0, 1], ru: 0.01, rv: 0.01, e: 4.5 }]; for (let k = 0; k <= 8; k++) { const t = k / 8; rings.push({ c: [x, t * h, 0], U: [1, 0, 0], V: [0, 0, 1], ru: w * (k === 0 || k === 8 ? 0.82 : 1), rv: w * (k === 0 || k === 8 ? 0.82 : 1), e: 4.5 }); } rings.push({ c: [x, h, 0], U: [1, 0, 0], V: [0, 0, 1], ru: 0.01, rv: 0.01, e: 4.5 });
      s.loft(mat, rings, { n: 28, tile: 1 });
    };
    slab('gold', 0, 1.0, 0.62); slab('silver', -1.35, 0.7, 0.62); slab('bronze', 1.35, 0.5, 0.62);
    slab('slate', 0, 0.12, 2.5);
    void step;
  });
}
function star() {
  writeSkin('star3d', (s, M) => {
    M.plain('gold', lin_(1, 0.78, 0.18), 0.22, 1, [0.9, 0.55, 0.05], 2);
    for (let i = 0; i < 5; i++) { const a = -Math.PI / 2 + (i / 5) * Math.PI * 2; s.capsule('gold', [0, 0.02, 0], [Math.cos(a) * 0.45, Math.sin(a) * -0.45 + 0.02, 0], 0.13, 0.03, { n: 10, ratio: 0.5 }); }
    s.ellipsoid('gold', [0, 0, 0], 0.22, 0.22, 0.1, { steps: 8, n: 16 });
  });
}
function trophy() {
  writeSkin('trophy', (s, M) => {
    M.plain('gold', lin_(1, 0.78, 0.18), 0.2, 1);
    M.plain('base', lin_(0.12, 0.1, 0.16), 0.4, 0.2);
    const rings = [];
    for (let k = 0; k <= 12; k++) { const t = k / 12, r = 0.12 + 0.36 * Math.sin(Math.PI * (0.15 + 0.7 * t)) ** 1.2 * (1 - 0.15 * t); rings.push({ c: [0, 0.45 + t * 0.7, 0], U: [1, 0, 0], V: [0, 0, 1], ru: Math.max(0.03, r), rv: Math.max(0.03, r), e: 2 }); }
    s.loft('gold', rings, { n: 28, tile: 1 });
    s.ellipsoid('gold', [0, 0.35, 0], 0.1, 0.1, 0.1, { steps: 6, n: 12 });
    s.capsule('gold', [0, 0.2, 0], [0, 0.45, 0], 0.05, 0.05, { n: 10 });
    s.ellipsoid('base', [0, 0.1, 0], 0.32, 0.12, 0.32, { steps: 6, n: 20 });
    for (const sx of [-1, 1]) s.capsule('gold', [sx * 0.46, 0.95, 0], [sx * 0.52, 1.05, 0], 0.05, 0.03, { n: 8 });
  });
}

if (MAIN) {
  const jobs = {
    blob: () => { for (let p = 0; p < 4; p++) blob(p); face(); stalk(); },
    arena, pillar, lava: lavaPlane, raft, seabed,
    buoy: () => { buoy('buoy_red', false); buoy('buoy_gold', true); },
    cliff: () => { cliff('cliff_ns', 48, 8, 4, 3); cliff('cliff_ew', 4, 8, 36, 7); },
    track, kart: () => { for (let p = 0; p < 4; p++) kart(p); }, wheel, barrel,
    table, tiles: () => { boardtile('tile_plain', [0.85, 0.9, 1].map(lin), null); boardtile('tile_star', [0.95, 0.5, 0.9].map(lin), [1, 0.45, 0.95].map(lin)); boardtile('tile_end', [1, 0.8, 0.2].map(lin), [1, 0.7, 0.1].map(lin)); },
    star, trophy, podium,
  };
  for (const [k, f] of Object.entries(jobs)) if (want(k)) f();
}
