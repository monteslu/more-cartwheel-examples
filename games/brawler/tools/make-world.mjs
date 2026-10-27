// Generates the stage scenery (street, kerb, pavement, buildings with lit windows, lamp posts, per stage set dressing)
// and the breakable props as textured glb files with full PBR materials into assets/models/. Run after make-textures:
//   node tools/make-world.mjs
// Every model here is made by our own code. The textures it points at are written by make-textures.mjs.
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb } from './gltf-writer.mjs';
import { Mesher } from './mesher.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const TEX = join(ROOT, 'assets', 'textures'), OUT = join(ROOT, 'assets', 'models');
const hf = (i, salt) => { let h = (Math.imul(i, 2654435761) ^ Math.imul(salt, 40503)) >>> 0; h ^= h >>> 13; h = Math.imul(h, 0x5bd1e995) >>> 0; h ^= h >>> 15; return (h & 0xffff) / 65535; };

/** material table: name -> glb material, built lazily per file */
class Mats {
  constructor(glb) { this.glb = glb; this.idx = {}; }
  get(name) {
    if (this.idx[name] !== undefined) return this.idx[name];
    const d = DEFS[name];
    if (!d) throw new Error('no material ' + name);
    return (this.idx[name] = this.glb.pbr(name, TEX, d.set, d));
  }
}
const lit = (c, e) => [c[0] * e, c[1] * e, c[2] * e];
const DEFS = {
  ground_alley: { set: 'ground_alley' }, ground_docks: { set: 'ground_docks' }, ground_arcade: { set: 'ground_arcade' }, ground_roof: { set: 'ground_roof' },
  pave_alley: { set: 'pave_alley' }, pave_docks: { set: 'pave_docks' }, pave_arcade: { set: 'pave_arcade' }, pave_roof: { set: 'pave_roof' },
  facade_alley: { set: 'facade_alley', emissive: [1.15, 1.15, 1.15] }, facade_docks: { set: 'facade_docks', emissive: [1.1, 1.1, 1.1] },
  facade_arcade: { set: 'facade_arcade', emissive: [1.5, 1.5, 1.5] }, facade_factory: { set: 'facade_factory', emissive: [1.3, 1.3, 1.3] },
  facade_far: { set: 'facade_alley', emissive: [0.9, 0.9, 0.9], tint: [0.55, 0.55, 0.65] },
  brick: { set: 'wall_brick' }, brick_dark: { set: 'wall_brick_dark' }, plaster: { set: 'wall_plaster', tint: [0.8, 0.78, 0.72] },
  metal: { set: 'wall_metal' }, metal_red: { set: 'wall_metal_red' }, metal_green: { set: 'wall_metal_green' }, plate: { set: 'wall_plate' },
  steel: { set: 'paint_metal', tint: [0.32, 0.33, 0.36], rough: 0.7 }, steel_dark: { set: 'paint_metal', tint: [0.12, 0.12, 0.14] },
  steel_yellow: { set: 'paint_metal', tint: [0.8, 0.6, 0.08] }, steel_red: { set: 'paint_metal', tint: [0.6, 0.12, 0.08] },
  steel_blue: { set: 'paint_metal', tint: [0.14, 0.26, 0.55] }, steel_green: { set: 'paint_metal', tint: [0.14, 0.4, 0.26] },
  wood: { set: 'wood' }, wood_grey: { set: 'wood_grey' }, wood_dark: { set: 'wood', tint: [0.55, 0.5, 0.48] },
  plastic: { set: 'fab_leather', tint: [0.1, 0.12, 0.13], metal: 0.0 },
  bulb: { set: null, emissive: [7, 6, 4] },
  neon_pink: { set: null, emissive: [4, 0.6, 2.2] }, neon_cyan: { set: null, emissive: [0.5, 3, 4] }, neon_amber: { set: null, emissive: [4, 2.4, 0.5] },
  neon_violet: { set: null, emissive: [1.8, 0.6, 4] },
  glow_orange: { set: null, emissive: [3.5, 1.2, 0.3] },
  screen: { set: null, emissive: [0.6, 1.6, 2.4] },
};

