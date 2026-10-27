// Generates the three stage backgrounds as textured glb files with PBR materials into assets/models/ (stage1..3.glb).
// Run after make-textures.mjs:   node tools/make-world.mjs [1 2 3]
// The fight happens on the plane z = 0 between x = -4.4 and 4.4; the camera looks down -z from 5 to 15 m away, so the
// floor runs toward the camera and everything else is layered behind (near dressing, mid buildings, far silhouettes)
// for depth. Animated pieces are separate named nodes the game moves: Fan* (spin about y), Blink* (aircraft lights),
// Lamp* (swaying lanterns, pivot at the top), Sign* (neon that flickers). Every model here is made by our own code.
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb } from './gltf-writer.mjs';
import { Mesher } from './mesher.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const TEX = join(ROOT, 'assets', 'textures'), OUT = join(ROOT, 'assets', 'models');
const hf = (i, salt) => { let h = (Math.imul(i + 1, 2654435761) ^ Math.imul(salt + 7, 40503)) >>> 0; h ^= h >>> 13; h = Math.imul(h, 0x5bd1e995) >>> 0; h ^= h >>> 15; return (h & 0xffff) / 65535; };
const PI = Math.PI;

const DEFS = {
  roof_deck: { set: 'roof_deck' }, roof_tar: { set: 'roof_tar' }, concrete: { set: 'concrete' }, concrete_dark: { set: 'concrete', tint: [0.6, 0.6, 0.62] },
  facade_dusk: { set: 'facade_dusk', emissive: [1.3, 1.3, 1.3] }, facade_office: { set: 'facade_office', emissive: [1.2, 1.2, 1.2] },
  facade_tower: { set: 'facade_tower', emissive: [1.3, 1.3, 1.3] },
  facade_far: { set: 'facade_office', emissive: [0.8, 0.8, 0.8], tint: [0.45, 0.45, 0.55] }, facade_far2: { set: 'facade_dusk', emissive: [0.8, 0.8, 0.8], tint: [0.5, 0.45, 0.48] },
  ground_wet: { set: 'ground_wet' }, brick: { set: 'wall_brick' }, brick_dark: { set: 'wall_brick_dark' },
  facade_alley: { set: 'facade_alley', emissive: [1.2, 1.2, 1.2] }, facade_neon: { set: 'facade_neon', emissive: [1.6, 1.6, 1.6] },
  metal: { set: 'wall_metal' }, plate: { set: 'wall_plate' },
  steel: { set: 'paint_metal', tint: [0.32, 0.33, 0.36], rough: 0.7 }, steel_dark: { set: 'paint_metal', tint: [0.1, 0.1, 0.12] },
  steel_red: { set: 'paint_metal', tint: [0.55, 0.1, 0.07] }, steel_green: { set: 'paint_metal', tint: [0.12, 0.32, 0.2] }, steel_blue: { set: 'paint_metal', tint: [0.12, 0.2, 0.42] },
  wood: { set: 'wood' }, wood_dark: { set: 'wood', tint: [0.45, 0.38, 0.34] }, deck: { set: 'deck' }, stone: { set: 'stone' }, stone_moss: { set: 'stone', tint: [0.75, 0.8, 0.7] },
  plaster_white: { set: 'plaster_white' }, roof_tiles: { set: 'roof_tiles' }, shoji: { set: 'shoji', emissive: [1.1, 1.1, 1.1] },
  lacquer_red: { set: 'lacquer', tint: [0.42, 0.05, 0.03], rough: 0.35, metal: 0 }, lacquer_black: { set: 'lacquer', tint: [0.04, 0.035, 0.03], rough: 0.3, metal: 0 },
  bark: { set: 'bark' }, leaves_pink: { set: 'leaves', tint: [0.95, 0.62, 0.72] }, leaves_green: { set: 'leaves', tint: [0.2, 0.32, 0.14] }, leaves_dark: { set: 'leaves', tint: [0.12, 0.2, 0.1] },
  rock: { set: 'rock' }, mountain: { set: 'rock', tint: [0.42, 0.48, 0.58] }, mountain_far: { set: 'rock', tint: [0.5, 0.58, 0.72] },
  bag: { set: 'paint_metal', tint: [0.02, 0.02, 0.025], rough: 0.25, metal: 0 },
  bulb: { set: null, emissive: [9, 6.5, 3.6] }, bulb_warm: { set: null, emissive: [6, 3.6, 1.6] }, red_light: { set: null, emissive: [12, 0.6, 0.4] },
  neon_pink: { set: null, emissive: [5, 0.7, 2.6] }, neon_cyan: { set: null, emissive: [0.5, 3.6, 4.8] }, neon_amber: { set: null, emissive: [5, 2.8, 0.5] }, neon_violet: { set: null, emissive: [2.2, 0.7, 5] },
  lantern_red: { set: null, tint: [0.6, 0.08, 0.05], emissive: [3.2, 0.7, 0.25] }, paper_lamp: { set: null, tint: [0.9, 0.8, 0.6], emissive: [3.2, 2.0, 0.9] },
  screen_ad: { set: null, tint: [0.05, 0.02, 0.06], emissive: [0.7, 0.25, 1.0] }, screen_blue: { set: null, emissive: [0.5, 1.6, 2.8] }, vending: { set: null, tint: [0.05, 0.06, 0.08], emissive: [0.18, 0.26, 0.42] },
  glass_dark: { set: null, tint: [0.02, 0.025, 0.03], rough: 0.08, metal: 0 },
};
class Mats {
  constructor(glb) { this.glb = glb; this.idx = {}; }
  get(name) {
    if (this.idx[name] !== undefined) return this.idx[name];
    const d = DEFS[name];
    if (!d) throw new Error('no material ' + name);
    return (this.idx[name] = this.glb.pbr(name, TEX, d.set, d));
  }
}

