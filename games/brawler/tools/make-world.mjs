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
  // pickups and weapons (small: plain PBR factors, no texture sets)
  gold: { set: null, tint: [1.0, 0.71, 0.29], rough: 0.3, metal: 1, emissive: [0.16, 0.1, 0.02] },
  gold_dark: { set: null, tint: [0.72, 0.48, 0.16], rough: 0.45, metal: 1 },
  apple: { set: null, tint: [0.55, 0.05, 0.04], rough: 0.38 }, leaf: { set: null, tint: [0.12, 0.3, 0.06], rough: 0.7 },
  glass_green: { set: null, tint: [0.05, 0.2, 0.08], rough: 0.06 }, label: { set: null, tint: [0.75, 0.68, 0.5], rough: 0.85 },
  blade: { set: 'paint_metal', tint: [0.9, 0.91, 0.94], rough: 0.2, metal: 1 }, tape: { set: null, tint: [0.06, 0.06, 0.07], rough: 0.85 },
  pipe: { set: 'paint_metal', tint: [0.42, 0.44, 0.46], rough: 0.5, metal: 1 },
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

// ── the facade grid ────────────────────────────────────────────────────────────────────────────────────────────────
// The window textures have 4 x 4 cells per FT metres: a bay (one window column) is BAY metres wide and a floor BAY metres
// tall. A building is MARGIN + n bays + MARGIN wide, its grid starts on the plinth top (y0), its height is whole floors
// plus a parapet. Buildings fill the street between lamp posts (every 11 m from x = 2, where the game puts the lamp
// lights), so a lamp post always stands in a gap between buildings, never in front of a window; gaps are a seam or an
// alley. Doors replace a ground-floor bay, shopfronts two, pipes run down bay boundaries, fire escapes and signs sit on
// window columns and floor lines: nothing overlaps a window.
const FT = 7.2, BAY = FT / 4, MARGIN = 0.3;
const bw = (n) => n * BAY + 2 * MARGIN;
function layout(stage, th) {
  const out = [];
  let bi = 0;
  const floorsMin = Math.max(3, Math.round((th.hmin - 1) / BAY)), floorsMax = Math.max(floorsMin + 1, Math.round((th.hmax - 1) / BAY));
  const put = (x0, n) => {
    const y0 = 0.95, fl = floorsMin + Math.floor(hf(bi, stage + 2) * (floorsMax - floorsMin + 1));
    const B = { bi, n, x0: x0 + MARGIN, y0, w: bw(n), cx: x0 + bw(n) / 2, h: y0 + fl * BAY + 0.35, floors: fl, zf: -6.0 - hf(bi, 9) * 0.4 };
    B.door = hf(bi, 11) > 0.3 ? Math.floor(hf(bi, 12) * n) : -1;
    B.shop = n >= 3 && hf(bi, 14) > 0.55 ? Math.floor(hf(bi, 15) * (n - 1)) : -1;
    if (B.shop >= 0 && B.door >= B.shop && B.door <= B.shop + 1) B.door = B.shop + 2 < n ? B.shop + 2 : B.shop - 1 >= 0 ? B.shop - 1 : -1;
    B.pipe = hf(bi, 13) > 0.5 ? 1 + Math.floor(hf(bi, 16) * (n - 1)) : -1; // an inner boundary between two bays
    out.push(B); bi++;
  };
  // [buildings' bay counts, inner gap] per 11 m segment; the rest of the segment is the gap around the lamp post
  const FILL = [[[5], 0], [[4], 0], [[2, 2], 0.4], [[3, 2], 0.2], [[2, 3], 0.2], [[3], 0]];
  for (let L = 2 - 11 * Math.ceil((2 - X0) / 11); L < X1 + 11; L += 11) {
    const k = Math.floor(hf(Math.round(L) + 500, stage + 61) * FILL.length);
    const [ns, inner] = FILL[k];
    const total = ns.reduce((a, n) => a + bw(n), 0) + inner * (ns.length - 1);
    let x = L + (11 - total) / 2; // centred between two lamp posts: the gaps at both posts are equal
    for (const n of ns) { put(x, n); x += bw(n) + inner; }
  }
  return out;
}