const THEMES = [
  { name: 'alley', ground: 'ground_alley', pave: 'pave_alley', facade: 'facade_alley', wall: 'brick', trim: 'plaster', base: 'brick_dark', bulb: 'bulb', hmin: 8, hmax: 15 },
  { name: 'docks', ground: 'ground_docks', pave: 'pave_docks', facade: 'facade_docks', wall: 'metal', trim: 'steel', base: 'plate', bulb: 'bulb', hmin: 7, hmax: 12 },
  { name: 'arcade', ground: 'ground_arcade', pave: 'pave_arcade', facade: 'facade_arcade', wall: 'plaster', trim: 'steel_dark', base: 'steel_dark', bulb: 'neon_cyan', hmin: 8, hmax: 13 },
  { name: 'factory', ground: 'ground_roof', pave: 'pave_roof', facade: 'facade_factory', wall: 'plate', trim: 'steel_dark', base: 'plate', bulb: 'bulb', hmin: 9, hmax: 16 },
];
const X0 = -34, X1 = 108;

function lampPost(m, x, bulb) {
  m.cyl('steel_dark', [x, 2.3, -4.2], 0.08, 4.4, { n: 12, uv: 1 });
  m.cyl('steel_dark', [x, 0.35, -4.2], 0.16, 0.4, { n: 12, uv: 1 });
  m.box('steel_dark', [x, 4.45, -3.7], [0.12, 0.1, 1.1], { uv: 1 });
  m.box('steel_dark', [x, 4.4, -3.1], [0.5, 0.12, 0.42], { uv: 1 });
  m.box(bulb, [x, 4.32, -3.1], [0.42, 0.04, 0.32], { uv: 1 });
}

function street(m, th, stage) {
  // roadway, kerb, pavement
  m.box('g', [(X0 + X1) / 2, -0.15, 1.2], [X1 - X0, 0.3, 9.2], { uv: 4 });
  m.box('p', [(X0 + X1) / 2, 0.085, -3.575], [X1 - X0, 0.17, 0.35], { uv: 1 });
  m.box('p', [(X0 + X1) / 2, 0.085, -4.95], [X1 - X0, 0.17, 2.4], { uv: 2 });
  // kerb every few metres: a dark joint line is in the texture; add a drain grate and manholes
  for (let x = X0 + 2; x < X1; x += 9 + hf(x, 3) * 5) m.box('steel_dark', [x, 0.01, -3.1], [0.9, 0.02, 0.24], { uv: 1 });
  // the street's far lane edge: a deep void under the camera view
  // building row A, close behind the pavement
  let bx = X0, bi = 0;
  while (bx < X1) {
    const w = 5 + hf(bi, stage + 1) * 3.4, h = th.hmin + hf(bi, stage + 2) * (th.hmax - th.hmin);
    const cx = bx + w / 2, zf = -6.0 - hf(bi, 9) * 0.4, d = 5;
    const oxs = Math.floor(hf(bi, 5) * 4) / 4, oys = Math.floor(hf(bi, 6) * 4) / 4;
    m.box({ front: th.facade, side: th.wall, top: th.trim }, [cx, h / 2, zf - d / 2], [w, h, d], { uv: 2.4, facade: { tile: 7.2, ox: oxs, oy: oys + 0.4 / 7.2 } });
    // plinth, cornice, pilasters
    m.box(th.base, [cx, 0.55, zf + 0.04], [w + 0.1, 0.9, 0.16], { uv: 2 });
    m.box(th.trim, [cx, h + 0.17, zf + 0.1], [w + 0.3, 0.35, 0.5], { uv: 1.5 });
    m.box(th.trim, [cx, h - 0.4, zf + 0.05], [w + 0.1, 0.12, 0.2], { uv: 1.5 });
    for (const sd of [-1, 1]) m.box(th.trim, [cx + sd * (w / 2 - 0.12), h / 2, zf + 0.07], [0.28, h, 0.16], { uv: 1.5 });
    // a door or a shutter at street level
    if (hf(bi, 11) > 0.35) {
      const dx = cx + (hf(bi, 12) - 0.5) * (w - 2.5);
      m.box('steel_dark', [dx, 1.15, zf + 0.12], [1.3, 2.2, 0.12], { uv: 1 });
      m.box(th.trim, [dx, 2.3, zf + 0.15], [1.6, 0.14, 0.2], { uv: 1 });
      m.box('steel_yellow', [dx + 0.45, 1.1, zf + 0.2], [0.05, 0.2, 0.05], { uv: 1 });
    }
    // a drain pipe
    if (hf(bi, 13) > 0.5) m.cyl('steel', [cx + w / 2 - 0.55, h / 2, zf + 0.2], 0.07, h, { n: 10, uv: 1 });
    stageDressing(m, th, stage, cx, w, h, zf, bi);
    bx += w + 0.2 + hf(bi, 7) * 0.9;
    bi++;
  }
  // row B, taller, further back: its windows glitter through the gaps and above row A
  bx = X0 - 6; bi = 100;
  while (bx < X1 + 10) {
    const w = 8 + hf(bi, stage + 3) * 5, h = 16 + hf(bi, stage + 4) * 14;
    m.box({ front: 'facade_far', side: th.wall, top: th.trim }, [bx + w / 2, h / 2, -17 - hf(bi, 9) * 3], [w, h, 8], { uv: 3, facade: { tile: 10, ox: hf(bi, 5), oy: 0.4 / 10 + Math.floor(hf(bi, 6) * 4) / 4 } });
    bx += w + 0.5 + hf(bi, 7) * 1.5; bi++;
  }
  // lamp posts every 11 m
  for (let x = 2; x < X1; x += 11) lampPost(m, x, th.bulb);
}