/** a stage file: the static scenery in one node, animated pieces in their own named nodes */
class Stage {
  constructor() { this.m = new Mesher(); this.parts = []; }
  part(name, pos, fn) { const m = new Mesher(); fn(m); this.parts.push({ name, pos, m }); }
  write(file) {
    const glb = new Glb(), M = new Mats(glb);
    const mesh = (m, nm) => { const mi = {}; for (const k of m.groups.keys()) mi[k] = M.get(k); return glb.mesh(nm, m.prims(mi)); };
    glb.sceneNodes.push(glb.node({ name: 'Scenery', mesh: mesh(this.m, 'scenery') }));
    for (const p of this.parts) glb.sceneNodes.push(glb.node({ name: p.name, translation: p.pos, mesh: mesh(p.m, p.name) }));
    glb.write(join(OUT, file));
    let tris = 0; for (const m of [this.m, ...this.parts.map((p) => p.m)]) for (const g of m.groups.values()) tris += g.idx.length / 3;
    console.log(file, tris, 'tris', this.parts.length, 'animated parts');
  }
}

// ── shared pieces ──
/** a city block: a building with a facade, a cornice, a roof slab and a few roof props; bottom at y0 */
function building(m, x, z, w, d, y0, h, facade, side, trim, seed, o = {}) {
  const tile = o.tile ?? 7.2;
  m.box({ front: facade, left: o.sideFacade ? facade : side, right: o.sideFacade ? facade : side, back: side, top: 'roof_tar', bottom: side }, [x, y0 + h / 2, z - d / 2], [w, h, d],
    { uv: 2.4, facade: { tile, ox: Math.floor(hf(seed, 5) * 4) / 4, oy: Math.floor(hf(seed, 6) * 4) / 4 } });
  m.box(trim, [x, y0 + h + 0.25, z - d / 2], [w + 0.4, 0.5, d + 0.4], { uv: 1.5 });
  if (o.props !== false && h > 6) {
    if (hf(seed, 11) > 0.45) { // a water tank on legs
      const tx = x + (hf(seed, 12) - 0.5) * (w - 4), tz = z - d * (0.3 + 0.4 * hf(seed, 13)), ty = y0 + h + 0.5;
      for (const [a, b] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) m.box('steel_dark', [tx + a * 1.0, ty + 1.2, tz + b * 1.0], [0.16, 2.4, 0.16], { uv: 1 });
      m.cyl('wood_dark', [tx, ty + 3.6, tz], 1.5, 2.6, { n: 18, uv: 2 });
      m.cyl('steel_dark', [tx, ty + 5.3, tz], 1.55, 0.8, { n: 18, uv: 2, r1: 0.1 });
    }
    if (hf(seed, 14) > 0.4) m.box('steel', [x + (hf(seed, 15) - 0.5) * (w - 3), y0 + h + 1.0, z - d * 0.5], [2.2, 1.5, 1.8], { uv: 1.2 });
  }
}
/** a row of party bulbs hung between two points with a sag */
function bulbString(m, a, b, sag, n, mat) {
  let prev = null;
  for (let i = 0; i <= n; i++) {
    const t = i / n, p = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t - sag * 4 * t * (1 - t), a[2] + (b[2] - a[2]) * t];
    if (prev) { const c = [(p[0] + prev[0]) / 2, (p[1] + prev[1]) / 2, (p[2] + prev[2]) / 2]; const L = Math.hypot(p[0] - prev[0], p[1] - prev[1], p[2] - prev[2]); m.box('steel_dark', c, [Math.max(0.012, Math.abs(p[0] - prev[0])), 0.012, Math.max(0.012, Math.abs(p[2] - prev[2]))], { uv: 1 }); void L; }
    if (i > 0 && i < n) m.sphere(mat, [p[0], p[1] - 0.08, p[2]], 0.05, 0.065, 0.05, { n: 8, rings: 6 });
    prev = p;
  }
}
/** an air conditioner box with a fan grille; the fan blades are a separate part (Fan*) */
function acUnit(st, m, x, y, z, i) {
  m.box('steel', [x, y + 0.55, z], [1.3, 1.1, 1.1], { uv: 1.1 });
  m.box('steel_dark', [x, y + 1.12, z], [1.35, 0.06, 1.15], { uv: 1 });
  m.cyl('steel_dark', [x, y + 1.16, z], 0.45, 0.04, { n: 20, uv: 1 });
  st.part('Fan' + i, [x, y + 1.2, z], (f) => { for (let k = 0; k < 4; k++) { const a = (k / 4) * PI * 2; f.box('steel', [Math.cos(a) * 0.2, 0, Math.sin(a) * 0.2], [0.34, 0.015, 0.09], { uv: 1 }); } f.cyl('steel_dark', [0, 0, 0], 0.07, 0.04, { n: 10, uv: 1 }); });
}