function street(m, th, stage) {
  // roadway, kerb, pavement
  m.box('g', [(X0 + X1) / 2, -0.15, 1.2], [X1 - X0, 0.3, 9.2], { uv: 4 });
  m.box('p', [(X0 + X1) / 2, 0.085, -3.575], [X1 - X0, 0.17, 0.35], { uv: 1 });
  m.box('p', [(X0 + X1) / 2, 0.085, -4.95], [X1 - X0, 0.17, 2.4], { uv: 2 });
  // kerb every few metres: a dark joint line is in the texture; add a drain grate and manholes
  for (let x = X0 + 2; x < X1; x += 9 + hf(x, 3) * 5) m.box('steel_dark', [x, 0.01, -3.1], [0.9, 0.02, 0.24], { uv: 1 });
  // the street's far lane edge: a deep void under the camera view
  // building row A, close behind the pavement: every building is laid out on its facade grid first (layout())
  for (const B of layout(stage, th)) {
    const { cx, w, h, zf, bi, x0, y0, n } = B, d = 5;
    // the window texture's cells (BAY metres square) start exactly at the building's first bay and the plinth top
    m.box({ front: th.facade, side: th.wall, top: th.trim }, [cx, h / 2, zf - d / 2], [w, h, d], { uv: 2.4, facade: { tile: FT, ox: -x0 / FT, oy: y0 / FT } });
    // plinth (below the grid), cornice, pilasters on the side margins (never over a window)
    m.box(th.base, [cx, (y0 - 0.05) / 2, zf + 0.04], [w + 0.1, y0 - 0.05, 0.16], { uv: 2 });
    m.box(th.trim, [cx, h + 0.17, zf + 0.1], [w + 0.3, 0.35, 0.5], { uv: 1.5 });
    m.box(th.trim, [cx, h - 0.2, zf + 0.05], [w + 0.1, 0.12, 0.2], { uv: 1.5 });
    for (const sd of [-1, 1]) m.box(th.trim, [cx + sd * (w / 2 - MARGIN / 2), h / 2, zf + 0.07], [MARGIN - 0.04, h, 0.16], { uv: 1.5 });
    // a door replaces one ground-floor bay (it covers that bay's window and nothing else)
    if (B.door >= 0) {
      const dx = x0 + (B.door + 0.5) * BAY;
      m.box('steel_dark', [dx, 1.25, zf + 0.12], [1.3, 2.5, 0.12], { uv: 1 });
      m.box(th.trim, [dx, 2.55, zf + 0.15], [1.6, 0.14, 0.2], { uv: 1 });
      m.box('steel_yellow', [dx + 0.45, 1.2, zf + 0.2], [0.05, 0.2, 0.05], { uv: 1 });
    }
    // a shopfront over two ground-floor bays: a dark glass front in a frame, a lit sign band above it
    if (B.shop >= 0) {
      const sx = x0 + (B.shop + 1) * BAY, sw = 2 * BAY - 0.2;
      m.box('steel_dark', [sx, y0 + BAY / 2 - 0.1, zf + 0.1], [sw + 0.12, BAY + 0.1, 0.12], { uv: 1 });
      m.box('plastic', [sx, y0 + BAY / 2 - 0.1, zf + 0.17], [sw - 0.1, BAY - 0.15, 0.04], { uv: 1 });
      m.box(['neon_amber', 'neon_pink', 'neon_cyan', 'glow_orange'][bi % 4], [sx, y0 + BAY + 0.12, zf + 0.18], [sw * 0.8, 0.22, 0.05], { uv: 1 });
    }
    // a drain pipe down a bay boundary (the wall strip between two window columns)
    if (B.pipe >= 0) m.cyl('steel', [x0 + B.pipe * BAY, h / 2, zf + 0.2], 0.07, h, { n: 10, uv: 1 });
    stageDressing(m, th, stage, B);
  }
  // row B, taller, further back: whole cells of its own window tile, so no window is cut either
  let bx = X0 - 6, bi = 100;
  while (bx < X1 + 10) {
    const FB = 10, cb = FB / 4, nb = 3 + Math.floor(hf(bi, stage + 3) * 3), w = nb * cb, fl = 6 + Math.floor(hf(bi, stage + 4) * 6), h = fl * cb + 0.6;
    m.box({ front: 'facade_far', side: th.wall, top: th.trim }, [bx + w / 2, h / 2, -17 - hf(bi, 9) * 3], [w, h, 8], { uv: 3, facade: { tile: FB, ox: -bx / FB, oy: 0 } });
    bx += w + 0.5 + hf(bi, 7) * 1.5; bi++;
  }
  // lamp posts every 11 m
  for (let x = 2; x < X1; x += 11) lampPost(m, x, th.bulb);
}