function stageDressing(m, th, stage, cx, w, h, zf, bi) {
  if (stage === 0) {
    // fire escape landings and ladders on some buildings
    if (hf(bi, 21) > 0.5) {
      const fx = cx + (hf(bi, 22) - 0.5) * (w - 3);
      for (let k = 0; k < 3; k++) {
        const y = 3.2 + k * 3.1;
        m.box('plate', [fx, y, zf + 0.75], [2.4, 0.08, 1.3], { uv: 1.2 });
        m.box('steel_dark', [fx, y + 0.5, zf + 1.4], [2.4, 0.05, 0.05], { uv: 1 });
        for (const sd of [-1, 1]) m.box('steel_dark', [fx + sd * 1.2, y + 0.25, zf + 1.4], [0.05, 0.5, 0.05], { uv: 1 });
        // stair run down to the landing below
        for (let s = 0; s < 8; s++) m.box('plate', [fx + 1.0 - s * 0.0, y - 0.22 - s * 0.36, zf + 0.5 + s * 0.0 + 0.9], [0.8, 0.05, 0.28], { uv: 1 });
      }
    }
    if (hf(bi, 23) > 0.6) m.box('steel_green', [cx - w / 2 + 1.2, 0.0 + 0.6, -3.0 - 1.0], [0.0001, 0.0001, 0.0001], { uv: 1 });
  } else if (stage === 1) {
    // a stack of containers against some buildings
    if (hf(bi, 31) > 0.45) {
      const mats = ['metal_red', 'metal_green', 'metal'];
      const cw = 6.0, ch = 2.5, n = 1 + Math.floor(hf(bi, 32) * 2.99);
      const cx0 = cx + (hf(bi, 33) - 0.5) * 2;
      for (let k = 0; k < n; k++) {
        const mt = mats[Math.floor(hf(bi, 34 + k) * 3) % 3];
        m.box({ front: mt, back: mt, left: mt, right: mt, top: mt, bottom: mt }, [cx0, 0.17 + ch / 2 + k * ch, zf + 1.4], [cw, ch, 2.4], { uv: 2.4 });
        for (let r = 0; r < 3; r++) m.box('steel_dark', [cx0 - cw / 2 + 0.05, 0.17 + ch / 2 + k * ch, zf + 1.4 + (r - 1) * 0.6], [0.04, ch - 0.1, 0.08], { uv: 1 });
        m.box('steel_dark', [cx0, 0.17 + k * ch + 0.08, zf + 1.4], [cw + 0.1, 0.16, 2.5], { uv: 1 });
      }
    }
    // bollards on the kerb
    if (hf(bi, 35) > 0.3) {
      m.cyl('steel_dark', [cx - 1, 0.45, -3.2], 0.13, 0.55, { n: 10, uv: 1 });
      m.cyl('steel_yellow', [cx - 1, 0.62, -3.2], 0.135, 0.12, { n: 10, uv: 1 });
    }
  } else if (stage === 2) {
    // glowing kerb strip and a neon sign hung out from some buildings
    m.box(['neon_pink', 'neon_cyan', 'neon_violet', 'neon_amber'][bi % 4], [cx, 0.19, -3.45], [w * 0.9, 0.025, 0.05], { uv: 1 });
    if (hf(bi, 41) > 0.5) {
      const nm = ['neon_pink', 'neon_cyan', 'neon_amber'][Math.floor(hf(bi, 42) * 3) % 3];
      m.box('steel_dark', [cx + w / 2 - 0.9, 5.2, zf + 0.55], [0.12, 1.5, 0.6], { uv: 1 });
      m.box('steel_dark', [cx + w / 2 - 0.9, 5.2, zf + 0.9], [0.5, 1.4, 0.08], { uv: 1 });
      m.box(nm, [cx + w / 2 - 0.9, 5.2, zf + 0.96], [0.36, 1.2, 0.03], { uv: 1 });
    }
    // an arcade cabinet by a wall
    if (hf(bi, 43) > 0.55) {
      const ax = cx + (hf(bi, 44) - 0.5) * (w - 2);
      m.box('steel_dark', [ax, 0.17 + 0.9, -4.6], [0.8, 1.8, 0.7], { uv: 1 });
      m.box('screen', [ax, 0.17 + 1.2, -4.24], [0.6, 0.5, 0.03], { uv: 1 });
      m.box(['neon_pink', 'neon_cyan', 'neon_amber'][bi % 3], [ax, 0.17 + 0.6, -4.24], [0.5, 0.06, 0.03], { uv: 1 });
    }
  } else {
    // roof: pipes along the wall, vents, a tank
    m.cyl('steel', [cx, 1.4, zf + 0.35], 0.12, w + 0.2, { axis: 'x', n: 12, uv: 1.2 });
    m.cyl('steel_dark', [cx, 2.2, zf + 0.35], 0.08, w + 0.2, { axis: 'x', n: 10, uv: 1 });
    if (hf(bi, 51) > 0.5) {
      const vx = cx + (hf(bi, 52) - 0.5) * (w - 2);
      m.box('steel', [vx, 0.17 + 0.5, -4.7], [1.0, 1.0, 0.9], { uv: 1.2 });
      m.box('steel_dark', [vx, 0.17 + 1.05, -4.7], [1.1, 0.1, 1.0], { uv: 1 });
      m.cyl('glow_orange', [vx, 0.17 + 0.9, -4.2], 0.2, 0.02, { axis: 'z', n: 12, uv: 1, r1: 0.2 });
    }
    if (hf(bi, 53) > 0.75) {
      m.cyl('steel_red', [cx, 5.4, zf - 1.6], 1.2, 2.4, { n: 20, uv: 2 });
      for (const sd of [-1, 1]) m.box('steel_dark', [cx + sd * 0.8, 2.5, zf - 1.6], [0.12, 4.2, 0.12], { uv: 1 });
    }
  }
}