// ── stage 1: a rooftop at dusk over the city ─────────────────────────────
function stage1() {
  const st = new Stage(), m = st.m;
  // the roof: pavers where we fight, tar beyond, a parapet with a rail at the far edge and at the sides
  m.box('roof_deck', [0, -0.15, 0.5], [32, 0.3, 11], { uv: 1.8 });
  m.box('roof_tar', [0, -0.16, -7.5], [32, 0.3, 5], { uv: 2 });
  m.box({ all: 'concrete', top: 'concrete_dark' }, [0, 0.5, -10.2], [32, 1.0, 0.4], { uv: 2 });
  for (const sx of [-1, 1]) m.box({ all: 'concrete', top: 'concrete_dark' }, [sx * 15.8, 0.5, -2.5], [0.4, 1.0, 15.8], { uv: 2 });
  for (let x = -15; x <= 15; x += 2.5) m.box('steel_dark', [x, 1.5, -10.2], [0.07, 1.0, 0.07], { uv: 1 });
  m.box('steel_dark', [0, 2.0, -10.2], [32, 0.06, 0.06], { uv: 1 });
  m.box('steel_dark', [0, 1.5, -10.2], [32, 0.04, 0.04], { uv: 1 });
  // a skylight frame and roof drains on the deck in front (it reads as a roof, not a stage)
  m.box('steel_dark', [-9.5, 0.12, 3.2], [2.4, 0.24, 1.6], { uv: 1 }); m.box('glass_dark', [-9.5, 0.25, 3.2], [2.2, 0.03, 1.4], { uv: 1 });
  for (const x of [-6, 6]) m.cyl('steel_dark', [x, 0.005, 2.2], 0.18, 0.01, { n: 14, uv: 1 });
  // the stair hut on the right with a door, a lamp and an antenna; a water tower on the left; air conditioners
  m.box({ all: 'concrete', top: 'roof_tar' }, [10.5, 1.6, -7.5], [3.6, 3.2, 3.4], { uv: 2.2 });
  m.box('concrete_dark', [10.5, 3.3, -7.5], [3.9, 0.25, 3.7], { uv: 1.5 });
  m.box('steel_red', [9.6, 1.1, -5.78], [1.0, 2.2, 0.08], { uv: 1 });
  m.box('steel_dark', [9.6, 2.35, -5.7], [0.5, 0.06, 0.2], { uv: 1 }); m.box('bulb', [9.6, 2.3, -5.62], [0.22, 0.05, 0.12], { uv: 1 });
  m.cyl('steel_dark', [11.4, 6.0, -8.2], 0.06, 5.2, { n: 8, uv: 1 }); m.box('steel_dark', [11.4, 7.6, -8.2], [1.4, 0.05, 0.05], { uv: 1 });
  st.part('Blink0', [11.4, 8.65, -8.2], (b) => b.sphere('red_light', [0, 0, 0], 0.09, 0.09, 0.09, { n: 8, rings: 6 }));
  const tx = -10, tz = -7.6;
  for (const [a, b] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) m.box('steel_dark', [tx + a * 1.1, 1.6, tz + b * 1.1], [0.16, 3.2, 0.16], { uv: 1 });
  m.box('steel_dark', [tx, 3.25, tz], [2.8, 0.15, 2.8], { uv: 1 });
  m.cyl('wood_dark', [tx, 5.0, tz], 1.6, 3.4, { n: 24, uv: 2 });
  for (const y of [3.7, 5.0, 6.3]) m.cyl('steel_dark', [tx, y, tz], 1.63, 0.07, { n: 24, uv: 1, caps: false });
  m.cyl('steel_dark', [tx, 7.1, tz], 1.68, 0.9, { n: 24, uv: 2, r1: 0.12 });
  for (let k = 0; k < 8; k++) m.box('steel_dark', [tx + 1.3, 1.0 + k * 0.42, tz + 1.62], [0.4, 0.04, 0.04], { uv: 1 }); // a ladder
  acUnit(st, m, -4.5, 0, -8.6, 0); acUnit(st, m, -2.9, 0, -8.6, 1); acUnit(st, m, 5.5, 0, -8.8, 2);
  m.cyl('steel', [1.5, 0.35, -9.4], 0.18, 9, { axis: 'x', n: 12, uv: 1 }); m.cyl('steel', [1.5, 0.75, -9.5], 0.12, 9, { axis: 'x', n: 10, uv: 1 });
  // string lights from the hut to the water tower over the back of the roof
  bulbString(m, [-8.6, 4.2, -6.4], [8.7, 3.2, -5.8], 0.9, 26, 'bulb_warm');
  bulbString(m, [-8.6, 4.2, -6.4], [-15.8, 2.4, 1.0], 0.6, 14, 'bulb_warm');
  // the city: three rings of buildings below and beyond the roof (street level is 34 m down)
  const Y0 = -34;
  for (let i = 0, x = -60; x < 60; i++) { const w = 10 + hf(i, 1) * 10, h = 30 + hf(i, 2) * 14; building(m, x + w / 2, -22 - hf(i, 3) * 6, w, 14, Y0, h, ['facade_dusk', 'facade_office', 'facade_tower'][i % 3], 'concrete_dark', 'concrete_dark', i); x += w + 3 + hf(i, 4) * 5; }
  for (let i = 0, x = -120; x < 120; i++) { const w = 14 + hf(i, 11) * 16, h = 40 + hf(i, 12) * 50; building(m, x + w / 2, -60 - hf(i, 13) * 25, w, 18, Y0, h, i % 2 ? 'facade_office' : 'facade_tower', 'concrete_dark', 'concrete_dark', i + 50, { tile: 9 }); x += w + 4 + hf(i, 14) * 10; }
  for (let i = 0, x = -260; x < 260; i++) { const w = 18 + hf(i, 21) * 22, h = 50 + hf(i, 22) * 90; building(m, x + w / 2, -150 - hf(i, 23) * 60, w, 20, Y0, h, i % 2 ? 'facade_far' : 'facade_far2', 'concrete_dark', 'concrete_dark', i + 100, { tile: 12, props: false }); if (h > 110) st.part('Blink' + (i + 1), [x + w / 2, Y0 + h + 1.2, -150 - hf(i, 23) * 60 - 10], (b) => b.sphere('red_light', [0, 0, 0], 0.6, 0.6, 0.6, { n: 8, rings: 6 })); x += w + 6 + hf(i, 24) * 12; }
  // a billboard on a near building, lit from below
  m.box('steel_dark', [14, 6, -24], [12, 6.5, 0.4], { uv: 1 });
  m.box('screen_ad', [14, 6, -23.78], [11.2, 5.7, 0.05], { uv: 1 });
  for (const x of [9.5, 18.5]) m.box('steel_dark', [x, 1.5, -24.3], [0.4, 7, 0.4], { uv: 1 });
  st.write('stage1.glb');
}