function stageDressing(m, th, stage, B) {
  const { cx, w, h, zf, bi, x0, y0, n } = B;
  const freeBay = (salt) => { for (let t = 0; t < n; t++) { const j = (Math.floor(hf(bi, salt) * n) + t) % n; if (j !== B.door && !(B.shop >= 0 && (j === B.shop || j === B.shop + 1))) return j; } return -1; };
  if (stage === 0) {
    // fire escape landings and ladders on some buildings: centred on a window column, landings on the floor lines
    if (hf(bi, 21) > 0.5 && B.floors >= 4) {
      const fx = x0 + (Math.floor(hf(bi, 22) * n) + 0.5) * BAY;
      for (let k = 0; k < Math.min(3, B.floors - 2); k++) {
        const y = y0 + (k + 2) * BAY;
        m.box('plate', [fx, y, zf + 0.75], [2.4, 0.08, 1.3], { uv: 1.2 });
        m.box('steel_dark', [fx, y + 0.5, zf + 1.4], [2.4, 0.05, 0.05], { uv: 1 });
        for (const sd of [-1, 1]) m.box('steel_dark', [fx + sd * 1.2, y + 0.25, zf + 1.4], [0.05, 0.5, 0.05], { uv: 1 });
        // stair run down to the landing below
        for (let s = 0; s < 8; s++) m.box('plate', [fx + 1.0 - s * 0.0, y - 0.22 - s * 0.36, zf + 0.5 + s * 0.0 + 0.9], [0.8, 0.05, 0.28], { uv: 1 });
      }
    }
  } else if (stage === 1) {
    // a stack of containers against some buildings
    const doorX = B.door >= 0 ? x0 + (B.door + 0.5) * BAY : 1e9;
    if (hf(bi, 31) > 0.45 && Math.abs(doorX - (cx + (hf(bi, 33) - 0.5) * 2)) > 3.8) { // never in front of the door
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
    if (hf(bi, 41) > 0.5 && n >= 2) { // a blade sign on the wall strip between two window columns, across a floor line
      const nm = ['neon_pink', 'neon_cyan', 'neon_amber'][Math.floor(hf(bi, 42) * 3) % 3];
      let k = 1 + Math.floor(hf(bi, 45) * (n - 1));
      if (k === B.pipe) k = k > 1 ? k - 1 : k + 1 < n ? k + 1 : -1;
      const sx = x0 + k * BAY, sy = y0 + 2 * BAY;
      if (k > 0) {
        m.box('steel_dark', [sx, sy, zf + 0.55], [0.12, 1.5, 0.6], { uv: 1 });
        m.box('steel_dark', [sx, sy, zf + 0.9], [0.5, 1.4, 0.08], { uv: 1 });
        m.box(nm, [sx, sy, zf + 0.96], [0.36, 1.2, 0.03], { uv: 1 });
      }
    }
    // an arcade cabinet against a bay with no door or shopfront
    const cb = freeBay(44);
    if (hf(bi, 43) > 0.55 && cb >= 0) {
      const ax = x0 + (cb + 0.5) * BAY;
      m.box('steel_dark', [ax, 0.17 + 0.9, -4.6], [0.8, 1.8, 0.7], { uv: 1 });
      m.box('screen', [ax, 0.17 + 1.2, -4.24], [0.6, 0.5, 0.03], { uv: 1 });
      m.box(['neon_pink', 'neon_cyan', 'neon_amber'][bi % 3], [ax, 0.17 + 0.6, -4.24], [0.5, 0.06, 0.03], { uv: 1 });
    }
  } else {
    // roof: pipes along the wall, vents, a tank
    // pipes along the plinth and along the first floor line, between the window rows
    m.cyl('steel', [cx, y0 - 0.3, zf + 0.35], 0.12, w + 0.2, { axis: 'x', n: 12, uv: 1.2 });
    m.cyl('steel_dark', [cx, y0 + BAY, zf + 0.35], 0.08, w + 0.2, { axis: 'x', n: 10, uv: 1 });
    if (hf(bi, 51) > 0.5) {
      const vb = freeBay(52), vx = vb >= 0 ? x0 + (vb + 0.5) * BAY : cx;
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
/** a turned shape along y: profile [[y, r], ...] bottom to top, closed with caps where r > 0 at the ends */
function lathe(m, mat, prof, o = {}) {
  const n = o.n ?? 16;
  for (let i = 0; i + 1 < prof.length; i++) {
    const [y0, r0] = prof[i], [y1, r1] = prof[i + 1];
    m.cyl(mat, [0, (y0 + y1) / 2, 0], r0, y1 - y0, { r1, n, uv: o.uv ?? 0.3, caps: false });
  }
  const [yb, rb] = prof[0], [yt, rt] = prof[prof.length - 1];
  if (rb > 0) m.cyl(mat, [0, yb - 0.0005, 0], rb, 0.001, { n, uv: o.uv ?? 0.3 });
  if (rt > 0) m.cyl(mat, [0, yt + 0.0005, 0], rt, 0.001, { n, uv: o.uv ?? 0.3 });
}
// pickups and held weapons, modelled along +y and centred on the origin (the game holds them in a hand, upright, and
// lays them on the street turned onto their side); they replace plain boxes
function bat(m) { // 0.85 m: knob, taped handle, tapering to a round barrel
  lathe(m, 'wood', [[-0.425, 0.0], [-0.42, 0.028], [-0.405, 0.03], [-0.395, 0.017], [-0.1, 0.019], [0.1, 0.03], [0.38, 0.036], [0.415, 0.03], [0.425, 0.0]], { n: 18, uv: 0.4 });
  lathe(m, 'tape', [[-0.39, 0.019], [-0.2, 0.0205]], { n: 18 });
}
function pipe(m) { // 1 m of steel pipe with a threaded coupling on each end
  m.cyl('pipe', [0, 0, 0], 0.024, 0.94, { n: 16, uv: 0.5 });
  for (const y of [-0.47, 0.47]) m.cyl('steel_dark', [0, y, 0], 0.031, 0.06, { n: 16, uv: 0.5 });
}
function knife(m) { // 0.34 m: a handle, a guard and a blade narrowing to the point
  lathe(m, 'tape', [[-0.17, 0.0], [-0.168, 0.014], [-0.06, 0.016], [-0.05, 0.0]], { n: 12 });
  m.box('steel_dark', [0, -0.045, 0], [0.06, 0.012, 0.018], { uv: 0.2 });
  m.box('blade', [0, 0.02, 0], [0.034, 0.12, 0.004], { uv: 0.2 });
  m.cyl('blade', [0, 0.125, 0], 0.017, 0.09, { r1: 0.001, n: 4, uv: 0.2 }); // the point: a thin four-sided taper
}
function coin(m) { // a fat coin standing up, its face to the street: rim, face and a raised centre
  m.cyl('gold', [0, 0, 0], 0.1, 0.018, { axis: 'z', n: 28, uv: 0.3 });
  m.cyl('gold_dark', [0, 0, 0], 0.104, 0.012, { axis: 'z', n: 28, uv: 0.3, caps: false });
  m.cyl('gold', [0, 0, 0], 0.055, 0.024, { axis: 'z', n: 20, uv: 0.3 });
}
function apple(m) { // a round apple with a dimple, a stem and a leaf
  const prof = [];
  for (let i = 0; i <= 12; i++) { const a = -Math.PI / 2 + Math.PI * i / 12; prof.push([Math.sin(a) * 0.12 - (i === 12 ? 0.015 : 0), Math.max(0.0, Math.cos(a) * 0.13 * (1 - 0.08 * Math.sin(a)))]); }
  lathe(m, 'apple', prof, { n: 20 });
  m.cyl('wood_dark', [0, 0.13, 0], 0.007, 0.06, { r1: 0.005, n: 6, uv: 0.2 });
  m.box('leaf', [0.03, 0.14, 0], [0.05, 0.004, 0.025], { uv: 0.2 });
}
function bottle(m) { // 0.28 m green glass bottle with a paper label
  lathe(m, 'glass_green', [[-0.14, 0.0], [-0.138, 0.034], [-0.13, 0.036], [0.03, 0.036], [0.07, 0.022], [0.09, 0.014], [0.13, 0.013], [0.14, 0.0]], { n: 16 });
  lathe(m, 'label', [[-0.08, 0.0372], [0.0, 0.0372]], { n: 16 });
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

/** puddles: one glb per wet stage, every puddle a quad cut from the 8-shape atlas (make-textures puddles), turned,
 * stretched and flipped from a seeded random stream (the same every build), placed where water collects: strips along the
 * kerb by the drain grates, a splash under every drain pipe, pools in low spots of the road (more of them towards the
 * gutters, where the road's camber sends the water) and clusters of small splashes. No two puddles overlap: every
 * candidate is an oriented box (the shape's footprint plus a gap) tested against the ones already placed, and one
 * that touches another is dropped (stacked alpha drew dark blotches). Alpha blended over the street, glossy. */
function puddles(stage, count) {
  let st = (0x9e3779b9 ^ (stage * 7919 + 13)) >>> 0;
  const rnd = () => { st = (st + 0x6d2b79f5) >>> 0; let t = st; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  const r = (a, b) => a + (b - a) * rnd();
  const P = []; // [x, y, z, halfX, halfZ, rot, kind, flip]
  const GAP = 0.35; // metres of dry street between two puddles at least
  // separating axis test of two oriented rectangles in the street plane (each grown by half the gap)
  const overlaps = (A, B) => {
    const ax = [[Math.cos(A[5]), Math.sin(A[5])], [-Math.sin(A[5]), Math.cos(A[5])]];
    const bx = [[Math.cos(B[5]), Math.sin(B[5])], [-Math.sin(B[5]), Math.cos(B[5])]];
    const ah = [A[3] + GAP / 2, A[4] + GAP / 2], bh = [B[3] + GAP / 2, B[4] + GAP / 2];
    const d = [B[0] - A[0], B[2] - A[2]];
    for (const n of [...ax, ...bx]) {
      const pa = ah[0] * Math.abs(n[0] * ax[0][0] + n[1] * ax[0][1]) + ah[1] * Math.abs(n[0] * ax[1][0] + n[1] * ax[1][1]);
      const pb = bh[0] * Math.abs(n[0] * bx[0][0] + n[1] * bx[0][1]) + bh[1] * Math.abs(n[0] * bx[1][0] + n[1] * bx[1][1]);
      if (Math.abs(d[0] * n[0] + d[1] * n[1]) > pa + pb) return false;
    }
    return true;
  };
  const add = (x, y, z, hx, hz, rot, kind) => {
    const c = [x, y, z, hx, hz, rot, kind, rnd() < 0.5];
    // the texture's shapes stay inside 90% of their cell: test that footprint
    const f = [x, y, z, hx * 0.9, hz * 0.9, rot];
    if (P.some((q) => q[1] === y && overlaps([q[0], q[1], q[2], q[3] * 0.9, q[4] * 0.9, q[5]], f))) return false;
    P.push(c);
    return true;
  };
  const LO = -10, HI = 80; // the part of the street a stage plays on
  // the kerb: drain grates (same spacing as street()) and drain pipes (the same building walk)
  for (let x = X0 + 2; x < X1; x += 9 + hf(x, 3) * 5) if (x > LO && x < HI && rnd() < 0.7) add(x + r(-1.2, 1.2), 0, r(-3.12, -2.95), r(0.9, 1.8), r(0.18, 0.32), r(-0.06, 0.06), rnd() < 0.6 ? 3 : 4);
  for (const B of layout(stage, THEMES[stage])) {
    if (B.pipe >= 0) {
      const px = B.x0 + B.pipe * BAY;
      if (px > LO && px < HI) {
        add(px + r(-0.2, 0.2), 0.172, r(-5.95, -5.6), r(0.25, 0.45), r(0.2, 0.35), r(0, 6.28), 6 + (rnd() < 0.5 ? 1 : 0));
        add(px + r(-0.6, 0.6), 0, r(-3.1, -2.98), r(1.0, 1.6), r(0.16, 0.28), r(-0.05, 0.05), 3);
      }
    }
  }
  // low spots in the road: the water runs to the kerb gutter, pools sit in dips mid road; small splash clusters near a pool. Up to 6 tries each to find dry street.
  const kinds = [0, 1, 2, 5];
  for (let i = 0; i < count; i++) {
    for (let tries = 0; tries < 6; tries++) {
      const g = rnd(), z = g < 0.5 ? r(-2.6, -1.6) : r(-1.4, 1.9); // the kerb gutter, or dips in the road (the near gutter lies under the bottom of the picture, where the street lamps mirror: a cut-off dark shape with a hot spot)
      const x = r(LO, HI), s = r(0.45, 1.4) * (g < 0.5 ? 1 : 0.8), asp = r(0.5, 1.5), rot = r(0, 6.28);
      if (!add(x, 0, z, s * asp, s / asp, rot, kinds[Math.floor(rnd() * 4)])) continue;
      if (rnd() < 0.3) add(x + r(-1.8, 1.8), 0, Math.min(z + r(-0.9, 0.9), 2.0), r(0.25, 0.55), r(0.25, 0.55), r(0, 6.28), 6 + (rnd() < 0.5 ? 1 : 0));
      break;
    }
  }
  const pos = [], nrm = [], uv = [], idx = [];
  P.forEach(([x, y, z, hx, hz, rot, k, flip], n) => {
    const c = Math.cos(rot), sn = Math.sin(rot), base = pos.length / 3, yy = y + 0.02 + n * 0.0004; // clear of the street in the depth buffer (4 mm fought it at 10 m)
    const u0 = (k % 4) / 4, v0 = Math.floor(k / 4) / 2;
    for (const [a, b] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
      const lx = a * hx, lz = b * hz;
      pos.push(x + lx * c - lz * sn, yy, z + lx * sn + lz * c);
      nrm.push(0, 1, 0);
      const fu = flip ? 1 - (a + 1) / 2 : (a + 1) / 2;
      uv.push(u0 + fu * 0.25, v0 + (b + 1) / 2 * 0.5);
    }
    idx.push(base, base + 2, base + 1, base, base + 3, base + 2);
  });
  const glb = new Glb();
  const mat = glb.pbr('puddle', TEX, null, { alphaMode: 'BLEND' });
  const pm = glb.materials[mat].pbrMetallicRoughness;
  pm.baseColorTexture = { index: glb.tex(join(TEX, 'puddles.png')) };
  pm.baseColorFactor = [1, 1, 1, 1]; pm.metallicFactor = 0; pm.roughnessFactor = 0.12;
  glb.materials[mat].extensions = undefined;
  glb.sceneNodes.push(glb.node({ name: 'Puddles', mesh: glb.mesh('puddles', { pos, nrm, uv, idx }, mat) }));
  glb.write(join(OUT, `puddles${stage}.glb`));
  console.log('puddles', stage, P.length);
}

for (let i = 0; i < 4; i++) writeStage(i);
puddles(0, 22); puddles(1, 55); puddles(3, 10);
writeProp('crate', crate); writeProp('barrel', barrel); writeProp('bin', bin);
for (const [n, fn] of [['bat', bat], ['pipe', pipe], ['knife', knife], ['coin', coin], ['apple', apple], ['bottle', bottle]]) writeProp(n, fn);
blobMesh();

// ── the jukebox in the docks boss arena (Arthur thumps it): base on the floor at the origin, facing +Z. Its glow tubes,
// window and buttons are their own materials (juke_glow, juke_window, juke_button) so the game can light them.
DEFS.chrome = { set: 'paint_metal', tint: [0.86, 0.86, 0.88], rough: 0.28, metal: 1 };
DEFS.juke_wood = { set: 'wood', tint: [0.42, 0.2, 0.12] };
DEFS.juke_red = { set: 'paint_metal', tint: [0.55, 0.06, 0.05], rough: 0.45 };
DEFS.juke_glow = { set: null, tint: [0.9, 0.5, 0.25], rough: 0.3, emissive: [0.6, 0.25, 0.08] };
DEFS.juke_window = { set: null, tint: [0.12, 0.1, 0.08], rough: 0.15, emissive: [0.25, 0.18, 0.1] };
DEFS.juke_button = { set: null, tint: [0.9, 0.85, 0.7], rough: 0.4, emissive: [0.3, 0.25, 0.15] };
function jukebox(m) {
  const W = 0.92, D = 0.56, Hb = 0.98, R = W / 2;
  m.box({ all: 'juke_wood', front: 'juke_red' }, [0, Hb / 2 + 0.06, 0], [W, Hb, D], { uv: 1 });
  m.cyl('juke_wood', [0, Hb + 0.06, 0], R, D, { axis: 'z', n: 32, uv: 1 }); // the arched top
  m.cyl('chrome', [0, Hb + 0.06, 0.005], R + 0.025, D - 0.06, { axis: 'z', n: 32, uv: 1, caps: false }); // a chrome band round the arch
  // glow tubes: up both front corners and round the arch, a little proud of the face
  for (const sx of [-1, 1]) m.cyl('juke_glow', [sx * (R - 0.05), 0.55, D / 2 + 0.02], 0.035, 0.9, { n: 12, uv: 1 });
  for (let i = 0; i < 20; i++) {
    const a0 = (i / 20) * Math.PI, a1 = ((i + 1) / 20) * Math.PI, rr = R - 0.05, cy = Hb + 0.06;
    const p0 = [Math.cos(a0) * rr, cy + Math.sin(a0) * rr], p1 = [Math.cos(a1) * rr, cy + Math.sin(a1) * rr];
    m.box('juke_glow', [(p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, D / 2 + 0.02], [Math.hypot(p1[0] - p0[0], p1[1] - p0[1]) + 0.01, 0.06, 0.06], { uv: 1 });
  }
  // the window over the record and the selector, chrome framed; a row of buttons; the speaker grille with slats
  m.box('juke_window', [0, 1.02, D / 2 + 0.01], [0.6, 0.42, 0.02], { uv: 1 });
  m.box('chrome', [0, 1.245, D / 2 + 0.012], [0.66, 0.03, 0.03], { uv: 1 }); m.box('chrome', [0, 0.795, D / 2 + 0.012], [0.66, 0.03, 0.03], { uv: 1 });
  for (const sx of [-1, 1]) m.box('chrome', [sx * 0.315, 1.02, D / 2 + 0.012], [0.03, 0.48, 0.03], { uv: 1 });
  for (let i = 0; i < 8; i++) m.box('juke_button', [-0.245 + i * 0.07, 0.72, D / 2 + 0.02], [0.05, 0.035, 0.03], { uv: 1 });
  m.box('steel_dark', [0, 0.38, D / 2 + 0.008], [0.62, 0.5, 0.02], { uv: 1 });
  for (let i = 0; i < 9; i++) m.box('chrome', [0, 0.17 + i * 0.052, D / 2 + 0.02], [0.6, 0.012, 0.012], { uv: 1 });
  m.box('chrome', [0, 0.03, 0], [W + 0.04, 0.06, D + 0.04], { uv: 1 }); // the plinth
}
{
  const glb = new Glb(), M = new Mats(glb), m = new Mesher();
  jukebox(m);
  const mi = {};
  for (const k of m.groups.keys()) mi[k] = M.get(k);
  glb.sceneNodes.push(glb.node({ name: 'jukebox', mesh: glb.mesh('jukebox', m.prims(mi)) }));
  glb.write(join(OUT, 'jukebox.glb'));
  console.log('jukebox');
}