function writeStage(i) {
  const th = THEMES[i];
  const glb = new Glb(), M = new Mats(glb);
  DEFS['g'] = DEFS[th.ground]; DEFS['p'] = DEFS[th.pave];
  const m = new Mesher();
  street(m, th, i);
  const mi = {};
  for (const k of m.groups.keys()) mi[k] = M.get(k);
  const mesh = glb.mesh('stage' + i, m.prims(mi));
  glb.sceneNodes.push(glb.node({ name: 'Scenery', mesh }));
  glb.write(join(OUT, `stage${i}.glb`));
  console.log('stage', i, [...m.groups].reduce((a, [, g]) => a + g.idx.length / 3, 0), 'tris');
}

// ── breakable props, centred on the origin so the game places them by centre ──
function writeProp(name, fn) {
  const glb = new Glb(), M = new Mats(glb), m = new Mesher();
  fn(m);
  const mi = {};
  for (const k of m.groups.keys()) mi[k] = M.get(k);
  glb.sceneNodes.push(glb.node({ name, mesh: glb.mesh(name, m.prims(mi)) }));
  glb.write(join(OUT, `${name}.glb`));
  console.log(name);
}
function crate(m) {
  m.box('wood', [0, 0, 0], [0.84, 0.84, 0.84], { uv: 0.84 });
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) m.box('wood_dark', [sx * 0.4, 0, sz * 0.4], [0.1, 0.9, 0.1], { uv: 0.6 });
  for (const sy of [-1, 1]) { m.box('wood_dark', [0, sy * 0.4, 0], [0.9, 0.1, 0.9], { uv: 0.6 }); }
  for (const sd of [-1, 1]) {
    m.box('steel', [0, 0, sd * 0.44], [0.12, 0.88, 0.02], { uv: 0.5 });
    m.box('steel', [sd * 0.44, 0, 0], [0.02, 0.88, 0.12], { uv: 0.5 });
  }
  // diagonal brace on the front, as two thin boards
  m.box('wood_dark', [0, 0, 0.43], [0.06, 0.06, 0.04], { uv: 0.3 });
}
function barrel(m) {
  m.cyl('steel_blue', [0, 0, 0], 0.33, 0.98, { n: 24, uv: 1.2, caps: true });
  for (const y of [-0.34, 0, 0.34]) m.cyl('steel_blue', [0, y, 0], 0.355, 0.07, { n: 24, uv: 1, caps: false });
  m.cyl('steel_dark', [0, 0.5, 0], 0.30, 0.04, { n: 24, uv: 1 });
  m.cyl('steel', [0.1, 0.52, 0.08], 0.05, 0.04, { n: 10, uv: 0.5 });
}
function bin(m) {
  m.box('metal_green', [0, 0, 0], [0.8, 0.96, 0.7], { uv: 1.2 });
  m.box('plastic', [0, 0.54, -0.02], [0.86, 0.12, 0.76], { uv: 1 });
  m.box('plastic', [0, 0.6, 0.3], [0.8, 0.04, 0.08], { uv: 1 });
  for (const sx of [-1, 1]) m.cyl('steel_dark', [sx * 0.34, -0.5, -0.26], 0.07, 0.08, { axis: 'x', n: 10, uv: 0.5 });
  m.box('steel_dark', [0, -0.46, 0], [0.82, 0.06, 0.72], { uv: 1 });
}
/** a soft round shadow under every actor (multiplied onto the ground) */
function blobMesh() {
  const glb = new Glb();
  const mat = glb.pbr('blob', TEX, null, { alphaMode: 'BLEND' });
  glb.materials[mat].pbrMetallicRoughness.baseColorTexture = { index: glb.tex(join(TEX, 'decal_blob.png')) };
  glb.materials[mat].pbrMetallicRoughness.baseColorFactor = [1, 1, 1, 1];
  glb.materials[mat].pbrMetallicRoughness.metallicFactor = 0; glb.materials[mat].pbrMetallicRoughness.roughnessFactor = 1;
  glb.materials[mat].extensions = undefined;
  const g = { pos: [-1, 0, 1, 1, 0, 1, 1, 0, -1, -1, 0, -1], nrm: [0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0], uv: [0, 1, 1, 1, 1, 0, 0, 0], idx: [0, 1, 2, 0, 2, 3] };
  glb.sceneNodes.push(glb.node({ name: 'Blob', mesh: glb.mesh('blob', g, mat) }));
  glb.write(join(OUT, 'blob.glb'));
  console.log('blob');
}

for (let i = 0; i < 4; i++) writeStage(i);
writeProp('crate', crate); writeProp('barrel', barrel); writeProp('bin', bin);
blobMesh();