// ── stage 2: a neon alley at night ───────────────────────────────────────
function fireEscape(m, x, zf, floors) {
  for (let k = 0; k < floors; k++) {
    const y = 3.4 + k * 3.2;
    m.box('plate', [x, y, zf + 0.7], [2.6, 0.08, 1.3], { uv: 1.2 });
    m.box('steel_dark', [x, y + 0.9, zf + 1.35], [2.6, 0.05, 0.05], { uv: 1 });
    for (const s of [-1, 0, 1]) m.box('steel_dark', [x + s * 1.28, y + 0.45, zf + 1.35], [0.05, 0.9, 0.05], { uv: 1 });
    for (let s = 0; s < 9; s++) m.box('plate', [x + 0.7, y - 0.25 - s * 0.33, zf + 0.6 - s * 0.0], [0.9, 0.05, 0.26], { uv: 1 });
  }
}
function sign(st, name, x, y, z, w, h, mat) {
  st.m.box('steel_dark', [x, y, z - 0.08], [w + 0.2, h + 0.2, 0.14], { uv: 1 });
  st.part(name, [x, y, z], (p) => { p.box(mat, [0, 0, 0], [w, h * 0.16, 0.04], { uv: 1 }); p.box(mat, [0, h * 0.34, 0], [w * 0.8, h * 0.12, 0.04], { uv: 1 }); p.box(mat, [0, -h * 0.34, 0], [w * 0.6, h * 0.12, 0.04], { uv: 1 }); for (const s of [-1, 1]) p.box(mat, [s * w * 0.5, 0, 0], [0.05, h, 0.04], { uv: 1 }); });
}
function stage2() {
  const st = new Stage(), m = st.m;
  m.box('ground_wet', [0, -0.15, -4], [36, 0.3, 30], { uv: 2.2 });
  // the back: two buildings with a passage between them that opens onto a lit street
  building(m, -9.5, -6, 13, 8, 0, 15, 'facade_alley', 'brick_dark', 'concrete_dark', 1, { props: false });
  building(m, 9.5, -6, 13, 8, 0, 13, 'facade_alley', 'brick_dark', 'concrete_dark', 2, { props: false });
  for (const sx of [-1, 1]) m.box('brick_dark', [sx * 3.05, 7, -10], [0.1, 14, 8], { uv: 2.6 }); // passage walls
  for (const sx of [-1, 1]) m.box('brick', [sx * 16, 7, 0], [1, 14, 12], { uv: 2.6 }); // the side walls
  for (let i = 0; i < 5; i++) building(m, -16 + i * 8, -22, 7.6, 6, 0, 10 + hf(i, 31) * 8, 'facade_neon', 'brick_dark', 'steel_dark', 10 + i, { props: false, tile: 6 });
  m.box('brick_dark', [0, 15, -30], [60, 30, 1], { uv: 2.6 });
  // dressing along the back wall
  fireEscape(m, -11, -6, 3); fireEscape(m, 8.5, -6, 3);
  for (const x of [-4.2, 4.6, 13]) m.cyl('steel', [x, 7, -5.85], 0.09, 14, { n: 10, uv: 1 });
  m.box('steel_green', [-7, 0.7, -5.2], [2.4, 1.3, 1.2], { uv: 1.2 }); m.box('steel_dark', [-7, 1.42, -5.25], [2.5, 0.12, 1.3], { uv: 1 }); // dumpsters
  m.box('steel_blue', [6.8, 0.7, -5.2], [2.2, 1.3, 1.2], { uv: 1.2 }); m.box('steel_dark', [6.8, 1.42, -5.25], [2.3, 0.12, 1.3], { uv: 1 });
  for (const [x, z, s] of [[-9.6, -4.9, 0.9], [-9.4, -4.9, 0.8], [10.3, -5.2, 0.85]]) m.box('wood', [x, s / 2 + (x === -9.4 ? 0.9 : 0), z], [s, s, s], { uv: 0.9 });
  for (let i = 0; i < 6; i++) m.sphere('bag', [-5 + (i % 3) * 0.55 + (i > 2 ? 11.2 : 0), 0.3, -5.3 + (i % 2) * 0.3], 0.38, 0.32, 0.34, { n: 12, rings: 8, bump: (x, y, z) => 1 + 0.08 * Math.sin(x * 9 + z * 7) + (y < -0.5 ? -0.25 : 0) });
  m.box('glass_dark', [2.0, 1.15, -5.6], [0.8, 1.2, 0.05], { uv: 1 }); m.box('neon_cyan', [2.0, 1.8, -5.57], [0.8, 0.06, 0.02], { uv: 1 }); m.box('neon_amber', [2.0, 0.42, -5.57], [0.5, 0.05, 0.02], { uv: 1 }); m.box('steel_red', [2.0, 1.0, -5.95], [1.1, 2.0, 0.7], { uv: 1 });
  acUnit(st, m, -13, 5.2, -5.3, 0);
  // neon signs on the walls (they flicker: separate parts), and a sign arch over the passage
  sign(st, 'Sign0', -6.5, 5.2, -5.9, 3.2, 1.0, 'neon_pink');
  sign(st, 'Sign1', 6.0, 6.2, -5.9, 2.6, 0.9, 'neon_cyan');
  sign(st, 'Sign2', 12.6, 4.4, -5.9, 2.0, 1.2, 'neon_amber');
  sign(st, 'Sign3', 0, 9.0, -5.7, 4.6, 1.0, 'neon_violet');
  m.box('screen_blue', [-13.2, 2.6, -5.94], [2.2, 1.2, 0.05], { uv: 1 });
  // cables across the alley with red lanterns hanging (they sway: Lamp parts, pivot at the top)
  for (const [z, y] of [[-3.0, 7.2], [-1.0, 7.6]]) {
    m.box('steel_dark', [0, y, z], [32, 0.03, 0.03], { uv: 1 });
    for (let i = 0; i < 7; i++) { const x = -9 + i * 3 + (z > -2 ? 1.5 : 0); st.part('Lamp' + st.parts.length, [x, y, z], (p) => { p.box('steel_dark', [0, -0.25, 0], [0.015, 0.5, 0.015], { uv: 1 }); p.sphere('lantern_red', [0, -0.75, 0], 0.22, 0.3, 0.22, { n: 12, rings: 8, bump: (a, b, c) => 1 + 0.04 * Math.cos(Math.atan2(c, a) * 10) }); p.cyl('steel_dark', [0, -0.45, 0], 0.12, 0.05, { n: 10, uv: 1 }); p.cyl('steel_dark', [0, -1.05, 0], 0.1, 0.05, { n: 10, uv: 1 }); }); }
  }
  st.write('stage2.glb');
}

