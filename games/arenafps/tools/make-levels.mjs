// The three arenas as data: rooms, corridors, open halls, doors, two heights (roofs reached by stairs and jump pads),
// spawn rooms, items and dressing. Writes, per arena:
//   levels/<name>.cwscene   CSG shapes (cartwheel/csg.h): one root per surface kind, rooms and doors cut out of solid
//                           blocks, roof slabs with stair holes, parapets, decks, landings and ledges
//   levels/<name>.lvl       what the game places itself: spawns, items, stairs, jump pads, lamps, door frames, props
// then bakes the scene with the engine's cwcsg tool (levels/<name>_<root>.cwcsg) and prints a flow report per arena
// (FUN-RUBRIC FP11: loops, dead ends, how far each spawn is from the power items).
//
//   node tools/make-levels.mjs [--cwcsg <path to cwcsg>] [--check]
//
// Coordinates: metres, x east, z south, y up; a rect is [x0, z0, x1, z1]. Rooms are roofed (an interior STOREY high
// under a ROOF slab whose top is the upper level, TOP); open zones have no roof. Walls are what is left of a solid block
// between the rooms cut out of it.
import { writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const HERE = dirname(fileURLToPath(import.meta.url));
const GAME = join(HERE, '..');
const OUT = join(GAME, 'levels');
const STOREY = 3.6, ROOF = 0.5, TOP = STOREY + ROOF;
const DOOR_H = 2.7, DOOR_W = 2.4, WALL = 0.6;

const f = (v) => (Math.round(v * 1000) / 1000).toString();
const inside = (r, x, z, e = 0) => x > r[0] - e && x < r[2] + e && z > r[1] - e && z < r[3] + e;
// a rect mirrored into the four quadrants (sx, sz = +-1)
const mir = (r, sx, sz) => {
  const a = [r[0] * sx, r[1] * sz, r[2] * sx, r[3] * sz];
  return [Math.min(a[0], a[2]), Math.min(a[1], a[3]), Math.max(a[0], a[2]), Math.max(a[1], a[3])];
};
const QUADS = [[-1, -1], [1, -1], [-1, 1], [1, 1]];
const QNAME = ['nw', 'ne', 'sw', 'se'];

// ── the arenas ──────────────────────────────────────────────────────────────────────────────────────────────────────

function foundry() {
  const L = base('foundry', 28);
  L.roofKind = 'accent';
  L.mass.push([-28, -28, 28, 28]);
  // the open core hall and two open courts west and east of it (one space, split by gate pillars)
  open(L, 'core', [-12, -12, 12, 12]);
  open(L, 'court_w', [-23, -12, -12, 12]);
  open(L, 'court_e', [12, -12, 23, 12]);
  // a roofed corridor all round the outer walls
  room(L, 'ring', [-28, -28, 28, -23.6]);
  room(L, 'ring', [-28, 23.6, 28, 28]);
  room(L, 'ring', [-28, -23.6, -23.6, 23.6]);
  room(L, 'ring', [23.6, -23.6, 28, 23.6]);
  // spawn rooms on the diagonals, side halls north and south
  QUADS.forEach(([sx, sz], i) => room(L, 'spawn_' + QNAME[i], mir([12.6, 12.6, 23, 23], sx, sz), { spawn: true }));
  room(L, 'hall_n', [-12, -23, 12, -12.6], { pillars: [[-4, -17.8], [4, -17.8]] });
  room(L, 'hall_s', [-12, 12.6, 12, 23], { pillars: [[-4, 17.8], [4, 17.8]] });
  for (const [sx, sz] of QUADS) {
    door(L, 23.3 * sx, 17.8 * sz, 'x');      // spawn - ring
    door(L, 17.8 * sx, 23.3 * sz, 'z');      // spawn - ring
    door(L, 12.3 * sx, 17.8 * sz, 'x');      // spawn - side hall
    door(L, 17.8 * sx, 12.3 * sz, 'z', 3.2); // spawn - court
  }
  for (const s of [-1, 1]) {
    door(L, -6, 23.3 * s, 'z'); door(L, 6, 23.3 * s, 'z');            // side hall - ring
    door(L, 23.3 * s, -6, 'x'); door(L, 23.3 * s, 6, 'x');            // court - ring
    door(L, -9.5, 12.3 * s, 'z', 3); door(L, 9.5, 12.3 * s, 'z', 3); // side hall - core
  }
  // the raised deck in the core, short stairs up to it from east and west
  extra(L, 'accent', [-4, -4, 4, 4], 0, 2.0, 'deck');
  L.ramps.push({ x: -9, y0: 0, z: 0, len: 5, rise: 2.0, w: 3, dir: 0, a: 'core', b: 'deck' });
  L.ramps.push({ x: 9, y0: 0, z: 0, len: 5, rise: 2.0, w: 3, dir: 1, a: 'core', b: 'deck' });
  // stairs from the core up to the roofs over the side halls: along the wall, to a landing at the roof edge
  extra(L, 'wall', [4, -12, 7.5, -9], 0, TOP, 'landing');
  L.ramps.push({ x: -7, y0: 0, z: -10.5, len: 11, rise: TOP, w: 3, dir: 0, a: 'core', b: 'roof' });
  extra(L, 'wall', [-7.5, 9, -4, 12], 0, TOP, 'landing');
  L.ramps.push({ x: 7, y0: 0, z: 10.5, len: 11, rise: TOP, w: 3, dir: 1, a: 'core', b: 'roof' });
  L.gaps.push([5.75, -12, 3.5], [-5.75, 12, 3.5]);
  // pillars that hold the roof trusses and break the core's long sight lines; gate pillars between the core and the courts
  for (const [sx, sz] of QUADS) extra(L, 'pillar', mir([6, 6, 8, 8], sx, sz), 0, 9, 'pillar');
  for (const s of [-1, 1]) for (const z of [-7, 0, 7]) extra(L, 'pillar', [12 * s - 0.8, z - 0.8, 12 * s + 0.8, z + 0.8], 0, 9, 'pillar');
  // jump pads in the courts up to the ring roof; parapet gaps where they land and where one can drop back down
  for (const s of [-1, 1]) {
    L.pads.push({ x: 19.5 * s, z: 0, y: 0, tx: 25.8 * s, ty: TOP, tz: 0, a: s < 0 ? 'court_w' : 'court_e', b: 'roof' });
    L.gaps.push([23 * s, 0, 3.6]);
  }
  L.gaps.push([0, -12, 3], [0, 12, 3], [-17.8, -12, 2.6], [17.8, 12, 2.6]);
  // items: rockets on the deck and in the ring, rails on the roofs, armour in the south hall and on the north roof
  item(L, 'rocket', 0, 2.0, 0);
  item(L, 'rocket', 0, 0, -25.8);
  item(L, 'rail', -16, TOP, -25.8);
  item(L, 'rail', 16, TOP, 25.8);
  item(L, 'armor', 0, 0, 17.8);
  item(L, 'armor', 0, TOP, -17.8);
  item(L, 'scatter', -17.5, 0, -6);
  item(L, 'scatter', 17.5, 0, 6);
  item(L, 'scatter', 0, 0, 25.8);
  for (const [sx, sz] of QUADS) item(L, 'health', 25.8 * sx, 0, 25.8 * sz);
  item(L, 'health', -9, 0, -17.8); item(L, 'health', 9, 0, 17.8);
  item(L, 'ammo', -25.8, 0, 0); item(L, 'ammo', 25.8, 0, 0);
  item(L, 'ammo', 9, 0, -6); item(L, 'ammo', -9, 0, 6);
  for (const [sx, sz] of QUADS) { spawn(L, 20.5 * sx, 15 * sz); spawn(L, 15 * sx, 20.5 * sz); }
  // dressing: crates and barrels for cover in the courts and the ring, consoles on the deck, roof plant
  for (const [sx, sz] of QUADS) {
    L.decor.push(`crates ${f(16 * sx)} ${f(9.5 * sz)} ${f(0.2 * sx)} 0`);
    L.decor.push(`barrels ${f(21.4 * sx)} ${f(-9 * sz)} 0`);
    L.decor.push(`barrels ${f(26.4 * sx)} ${f(14 * sz)} 0`);
    L.decor.push(`prop jersey ${f(9 * sx)} 0 ${f(4.8 * sz)} 0 1 2.0 0.8 0.6`);
    L.decor.push(`block metal ${f(21 * sx)} ${f(TOP)} ${f(16 * sz)} 2.4 1.6 2.4`);
    L.decor.push(`block metal ${f(4 * sx)} ${f(TOP)} ${f(25.8 * sz)} 2.4 1.6 2.4`);
  }
  L.decor.push(`prop console 0 2.0 -3.0 0 1 0.9 1.1 0.5`, `prop console 0 2.0 3.0 3.1416 1 0.9 1.1 0.5`);
  return L;
}

function courtyard() {
  const L = base('courtyard', 30);
  L.roofKind = 'accent';
  // four houses, each four rooms on a 2 x 2 grid: the back room (spawn) in the corner, two side rooms, the front room at
  // the plaza. Streets between the houses meet in the plaza; an open street runs round them all.
  for (let q = 0; q < 4; q++) {
    const [sx, sz] = QUADS[q], n = QNAME[q];
    L.mass.push(mir([6, 6, 24, 24], sx, sz));
    room(L, 'back_' + n, mir([15, 15, 23.4, 23.4], sx, sz), { spawn: true });
    room(L, 'side_x_' + n, mir([15, 6.6, 23.4, 14.4], sx, sz));
    room(L, 'side_z_' + n, mir([6.6, 15, 14.4, 23.4], sx, sz));
    room(L, 'front_' + n, mir([6.6, 6.6, 14.4, 14.4], sx, sz));
    door(L, 19.2 * sx, 14.7 * sz, 'z');           // back - side x
    door(L, 14.7 * sx, 19.2 * sz, 'x');           // back - side z
    door(L, 14.7 * sx, 10.5 * sz, 'x');           // side x - front
    door(L, 10.5 * sx, 14.7 * sz, 'z');           // side z - front
    door(L, 23.7 * sx, 19.2 * sz, 'x');           // back - outer street (side)
    door(L, 16.3 * sx, 23.7 * sz, 'z');           // back - outer street (end)
    door(L, 23.7 * sx, 10.5 * sz, 'x');           // side x - outer street
    door(L, 19.2 * sx, 6.3 * sz, 'z');            // side x - cross street
    door(L, 10.5 * sx, 23.7 * sz, 'z');           // side z - outer street
    door(L, 6.3 * sx, 19.2 * sz, 'x');            // side z - cross street
    door(L, 6.3 * sx, 10.5 * sz, 'x', 3);         // front - cross street
    door(L, 10.5 * sx, 6.3 * sz, 'z', 3);         // front - cross street
    // an outside stair along the outer street's far edge up to a landing that bridges over to the house roof
    L.ramps.push({ x: 7 * sx, y0: 0, z: 28.8 * sz, len: 11, rise: TOP, w: 2.4, dir: sx < 0 ? 1 : 0, a: 'outer', b: 'roof' });
    extra(L, 'accent', mir([18, 24, 21, 30], sx, sz), TOP - 0.4, 0.4, 'landing');
    L.bridges.push(mir([18, 24, 21, 30], sx, sz));
  }
  // bridges at roof height across the four cross streets: the roofs make an upper ring
  for (const r of [[-6, -16.2, 6, -13.8], [-6, 13.8, 6, 16.2], [-16.2, -6, -13.8, 6], [13.8, -6, 16.2, 6]]) {
    extra(L, 'accent', r, TOP - 0.4, 0.4, 'bridge');
    L.bridges.push(r);
  }
  L.rails = L.bridges.slice(4);
  // open zones: the plaza, the four cross streets, the street round the outside
  open(L, 'plaza', [-6, -6, 6, 6]);
  open(L, 'street_n', [-6, -24, 6, -6]);
  open(L, 'street_s', [-6, 6, 6, 24]);
  open(L, 'street_w', [-24, -6, -6, 6]);
  open(L, 'street_e', [6, -6, 24, 6]);
  open(L, 'outer', [-30, -30, 30, -24]);
  open(L, 'outer', [-30, 24, 30, 30]);
  open(L, 'outer', [-30, -24, -24, 24]);
  open(L, 'outer', [24, -24, 30, 24]);
  // the plaza: a low plinth with the rocket on it, four columns; baffles at the street mouths break the long lanes
  extra(L, 'accent', [-3, -3, 3, 3], 0, 0.8, 'plinth');
  for (const [sx, sz] of QUADS) extra(L, 'pillar', mir([4.4, 4.4, 5.4, 5.4], sx, sz), 0, 4.2, 'column');
  extra(L, 'wall', [-6, -21.2, -1.5, -20.6], 0, 2.6, 'baffle');
  extra(L, 'wall', [1.5, 20.6, 6, 21.2], 0, 2.6, 'baffle');
  extra(L, 'wall', [20.6, -6, 21.2, -1.5], 0, 2.6, 'baffle');
  extra(L, 'wall', [-21.2, 1.5, -20.6, 6], 0, 2.6, 'baffle');
  // jump pads in the plaza up onto the corners of the north houses
  for (const s of [-1, 1]) {
    L.pads.push({ x: 4.6 * s, z: 1.5, y: 0, tx: 9.2 * s, ty: TOP, tz: -9.2, a: 'plaza', b: 'roof' });
    L.gaps.push([7.6 * s, -6, 3.2], [6 * s, -7.6, 3.2]);
  }
  item(L, 'rocket', 0, 0.8, 0);
  item(L, 'rail', 0, TOP, -15);
  item(L, 'rail', 0, TOP, 15);
  item(L, 'armor', -15, TOP, 0);
  item(L, 'armor', 15, TOP, 0);
  for (let q = 0; q < 4; q++) {
    const [sx, sz] = QUADS[q];
    item(L, q === 1 || q === 2 ? 'scatter' : 'health', 10.5 * sx, 0, 10.5 * sz);
    item(L, 'health', 19.2 * sx, 0, 10.5 * sz);
    item(L, 'ammo', 27 * sx, 0, 20 * sz);
    spawn(L, 21 * sx, 18 * sz); spawn(L, 18 * sx, 21 * sz);
  }
  item(L, 'scatter', 0, 0, -18); item(L, 'scatter', 0, 0, 18);
  for (const [sx, sz] of QUADS) {
    L.decor.push(`crates ${f(27.4 * sx)} ${f(-2 * sz)} 1.6 0`);
    L.decor.push(`barrels ${f(26.6 * sx)} ${f(-12 * sz)} 0`);
    L.decor.push(`prop planter ${f(28.6 * sx)} 0 ${f(6 * sz)} 1.5708 1 1.0 1.6 2.2`);
    L.decor.push(`prop jersey ${f(3.5 * sx)} 0 ${f(12 * sz)} 0 1 2.0 0.8 0.6`);
    L.decor.push(`block metal ${f(19 * sx)} ${f(TOP)} ${f(19 * sz)} 2.4 1.6 2.4`);
  }
  return L;
}

function terraces() {
  const L = base('terraces', 26);
  L.roofKind = 'floor';
  L.mass.push([-26, -26, 26, 26]);
  // the pit: open, a wet floor round a raised island, ledges 1.2 m up along its walls (a jump gets you onto them)
  open(L, 'pit', [-12, -12, 12, 12]);
  // under the terraces: a gallery round the outer walls, corner spawn rooms with a stair up through the roof, tunnels to the pit
  room(L, 'gallery', [-26, -26, 26, -21.6]);
  room(L, 'gallery', [-26, 21.6, 26, 26]);
  room(L, 'gallery', [-26, -21.6, -21.6, 21.6]);
  room(L, 'gallery', [21.6, -21.6, 26, 21.6]);
  QUADS.forEach(([sx, sz], i) => {
    room(L, 'spawn_' + QNAME[i], mir([12.6, 12.6, 21, 21], sx, sz), { spawn: true });
    L.ramps.push({ x: 20 * sx, y0: 0, z: 13.4 * sz, len: 7.2, rise: TOP, w: 1.8, dir: sz < 0 ? 3 : 2, a: 'spawn_' + QNAME[i], b: 'roof' });
    L.holes.push(mir([19, 16.2, 21, 21], sx, sz));
    door(L, 21.3 * sx, 16.8 * sz, 'x'); // spawn - gallery
    door(L, 15 * sx, 21.3 * sz, 'z');   // spawn - gallery
  });
  room(L, 'tunnel_n', [-3, -21, 3, -12.6]);
  room(L, 'tunnel_s', [-3, 12.6, 3, 21]);
  room(L, 'tunnel_w', [-21, -3, -12.6, 3]);
  room(L, 'tunnel_e', [12.6, -3, 21, 3]);
  for (const s of [-1, 1]) {
    door(L, 0, 21.3 * s, 'z', 3); door(L, 21.3 * s, 0, 'x', 3);
    door(L, 0, 12.3 * s, 'z', 3.4); door(L, 12.3 * s, 0, 'x', 3.4);
  }
  // ledges 1.2 m up along the pit walls, cut at the tunnel mouths; a landing in each corner at the terrace's height
  const LEDGE = 1.2;
  for (const s of [-1, 1])
    for (const [a, b] of [[-12, -2], [2, 12]]) {
      extra(L, 'accent', [a, s < 0 ? -12 : 9.6, b, s < 0 ? -9.6 : 12], 0, LEDGE, 'ledge');
      extra(L, 'accent', [s < 0 ? -12 : 9.6, a, s < 0 ? -9.6 : 12, b], 0, LEDGE, 'ledge');
      // navigation links: jumps up the ledges (and back down), from the pit floor 1 m out from the face to 1 m in on the
      // top. The east and west ledges are flat: two jumps each, clear of the corner blocks and landings. The north and
      // south ledges carry the stairs to the terrace: one jump onto the foot of the stairs, beside the tunnel mouth
      const face = s * 9.6;
      for (const t of a < 0 ? [-7, -3.5] : [3.5, 7]) L.links.push({ s: [face - s * 1.0, 0, t], e: [face + s * 1.0, LEDGE, t], kind: 'jump', two: 1 });
      const foot = a < 0 ? -2.6 : 2.6;
      L.links.push({ s: [foot, 0, face - s * 1.0], e: [foot, LEDGE, face + s * 1.0], kind: 'jump', two: 1 });
    }
  for (const [sx, sz] of QUADS) {
    extra(L, 'wall', mir([8.6, 9.6, 12, 12], sx, sz), 0, TOP, 'landing');
    L.ramps.push({ x: 2 * sx, y0: LEDGE, z: 10.8 * sz, len: 6.6, rise: TOP - LEDGE, w: 2.4, dir: sx < 0 ? 1 : 0, a: 'ledge', b: 'roof' });
    L.gaps.push([10.3 * sx, 12 * sz, 3.4]);
  }
  // the island, low bridges over the water out to the ledges' gaps, short stairs from the bridges onto the island
  extra(L, 'pillar', [-3.5, -3.5, 3.5, 3.5], 0, 1.2, 'island');
  for (const r of [[-1.6, -12, 1.6, -3.5], [-1.6, 3.5, 1.6, 12], [-12, -1.6, -3.5, 1.6], [3.5, -1.6, 12, 1.6]]) extra(L, 'accent', r, 0, 0.4, 'bridge');
  L.ramps.push({ x: 0, y0: 0.4, z: -6.5, len: 3, rise: 0.8, w: 3.2, dir: 2, a: 'pit', b: 'pit' });
  L.ramps.push({ x: 0, y0: 0.4, z: 6.5, len: 3, rise: 0.8, w: 3.2, dir: 3, a: 'pit', b: 'pit' });
  L.ramps.push({ x: -6.5, y0: 0.4, z: 0, len: 3, rise: 0.8, w: 3.2, dir: 0, a: 'pit', b: 'pit' });
  L.ramps.push({ x: 6.5, y0: 0.4, z: 0, len: 3, rise: 0.8, w: 3.2, dir: 1, a: 'pit', b: 'pit' });
  for (const [sx, sz] of QUADS) extra(L, 'wall', mir([5.5, 5.5, 8, 8], sx, sz), 0, 2.2, 'block');
  // the terrace top: cover along the long sides, kiosks in the corners
  for (const [sx, sz] of QUADS) {
    extra(L, 'wall', mir([5, 17, 9, 17.6], sx, sz), TOP, 1.2, 'cover');
    extra(L, 'wall', mir([17, 5, 17.6, 9], sx, sz), TOP, 1.2, 'cover');
    extra(L, 'clad', mir([22.4, 22.4, 25.6, 25.6], sx, sz), TOP, 2.6, 'kiosk');
  }
  // jump pads in the pit, up onto the terrace east and west
  for (const [sx, sz] of QUADS) {
    L.pads.push({ x: 7.2 * sx, z: 4.2 * sz, y: 0, tx: 14.2 * sx, ty: TOP, tz: 4.2 * sz, a: 'pit', b: 'roof' });
    L.gaps.push([12 * sx, 4.2 * sz, 3]);
  }
  item(L, 'rocket', 0, 1.2, 0);
  item(L, 'rail', 0, TOP, -19);
  item(L, 'rail', 0, TOP, 19);
  item(L, 'armor', -19, TOP, 0);
  item(L, 'armor', 19, TOP, 0);
  item(L, 'scatter', -16.8, 0, 0); item(L, 'scatter', 16.8, 0, 0);
  item(L, 'scatter', 0, 0, -16.8); item(L, 'scatter', 0, 0, 16.8);
  for (const [sx, sz] of QUADS) {
    item(L, 'health', 6 * sx, LEDGE, 10.8 * sz);
    item(L, 'ammo', 23.8 * sx, 0, 8 * sz);
    spawn(L, 15 * sx, 17.5 * sz); spawn(L, 16.5 * sx, 14 * sz);
  }
  L.water = { x: 0, y: 0.3, z: 0, sx: 24, sz: 24 };
  for (const [sx, sz] of QUADS) {
    L.decor.push(`crates ${f(23.8 * sx)} ${f(14 * sz)} 0.3 0`);
    L.decor.push(`barrels ${f(12 * sx)} ${f(23.4 * sz)} 0`);
    L.decor.push(`prop planter ${f(14 * sx)} ${f(TOP)} ${f(24.6 * sz)} 0 1 2.2 1.6 1.0`);
  }
  return L;
}


// ── building blocks ──────────────────────────────────────────────────────────────────────────────────────────────────

function base(name, half) {
  return { name, half, mass: [], rooms: [], open: [], doors: [], holes: [], extras: [], ramps: [], pads: [], gaps: [], links: [], spawns: [], items: [], decor: [], bridges: [], roofKind: 'accent' };
}
function room(L, name, r, o = {}) { L.rooms.push({ name, r, ...o }); }
function open(L, name, r) { L.open.push({ name, r }); }
// a doorway through the wall at (x, z); axis 'x': the wall runs north-south (its normal is x), 'z': east-west
function door(L, x, z, axis, w = DOOR_W, h = DOOR_H) { L.doors.push({ x, z, axis, w, h }); }
function extra(L, kind, r, y0, h, tag) { L.extras.push({ kind, r, y0, h, tag }); }
function item(L, kind, x, y, z) { L.items.push({ kind, x, y, z }); }
function spawn(L, x, z, y = 0) { L.spawns.push({ x, y, z }); }

// navigation links for the bots: a drop through every parapet gap that opens onto an open zone, from 1.2 m back on the
// roof to 1.8 m out over the floor below (the game snaps each end to the floor under it)
function dropLinks(L) {
  const zones = [...L.rooms.map((r) => ({ name: r.name, r: r.r })), ...L.open.map((o) => ({ name: o.name, r: o.r }))];
  const at = (x, z) => { const hit = zones.find((zz) => inside(zz.r, x, z)); return hit ? hit.name : null; };
  for (const [gx, gz] of L.gaps)
    for (const [dx, dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const z = at(gx + dx, gz + dz);
      if (!z || !L.open.some((o) => o.name === z) || isMass(L, gx + dx, gz + dz)) continue;
      L.links.push({ s: [gx - dx * 1.2, TOP, gz - dz * 1.2], e: [gx + dx * 1.8, 0, gz + dz * 1.8], kind: 'drop', two: 0 });
      break;
    }
}

// ── the CSG scene ────────────────────────────────────────────────────────────────────────────────────────────────────

function box(name, parent, r, y0, y1, op) {
  const cx = (r[0] + r[2]) / 2, cz = (r[1] + r[3]) / 2;
  return [`[node name="${name}" parent="${parent}" type="CSGBox3D"]`, ...(op ? [`operation = ${op}`] : []),
    `position = vec3(${f(cx)}, ${f((y0 + y1) / 2)}, ${f(cz)})`, `size = vec3(${f(r[2] - r[0])}, ${f(y1 - y0)}, ${f(r[3] - r[1])})`, ''];
}

function isOpen(L, x, z) { return L.open.some((o) => inside(o.r, x, z)); }
function isMass(L, x, z) { return L.mass.some((m) => inside(m, x, z)) && !isOpen(L, x, z); }
function onBridge(L, x, z) { return (L.bridges || []).some((b) => inside(b, x, z, 0.05)); }

// parapets: 1 m walls on the roof's edge wherever it drops into open space, broken at the gaps (stair tops, pad landings)
function parapets(L) {
  const out = [];
  const gap = (x, z) => L.gaps.some(([gx, gz, w]) => Math.abs(gx - x) < w / 2 + 0.01 && Math.abs(gz - z) < w / 2 + 0.01) || onBridge(L, x, z);
  const STEP = 0.1;
  for (const m of L.mass) {
    // walk the outline of every roofed area by sampling each mass rect's grid edges against the open zones
    const xs = [], zs = [];
    for (let x = m[0]; x <= m[2] + 1e-6; x += STEP) xs.push(x);
    for (let z = m[1]; z <= m[3] + 1e-6; z += STEP) zs.push(z);
    // horizontal edges (running along x): for each candidate line z where roof meets open, collect runs
    const lines = new Set([m[1], m[3]]);
    for (const o of L.open) { lines.add(o.r[1]); lines.add(o.r[3]); }
    for (const z of lines) {
      if (z < m[1] - 1e-6 || z > m[3] + 1e-6) continue;
      for (const side of [-1, 1]) { // roof on side `side` of the line, open on the other
        let run = null;
        const flush = (xe) => { if (run && xe - run > 0.5) out.push({ r: [run, side < 0 ? z - 0.4 : z, xe, side < 0 ? z : z + 0.4] }); run = null; };
        for (const x of xs) {
          const roof = isMass(L, x, z + side * 0.2) && inside(m, x, z + side * 0.2);
          const drop = !isMass(L, x, z - side * 0.2) && Math.abs(z) < L.half - 0.05;
          if (roof && drop && !gap(x, z)) { if (run === null) run = x; } else flush(x);
        }
        flush(xs[xs.length - 1]);
      }
    }
    const cols = new Set([m[0], m[2]]);
    for (const o of L.open) { cols.add(o.r[0]); cols.add(o.r[2]); }
    for (const x of cols) {
      if (x < m[0] - 1e-6 || x > m[2] + 1e-6) continue;
      for (const side of [-1, 1]) {
        let run = null;
        const flush = (ze) => { if (run !== null && ze - run > 0.5) out.push({ r: [side < 0 ? x - 0.4 : x, run, side < 0 ? x : x + 0.4, ze] }); run = null; };
        for (const z of zs) {
          const roof = isMass(L, x + side * 0.2, z) && inside(m, x + side * 0.2, z);
          const drop = !isMass(L, x - side * 0.2, z) && Math.abs(x) < L.half - 0.05;
          if (roof && drop && !gap(x, z)) { if (run === null) run = z; } else flush(z);
        }
        flush(zs[zs.length - 1]);
      }
    }
  }
  // de-duplicate (two mass rects can share an edge)
  const seen = new Set();
  return out.filter((p) => { const k = p.r.map(f).join(','); if (seen.has(k)) return false; seen.add(k); return true; });
}

function scene(L) {
  const lines = ['[scene format=1]', '', '[node name="Level" type="Node3D"]', ''];
  const root = (name) => lines.push(`[node name="${name}" parent="." type="CSGCombiner3D"]`, `bake_file = "${L.name}_${name.toLowerCase()}.cwcsg"`, '');
  // walls: the solid blocks up to the roof, minus every room, open zone and doorway
  root('Walls');
  L.mass.forEach((m, i) => lines.push(...box('Mass' + i, 'Walls', m, 0, STOREY)));
  L.rooms.forEach((r, i) => lines.push(...box('Room' + i, 'Walls', r.r, -0.2, STOREY + 0.05, 'SUBTRACTION')));
  L.open.forEach((o, i) => lines.push(...box('Open' + i, 'Walls', o.r, -0.2, STOREY + 0.05, 'SUBTRACTION')));
  L.doors.forEach((d, i) => {
    const t = 1.4, r = d.axis === 'x' ? [d.x - t / 2, d.z - d.w / 2, d.x + t / 2, d.z + d.w / 2] : [d.x - d.w / 2, d.z - t / 2, d.x + d.w / 2, d.z + t / 2];
    lines.push(...box('Door' + i, 'Walls', r, -0.2, d.h, 'SUBTRACTION'));
  });
  // the roof slab over every block, minus the open zones and the stair holes; parapets on its open edges
  root('Roof');
  L.mass.forEach((m, i) => lines.push(...box('Slab' + i, 'Roof', m, STOREY, TOP)));
  L.open.forEach((o, i) => lines.push(...box('Open' + i, 'Roof', o.r, STOREY - 0.1, TOP + 0.1, 'SUBTRACTION')));
  L.holes.forEach((h, i) => lines.push(...box('Hole' + i, 'Roof', h, STOREY - 0.1, TOP + 0.1, 'SUBTRACTION')));
  root('Parapet');
  L.parapets.forEach((p, i) => lines.push(...box('P' + i, 'Parapet', p.r, TOP, TOP + 1.0)));
  // the rest by surface kind
  const kinds = [...new Set(L.extras.map((e) => e.kind))];
  for (const k of kinds) {
    const name = 'X_' + k;
    root(name);
    L.extras.filter((e) => e.kind === k).forEach((e, i) => lines.push(...box(e.tag + i, name, e.r, e.y0, e.y0 + e.h)));
  }
  L.roots = [['walls', 'wall'], ['roof', L.roofKind], ['parapet', 'wall'], ...kinds.map((k) => ['x_' + k, k])];
  return lines.join('\n');
}

// ── what the game places: spawns, items, stairs, pads, lamps, door frames, props ─────────────────────────────────────

function lvl(L) {
  const o = [`# ${L.name}: written by tools/make-levels.mjs, do not edit`, `half ${f(L.half)}`, `top ${f(TOP)}`, `storey ${f(STOREY)}`];
  for (const [file, kind] of L.roots) o.push(`csg ${L.name}_${file}.cwcsg ${kind}`);
  for (const s of L.spawns) o.push(`spawn ${f(s.x)} ${f(s.y + 0.05)} ${f(s.z)}`);
  for (const i of L.items) o.push(`item ${i.kind} ${f(i.x)} ${f(i.y)} ${f(i.z)}`);
  for (const r of L.ramps) o.push(`ramp ${f(r.x)} ${f(r.y0)} ${f(r.z)} ${f(r.len)} ${f(r.rise)} ${f(r.w)} ${r.dir}`);
  for (const p of L.pads) o.push(`pad ${f(p.x)} ${f(p.y)} ${f(p.z)} ${f(p.tx)} ${f(p.ty)} ${f(p.tz)}`);
  for (const k of L.links) o.push(`link ${k.s.map(f).join(' ')} ${k.e.map(f).join(' ')} ${k.kind} ${k.two}`);
  // a ceiling lamp per room (two in long ones), door frames on every doorway
  for (const r of L.rooms) {
    const w = r.r[2] - r.r[0], d = r.r[3] - r.r[1];
    const n = Math.max(1, Math.round(Math.max(w, d) / 11));
    for (let k = 0; k < n; k++) {
      const t = (k + 0.5) / n;
      const x = w >= d ? r.r[0] + w * t : (r.r[0] + r.r[2]) / 2, z = w >= d ? (r.r[1] + r.r[3]) / 2 : r.r[1] + d * t;
      o.push(`lamp ${f(x)} ${f(STOREY)} ${f(z)} ${r.spawn ? 1 : 0}`);
    }
    for (const [x, z] of r.pillars || []) o.push(`column ${f(x)} 0 ${f(z)} ${f(STOREY)}`);
  }
  for (const d of L.doors) o.push(`doorframe ${f(d.x)} ${f(d.z)} ${f(d.w)} ${f(d.h)} ${d.axis}`);
  if (L.water) o.push(`water ${f(L.water.x)} ${f(L.water.y)} ${f(L.water.z)} ${f(L.water.sx)} ${f(L.water.sz)}`);
  for (const b of L.rails || []) o.push(`rail ${f(b[0])} ${f(b[1])} ${f(b[2])} ${f(b[3])} ${f(TOP)}`);
  o.push(...L.decor);
  return o.join('\n') + '\n';
}

// ── the flow report (FP11): a graph of zones joined by doors, open borders, stairs and pads ───────────────────────────

function flow(L) {
  const zones = [...L.rooms.map((r) => ({ name: r.name, r: r.r, spawn: !!r.spawn })), ...L.open.map((o) => ({ name: o.name, r: o.r }))];
  const names = [...new Set(zones.map((z) => z.name)), 'roof'];
  const at = (x, z) => { const hit = zones.find((zz) => inside(zz.r, x, z)); return hit ? hit.name : null; };
  const edges = [];
  for (const d of L.doors) {
    const e = 0.9, a = d.axis === 'x' ? at(d.x - e, d.z) : at(d.x, d.z - e), b = d.axis === 'x' ? at(d.x + e, d.z) : at(d.x, d.z + e);
    if (a && b && a !== b) edges.push([a, b, 'door']);
    else console.warn(`${L.name}: door at ${d.x},${d.z} joins ${a} and ${b}`);
  }
  // open zones that share a border
  for (let i = 0; i < L.open.length; i++)
    for (let j = i + 1; j < L.open.length; j++) {
      const a = L.open[i], b = L.open[j];
      if (a.name === b.name) continue;
      const ox = Math.min(a.r[2], b.r[2]) - Math.max(a.r[0], b.r[0]), oz = Math.min(a.r[3], b.r[3]) - Math.max(a.r[1], b.r[1]);
      if ((ox > 2 && Math.abs(oz) < 0.01) || (oz > 2 && Math.abs(ox) < 0.01)) edges.push([a.name, b.name, 'open']);
    }
  for (const r of L.ramps) edges.push([r.a, r.b, 'stair']);
  // a gap in a parapet: one can drop from the roof into the zone below it
  for (const [gx, gz] of L.gaps)
    for (const [dx, dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const z = at(gx + dx, gz + dz);
      if (z && L.open.some((o) => o.name === z)) { edges.push(['roof', z, 'drop']); break; }
    }
  for (const p of L.pads) edges.push([p.a, p.b, 'pad']);
  // the extras with their own floor (deck, ledge) are part of the zone they stand in
  const alias = (n) => (n === 'deck' ? 'core' : n === 'ledge' ? 'pit' : n);
  const E = edges.map(([a, b, k]) => [alias(a), alias(b), k]).filter(([a, b]) => a !== b);
  const V = [...new Set(E.flatMap(([a, b]) => [a, b]))];
  const deg = Object.fromEntries(V.map((v) => [v, new Set(E.filter((e) => e[0] === v || e[1] === v).map((e) => (e[0] === v ? e[1] : e[0]))).size]));
  // connected components and cycle rank (independent loops)
  const parent = Object.fromEntries(V.map((v) => [v, v]));
  const find = (v) => (parent[v] === v ? v : (parent[v] = find(parent[v])));
  for (const [a, b] of E) parent[find(a)] = find(b);
  const comps = new Set(V.map(find)).size;
  // loops counted over distinct zone pairs (several doors between the same two zones count once)
  const pairs = new Set(E.map(([a, b]) => [a, b].sort().join('|')));
  const loops = pairs.size - V.length + comps;
  // hops from each spawn room to the power items (rocket, rail, armor)
  const adj = Object.fromEntries(V.map((v) => [v, new Set()]));
  for (const [a, b] of E) { adj[a].add(b); adj[b].add(a); }
  const bfs = (s) => { const d = { [s]: 0 }, q = [s]; while (q.length) { const v = q.shift(); for (const w of adj[v]) if (!(w in d)) { d[w] = d[v] + 1; q.push(w); } } return d; };
  const itemZone = (i) => (i.y >= TOP - 0.01 ? 'roof' : alias(at(i.x, i.z) || 'roof'));
  const powers = L.items.filter((i) => ['rocket', 'rail', 'armor'].includes(i.kind)).map((i) => ({ kind: i.kind, zone: itemZone(i) }));
  const spawnRooms = [...new Set(L.rooms.filter((r) => r.spawn).map((r) => r.name))];
  const spawnFit = spawnRooms.map((s) => {
    const d = bfs(s);
    const near = powers.filter((p) => (d[p.zone] ?? 99) <= 3);
    return { spawn: s, power_within_3: new Set(near.map((p) => p.kind + '@' + p.zone)).size };
  });
  const dead = V.filter((v) => deg[v] < 2);
  return { arena: L.name, zones: V.length, links: E.length, loops, components: comps, dead_ends: dead, spawn_power: spawnFit,
    pass: comps === 1 && loops >= 3 && dead.length === 0 && spawnFit.every((s) => s.power_within_3 >= 2) };
}

// ── main ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

const args = process.argv.slice(2);
const ai = args.indexOf('--cwcsg');
const CWCSG = ai >= 0 ? args[ai + 1] : process.env.CWCSG;
const CHECK = args.includes('--check');
mkdirSync(OUT, { recursive: true });
let ok = true;
for (const L of [foundry(), courtyard(), terraces()]) {
  L.parapets = parapets(L);
  dropLinks(L);
  const sc = scene(L);
  writeFileSync(join(OUT, L.name + '.cwscene'), sc + '\n');
  writeFileSync(join(OUT, L.name + '.lvl'), lvl(L));
  const rep = flow(L);
  console.log(JSON.stringify(rep));
  ok &&= rep.pass;
  if (CWCSG) {
    if (!existsSync(CWCSG)) throw new Error('no cwcsg at ' + CWCSG);
    const res = execFileSync(CWCSG, [join(OUT, L.name + '.cwscene'), '--assets', OUT, ...(CHECK ? ['--check'] : []), '-q'], { encoding: 'utf8' });
    if (res.trim()) console.log(res.trim());
  }
}
if (!ok) { console.error('flow: a layout misses FP11 (see the reports above)'); process.exitCode = 1; }