// ── stage 3: a dojo courtyard on a mountain in the afternoon ─────────────
function tree(m, x, z, h, crown, mat, seed, pine = false) {
  m.cyl('bark', [x, h / 2, z], 0.3, h, { n: 14, uv: 2, r1: 0.2 });
  const branches = pine ? 6 : 5;
  for (let i = 0; i < branches; i++) {
    const a = (i / branches) * PI * 2 + hf(seed, i) * 0.8, y = h * (0.55 + 0.4 * (i / branches)), L = (pine ? 2.6 : 2.2) * (1 - 0.4 * (i / branches));
    const ex = x + Math.cos(a) * L, ez = z + Math.sin(a) * L * 0.6, ey = y + (pine ? 0.2 : 0.9);
    const n = 6;
    for (let k = 0; k < n; k++) { const t0 = k / n, t1 = (k + 1) / n; const p0 = [x + (ex - x) * t0, y + (ey - y) * t0, z + (ez - z) * t0], p1 = [x + (ex - x) * t1, y + (ey - y) * t1, z + (ez - z) * t1]; m.box('bark', [(p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, (p0[2] + p1[2]) / 2], [Math.abs(p1[0] - p0[0]) + 0.14 * (1 - t0), Math.abs(p1[1] - p0[1]) + 0.14 * (1 - t0), Math.abs(p1[2] - p0[2]) + 0.14 * (1 - t0)], { uv: 1 }); }
    const cr = crown * (0.75 + 0.5 * hf(seed, i + 10));
    m.sphere(mat, [ex, ey + 0.2, ez], cr * (pine ? 1.3 : 1), cr * (pine ? 0.45 : 0.7), cr * (pine ? 1.0 : 0.9), { n: 14, rings: 9, uv: 1.4, bump: (a, b, c) => 1 + 0.16 * Math.sin(a * 7 + seed) * Math.cos(c * 6 + i) + 0.1 * Math.sin(b * 9) });
  }
  m.sphere(mat, [x, h + crown * 0.4, z], crown * 1.2, crown * (pine ? 0.5 : 0.8), crown, { n: 16, rings: 10, uv: 1.4, bump: (a, b, c) => 1 + 0.14 * Math.sin(a * 6 + seed) * Math.cos(c * 5) + 0.08 * Math.sin(b * 11) });
}
function stoneLantern(st, m, x, z, i) {
  m.box('rock', [x, 0.15, z], [0.9, 0.3, 0.9], { uv: 1 });
  m.cyl('rock', [x, 0.75, z], 0.16, 0.9, { n: 10, uv: 1 });
  m.box('rock', [x, 1.25, z], [0.7, 0.14, 0.7], { uv: 1 });
  m.box('paper_lamp', [x, 1.55, z], [0.42, 0.42, 0.42], { uv: 1 });
  for (const [a, b] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) m.box('rock', [x + a * 0.24, 1.55, z + b * 0.24], [0.1, 0.46, 0.1], { uv: 1 });
  m.cyl('rock', [x, 1.95, z], 0.62, 0.35, { n: 6, uv: 1, r1: 0.1 });
  void st; void i;
}
function stage3() {
  const st = new Stage(), m = st.m;
  m.box('stone', [0, -0.15, -1], [36, 0.3, 26], { uv: 1.8 });
  // the hall: a raised wooden veranda, red pillars, paper screens lit from inside, a deep curved tiled roof
  m.box('deck', [0, 0.25, -6.6], [24, 0.5, 2.4], { uv: 2 });
  m.box('stone_moss', [0, 0.1, -5.25], [24.4, 0.2, 0.5], { uv: 1.2 }); // the step
  for (let i = 0; i < 9; i++) m.cyl('lacquer_red', [-11 + i * 2.75, 3.0, -5.6], 0.2, 5.5, { n: 14, uv: 1 });
  for (let i = 0; i < 8; i++) { m.box('shoji', [-9.625 + i * 2.75, 2.7, -7.7], [2.5, 4.4, 0.08], { uv: 2.5 }); }
  m.box('plaster_white', [0, 6.1, -7.7], [24, 1.6, 0.3], { uv: 2 });
  m.box('lacquer_black', [0, 5.6, -5.6], [24.4, 0.4, 0.4], { uv: 1 });
  for (const sx of [-1, 1]) m.box('plaster_white', [sx * 12.1, 3.2, -8.5], [0.4, 6.4, 6], { uv: 2 });
  // the roof: rows of curved tiles from the eaves (z -4, rising at the corners) up to the ridge
  const rows = [];
  for (let j = 0; j <= 10; j++) {
    const t = j / 10, row = [];
    for (let i = 0; i <= 24; i++) {
      const u = i / 24, x = -14 + 28 * u, edge = Math.pow(Math.abs(u - 0.5) * 2, 4);
      const z = -3.6 - 7.4 * t, y = 5.9 + 4.4 * Math.pow(t, 1.3) + 0.7 * edge * (1 - t) - 0.35 * Math.sin(t * PI) * 0;
      row.push([x, y + 0.4 * (1 - t) * (1 - t) * 0 , z]);
    }
    rows.push(row);
  }
  m.sheet('roof_tiles', rows, { uv: 2.2, twoSided: true });
  m.box('lacquer_black', [0, 10.4, -11.0], [28.6, 0.5, 0.6], { uv: 1 }); // the ridge
  for (const sx of [-1, 1]) m.box('lacquer_black', [sx * 14.1, 7.9, -7.3], [0.3, 0.35, 8], { uv: 1 });
  // courtyard walls with small roofs at the sides
  for (const sx of [-1, 1]) {
    m.box('plaster_white', [sx * 16.5, 1.4, 0], [0.5, 2.8, 14], { uv: 2 });
    m.box('roof_tiles', [sx * 16.5, 2.95, 0], [1.4, 0.25, 14.4], { uv: 2.2 });
  }
  // trees: a cherry in bloom on the left, pines on the right and far behind
  tree(m, -11.5, -3.2, 4.6, 1.8, 'leaves_pink', 1);
  tree(m, -14.5, -1.0, 3.8, 1.5, 'leaves_pink', 2);
  tree(m, 12, -3.8, 5.2, 1.6, 'leaves_green', 3, true);
  tree(m, 14.8, -1.2, 4.2, 1.3, 'leaves_dark', 4, true);
  for (let i = 0; i < 9; i++) tree(m, -24 + i * 6 + hf(i, 41) * 3, -16 - hf(i, 42) * 6, 6 + hf(i, 43) * 4, 2.2, i % 2 ? 'leaves_dark' : 'leaves_green', 10 + i, true);
  stoneLantern(st, m, -7.6, -4.4, 0); stoneLantern(st, m, 7.6, -4.4, 1);
  // a weapon rack and a drum on the veranda
  m.box('wood_dark', [-4.2, 1.3, -7.2], [2.2, 0.08, 0.2], { uv: 1 }); m.box('wood_dark', [-4.2, 1.9, -7.2], [2.2, 0.08, 0.2], { uv: 1 });
  for (let i = 0; i < 6; i++) m.cyl('wood', [-5.1 + i * 0.36, 1.6, -7.15], 0.022, 2.0, { n: 6, uv: 1 });
  m.cyl('lacquer_red', [5.0, 1.15, -7.0], 0.55, 0.75, { axis: 'z', n: 20, uv: 1 }); m.cyl('plaster_white', [5.0, 1.15, -6.6], 0.5, 0.02, { axis: 'z', n: 20, uv: 1 });
  // hanging paper lanterns under the eaves (they sway)
  for (let i = 0; i < 6; i++) { const x = -8.25 + i * 3.3; st.part('Lamp' + i, [x, 5.35, -5.3], (p) => { p.box('steel_dark', [0, -0.2, 0], [0.012, 0.4, 0.012], { uv: 1 }); p.sphere('paper_lamp', [0, -0.62, 0], 0.2, 0.28, 0.2, { n: 12, rings: 8, bump: (a, b, c) => 1 + 0.04 * Math.cos(Math.atan2(c, a) * 12) }); }); }
  // mountains: layers of rock ridges fading into the haze
  for (let i = 0; i < 7; i++) { const x = -160 + i * 55 + hf(i, 51) * 20, h = 60 + hf(i, 52) * 70; m.sphere('mountain', [x, -10, -170 - hf(i, 53) * 40], 45 + hf(i, 54) * 25, h, 40, { n: 18, rings: 10, uv: 12, bump: (a, b, c) => 1 + 0.12 * Math.sin(a * 5 + i) * Math.cos(c * 4) + 0.06 * Math.sin(b * 13 + i) }); }
  for (let i = 0; i < 6; i++) { const x = -260 + i * 100 + hf(i, 61) * 30, h = 120 + hf(i, 62) * 80; m.sphere('mountain_far', [x, -20, -380 - hf(i, 63) * 60], 90 + hf(i, 64) * 40, h, 60, { n: 18, rings: 10, uv: 20, bump: (a, b, c) => 1 + 0.1 * Math.sin(a * 4 + i * 2) * Math.cos(c * 3) }); }
  // the courtyard ends in a low wall with a gate looking out over the valley
  m.box('plaster_white', [0, 0.6, -14.5], [40, 1.2, 0.5], { uv: 2 }); m.box('roof_tiles', [0, 1.3, -14.5], [40.6, 0.2, 1.0], { uv: 2.2 });
  st.write('stage3.glb');
}

const want = process.argv.slice(2);
if (!want.length || want.includes('1')) stage1();
if (!want.length || want.includes('2')) stage2();
if (!want.length || want.includes('3')) stage3();
