#!/usr/bin/env node
// crawler's floors in bulk: writes assets/levels/<nnn>.cwmaze for every floor (the format is maze.h's) and
// assets/levels/count.txt. Each floor comes from its own seed: a layout (rooms and corridors, a braided maze, a cave,
// halls of pillars, rings, vaults), then the exit (far from the start), doors on the way with their keys placed
// before them, spawners away from the start, food, potions, treasure, charms, breakable walls, trap walls, teleporters,
// the tileset's torches and decor, and the lights the bake uses. A floor drawn by hand (tools/levels/<nnn>.txt: the
// same format, its decor and lights optional) replaces the generated one; the generator adds decor and lights to it
// when it has none.
//
//   node tools/gen-levels.mjs [--only <n,...>] [--count <n>]
// Every floor it writes passes the same proof the level check runs (tools/maze.mjs solve, adversarial).
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { THEMES, DIRS4, bfs, solve, parseMaze, levelName } from './maze.mjs';

const GAME = join(dirname(fileURLToPath(import.meta.url)), '..');
const OUT = join(GAME, 'assets/levels');
const HAND = join(GAME, 'tools/levels');
const argv = process.argv.slice(2);
const opt = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };
const COUNT = Number(opt('--count') || 120);
const ONLY = opt('--only') ? new Set(opt('--only').split(',').map(Number)) : null;
const PER_THEME = 20;

function rng(seed) {
  let a = seed >>> 0;
  const f = () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  f.int = (lo, hi) => lo + Math.floor(f() * (hi - lo + 1));
  f.pick = (arr) => arr[Math.floor(f() * arr.length)];
  f.shuffle = (arr) => { for (let i = arr.length - 1; i > 0; i--) { const j = Math.floor(f() * (i + 1)); [arr[i], arr[j]] = [arr[j], arr[i]]; } return arr; };
  return f;
}

// ── the tilesets: their layouts, monsters and light ─────────────────────────
const THEME = {
  crypt: { layouts: ['rooms', 'braid', 'vaults', 'rooms'], roster: [['shade', 1], ['ghoul', 1], ['imp', 6], ['hexer', 10], ['bloater', 13], ['ogre', 16]],
           torch: [1.0, 0.6, 0.3], torchE: 70, glow: 'candle' },
  sewer: { layouts: ['braid', 'rings', 'rooms', 'braid'], roster: [['skitter', 1], ['shade', 1], ['bloater', 1], ['ghoul', 3], ['slinger', 5], ['hexer', 9], ['ogre', 14]],
           torch: [0.95, 0.75, 0.45], torchE: 60 },
  foundry: { layouts: ['halls', 'rooms', 'vaults', 'halls'], roster: [['imp', 1], ['slinger', 1], ['ghoul', 1], ['ogre', 4], ['hound', 8], ['hexer', 12]],
             torch: [1.0, 0.48, 0.18], torchE: 95 },
  grotto: { layouts: ['cave', 'cave', 'cave', 'rooms'], roster: [['sporeling', 1], ['skitter', 1], ['shade', 2], ['slinger', 5], ['ogre', 9], ['hexer', 13]],
            torch: [0.35, 0.95, 0.8], torchE: 60 },
  cathedral: { layouts: ['halls', 'rings', 'vaults', 'halls'], roster: [['shade', 1], ['ghoul', 1], ['hexer', 1], ['imp', 4], ['ogre', 8], ['hound', 12]],
               torch: [1.0, 0.82, 0.55], torchE: 75 },
  abyss: { layouts: ['cave', 'braid', 'rings', 'rooms'], roster: [['hound', 1], ['imp', 1], ['shade', 1], ['ogre', 3], ['hexer', 5], ['slinger', 8], ['bloater', 11]],
           torch: [0.62, 0.42, 1.0], torchE: 70 },
};
// the colour each kind's spawner glows (its bake light)
const KIND_GLOW = { shade: [0.6, 0.8, 1.0], ghoul: [0.9, 0.85, 0.6], imp: [1.0, 0.45, 0.15], slinger: [1.0, 0.55, 0.2], hexer: [0.75, 0.35, 1.0], ogre: [0.9, 0.3, 0.2],
                    hound: [1.0, 0.35, 0.1], bloater: [0.5, 1.0, 0.3], skitter: [0.4, 1.0, 0.8], sporeling: [0.3, 1.0, 0.6] };

// floors whose first layout played badly (the level check's clear run: the bots could not finish it): a later seed
const RESEED = { 119: 9, 105: 2 };

// ── the grid ──────────────────────────────────────────────────────────────
const make = (w, h, ch = '#') => Array.from({ length: h }, () => Array(w).fill(ch));
const carve = (g, x0, y0, x1, y1, ch = '.') => {
  for (let y = Math.max(1, Math.min(y0, y1)); y <= Math.min(g.length - 2, Math.max(y0, y1)); y++)
    for (let x = Math.max(1, Math.min(x0, x1)); x <= Math.min(g[0].length - 2, Math.max(x0, x1)); x++) g[y][x] = ch;
};
// an L-shaped corridor of width cw between two points
const corridor = (g, r, ax, ay, bx, by, cw) => {
  const lo = -Math.floor((cw - 1) / 2), hi = lo + cw - 1;
  if (r() < 0.5) { carve(g, ax, ay + lo, bx, ay + hi); carve(g, bx + lo, ay, bx + hi, by); }
  else { carve(g, ax + lo, ay, ax + hi, by); carve(g, ax, by + lo, bx, by + hi); }
};
const corrWidth = (r) => { const v = r(); return v < 0.2 ? 1 : v < 0.8 ? 2 : 3; };

function layoutRooms(w, h, r) {
  const g = make(w, h);
  const leaves = [];
  const split = (x, y, ww, hh, depth) => {
    const can = ww >= 14 || hh >= 12;
    if (!can || depth > 5 || (depth > 2 && r() < 0.15)) { leaves.push([x, y, ww, hh]); return; }
    const vert = ww / hh > 1.2 ? true : hh / ww > 1.2 ? false : r() < 0.5;
    if (vert && ww >= 14) { const s = r.int(6, ww - 7); split(x, y, s, hh, depth + 1); split(x + s, y, ww - s, hh, depth + 1); }
    else if (hh >= 12) { const s = r.int(5, hh - 6); split(x, y, ww, s, depth + 1); split(x, y + s, ww, hh - s, depth + 1); }
    else leaves.push([x, y, ww, hh]);
  };
  split(1, 1, w - 2, h - 2, 0);
  const rooms = leaves.map(([x, y, ww, hh]) => {
    const rw = r.int(Math.max(3, Math.floor(ww * 0.5)), ww - 2), rh = r.int(Math.max(3, Math.floor(hh * 0.5)), hh - 2);
    const rx = x + r.int(1, Math.max(1, ww - rw - 1)), ry = y + r.int(1, Math.max(1, hh - rh - 1));
    carve(g, rx, ry, rx + rw - 1, ry + rh - 1);
    if (rw >= 7 && rh >= 7 && r() < 0.35) /* a few pillars in the bigger rooms */
      for (let py = ry + 2; py < ry + rh - 2; py += 3) for (let px = rx + 2; px < rx + rw - 2; px += 3) g[py][px] = '#';
    return [rx + (rw >> 1), ry + (rh >> 1)];
  });
  r.shuffle(rooms);
  rooms.sort((a, b) => a[0] + a[1] * 0.7 - (b[0] + b[1] * 0.7));
  for (let i = 1; i < rooms.length; i++) corridor(g, r, rooms[i - 1][0], rooms[i - 1][1], rooms[i][0], rooms[i][1], corrWidth(r));
  for (let k = 0; k < Math.floor(rooms.length / 3); k++) { const a = r.pick(rooms), b = r.pick(rooms); corridor(g, r, a[0], a[1], b[0], b[1], corrWidth(r)); }
  return g;
}

function layoutBraid(w, h, r) {
  const g = make(w, h);
  const s = r() < 0.6 ? 3 : 4, cw = s - 1; // a cell: corridor cw wide, walls 1 thick
  const nx = Math.floor((w - 1) / s), ny = Math.floor((h - 1) / s);
  const seen = make(nx, ny, 0);
  const cellXY = (cx, cy) => [1 + cx * s, 1 + cy * s];
  const stack = [[r.int(0, nx - 1), r.int(0, ny - 1)]];
  seen[stack[0][1]][stack[0][0]] = 1;
  const open = (cx, cy) => { const [x, y] = cellXY(cx, cy); carve(g, x, y, x + cw - 1, y + cw - 1); };
  open(...stack[0]);
  while (stack.length) {
    const [cx, cy] = stack[stack.length - 1];
    const n = r.shuffle(DIRS4.map(([dx, dy]) => [cx + dx, cy + dy, dx, dy])).find(([x, y]) => x >= 0 && y >= 0 && x < nx && y < ny && !seen[y][x]);
    if (!n) { stack.pop(); continue; }
    const [x2, y2, dx, dy] = n;
    seen[y2][x2] = 1;
    open(x2, y2);
    const [ax, ay] = cellXY(cx, cy);
    if (dx) carve(g, dx > 0 ? ax + cw : ax - 1, ay, dx > 0 ? ax + cw : ax - 1, ay + cw - 1);
    else carve(g, ax, dy > 0 ? ay + cw : ay - 1, ax + cw - 1, dy > 0 ? ay + cw : ay - 1);
    stack.push([x2, y2]);
  }
  // braid: knock through most dead ends, and open a few rooms
  for (let cy = 0; cy < ny; cy++)
    for (let cx = 0; cx < nx; cx++) {
      const [x, y] = cellXY(cx, cy);
      const exits = DIRS4.filter(([dx, dy]) => { const tx = dx > 0 ? x + cw : dx < 0 ? x - 1 : x, ty = dy > 0 ? y + cw : dy < 0 ? y - 1 : y; return g[ty] && g[ty][tx] === '.'; }).length;
      if (exits === 1 && r() < 0.7) {
        const [dx, dy] = r.pick(DIRS4);
        const tx = dx > 0 ? x + cw : dx < 0 ? x - 1 : x, ty = dy > 0 ? y + cw : dy < 0 ? y - 1 : y;
        if (tx > 0 && ty > 0 && tx < w - 1 && ty < h - 1) dx ? carve(g, tx, y, tx, y + cw - 1) : carve(g, x, ty, x + cw - 1, ty);
      }
    }
  for (let k = 0; k < Math.floor(nx * ny / 18) + 1; k++) { const x = r.int(2, w - 9), y = r.int(2, h - 8); carve(g, x, y, x + r.int(4, 7), y + r.int(3, 6)); }
  return g;
}

function layoutCave(w, h, r) {
  let g = make(w, h);
  for (let y = 1; y < h - 1; y++) for (let x = 1; x < w - 1; x++) g[y][x] = r() < 0.43 ? '#' : '.';
  for (let it = 0; it < 5; it++) {
    const n = make(w, h);
    for (let y = 1; y < h - 1; y++)
      for (let x = 1; x < w - 1; x++) {
        let c = 0;
        for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) c += g[y + dy][x + dx] === '#' ? 1 : 0;
        n[y][x] = c >= 5 ? '#' : '.';
      }
    g = n;
  }
  // a few winding tunnels so the caves join up
  for (let k = 0; k < 4; k++) { let x = r.int(3, w - 4), y = r.int(3, h - 4); for (let s = 0; s < 30; s++) { carve(g, x, y, x + 1, y + 1); const [dx, dy] = r.pick(DIRS4); x = Math.min(w - 3, Math.max(1, x + dx * 2)); y = Math.min(h - 3, Math.max(1, y + dy * 2)); } }
  return g;
}

function layoutHalls(w, h, r) {
  const g = make(w, h);
  const halls = [];
  const cols = w > 34 ? 2 : 1, rows = h > 30 ? 2 : 1;
  const cw = Math.floor((w - 2) / cols), ch = Math.floor((h - 2) / rows);
  for (let j = 0; j < rows; j++)
    for (let i = 0; i < cols; i++) {
      const hw = r.int(Math.floor(cw * 0.55), cw - 3), hh = r.int(Math.floor(ch * 0.55), ch - 3);
      const x = 1 + i * cw + r.int(1, Math.max(1, cw - hw - 2)), y = 1 + j * ch + r.int(1, Math.max(1, ch - hh - 2));
      carve(g, x, y, x + hw - 1, y + hh - 1);
      const step = r.pick([3, 4]);
      for (let py = y + 2; py < y + hh - 2; py += step) for (let px = x + 2; px < x + hw - 2; px += step) if (r() < 0.85) g[py][px] = '#';
      halls.push([x + (hw >> 1), y + (hh >> 1), x, y, hw, hh]);
    }
  for (let i = 1; i < halls.length; i++) corridor(g, r, halls[i - 1][0], halls[i - 1][1], halls[i][0], halls[i][1], r.pick([2, 3]));
  // side chapels off each hall
  for (const [, , x, y, hw, hh] of halls)
    for (let k = 0; k < 2; k++) {
      const side = r.int(0, 3), len = r.int(3, 6);
      const cx = x + r.int(1, hw - 2), cy = y + r.int(1, hh - 2);
      if (side === 0) carve(g, cx - 1, y - len, cx + 1, y - 1); else if (side === 1) carve(g, cx - 1, y + hh, cx + 1, y + hh + len - 1);
      else if (side === 2) carve(g, x - len, cy - 1, x - 1, cy + 1); else carve(g, x + hw, cy - 1, x + hw + len - 1, cy + 1);
    }
  return g;
}

function layoutRings(w, h, r) {
  const g = make(w, h);
  const n = Math.floor((Math.min(w, h) - 2) / 6);
  for (let k = 0; k < n; k++) {
    const x0 = 1 + k * 3, y0 = 1 + k * 3, x1 = w - 2 - k * 3, y1 = h - 2 - k * 3;
    if (x1 - x0 < 3 || y1 - y0 < 3) break;
    carve(g, x0, y0, x1, y0 + 1); carve(g, x0, y1 - 1, x1, y1); carve(g, x0, y0, x0 + 1, y1); carve(g, x1 - 1, y0, x1, y1);
    if (k > 0) { // gaps through the wall to the ring outside, and a few walls across this ring
      for (let gap = 0; gap < 2; gap++) {
        const side = r.int(0, 3);
        if (side < 2) { const x = r.int(x0 + 2, x1 - 3), y = side ? y1 + 1 : y0 - 1; carve(g, x, y, x + 1, y); }
        else { const y = r.int(y0 + 2, y1 - 3), x = side === 3 ? x1 + 1 : x0 - 1; carve(g, x, y, x, y + 1); }
      }
      const cuts = r.int(0, 2);
      for (let c = 0; c < cuts; c++) { const x = r.int(x0 + 3, x1 - 3); g[y0][x] = g[y0 + 1][x] = '#'; }
    }
  }
  const cx = w >> 1, cy = h >> 1; // the middle: a room
  carve(g, cx - 3, cy - 2, cx + 3, cy + 2);
  return g;
}

function layoutVaults(w, h, r) {
  const g = make(w, h);
  const s = r.pick([6, 7, 8]);
  const nx = Math.floor((w - 1) / s), ny = Math.floor((h - 1) / s);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) carve(g, 1 + i * s, 1 + j * s, (i + 1) * s - 1, (j + 1) * s - 1);
  // doorways on a spanning tree plus a few more
  const seen = make(nx, ny, 0), stack = [[0, 0]];
  seen[0][0] = 1;
  const link = (i, j, dx, dy) => {
    const wd = r.pick([1, 2, 2, 3]);
    if (dx) { const x = (i + (dx > 0 ? 1 : 0)) * s, y = 1 + j * s + r.int(0, s - 1 - wd); carve(g, x, y, x, y + wd - 1); }
    else { const y = (j + (dy > 0 ? 1 : 0)) * s, x = 1 + i * s + r.int(0, s - 1 - wd); carve(g, x, y, x + wd - 1, y); }
  };
  while (stack.length) {
    const [i, j] = stack[stack.length - 1];
    const n = r.shuffle(DIRS4.slice()).find(([dx, dy]) => i + dx >= 0 && j + dy >= 0 && i + dx < nx && j + dy < ny && !seen[j + dy][i + dx]);
    if (!n) { stack.pop(); continue; }
    link(i, j, n[0], n[1]);
    seen[j + n[1]][i + n[0]] = 1;
    stack.push([i + n[0], j + n[1]]);
  }
  for (let k = 0; k < Math.floor(nx * ny / 4); k++) { const i = r.int(0, nx - 2), j = r.int(0, ny - 2); r() < 0.5 ? link(i, j, 1, 0) : link(i, j, 0, 1); }
  return g;
}

// a set piece (every tenth floor): a citadel. An outer ring, four great halls off it, a cross of corridors, and in the
// middle a vault walled all round (a pocket the furnish step locks behind a door and fills with treasure)
function layoutCitadel(w, h, r) {
  const g = make(w, h);
  carve(g, 1, 1, w - 2, 2), carve(g, 1, h - 3, w - 2, h - 2), carve(g, 1, 1, 2, h - 2), carve(g, w - 3, 1, w - 2, h - 2);
  const cx = w >> 1, cy = h >> 1, hw = Math.floor(w / 4) - 2, hh = Math.floor(h / 4) - 2;
  for (const [x0, y0] of [[4, 4], [w - 5 - hw, 4], [4, h - 5 - hh], [w - 5 - hw, h - 5 - hh]]) {
    carve(g, x0, y0, x0 + hw, y0 + hh);
    for (let py = y0 + 2; py < y0 + hh - 1; py += 3) for (let px = x0 + 2; px < x0 + hw - 1; px += 3) if (r() < 0.7) g[py][px] = '#';
  }
  carve(g, 3, cy - 1, w - 4, cy + 1), carve(g, cx - 1, 3, cx + 1, h - 4);
  /* the vault: a room in the middle with one way in */
  const vw = 3, vh = 2;
  for (let y = cy - vh - 1; y <= cy + vh + 1; y++) for (let x = cx - vw - 1; x <= cx + vw + 1; x++) g[y][x] = '#';
  carve(g, cx - vw, cy - vh, cx + vw, cy + vh);
  g[cy + vh + 1][cx] = '.';
  /* the cross goes round the vault */
  carve(g, cx - vw - 3, cy - 1, cx - vw - 2, cy + 1), carve(g, cx + vw + 2, cy - 1, cx + vw + 3, cy + 1);
  carve(g, cx - vw - 3, cy - vh - 3, cx + vw + 3, cy - vh - 2), carve(g, cx - vw - 3, cy + vh + 2, cx + vw + 3, cy + vh + 3);
  carve(g, cx - vw - 3, cy - vh - 3, cx - vw - 2, cy + vh + 3), carve(g, cx + vw + 2, cy - vh - 3, cx + vw + 3, cy + vh + 3);
  return g;
}

const LAYOUT = { citadel: layoutCitadel, rooms: layoutRooms, braid: layoutBraid, cave: layoutCave, halls: layoutHalls, rings: layoutRings, vaults: layoutVaults };

// keep the biggest connected floor; wall the rest; make sure the edge is wall
function keepLargest(g) {
  const h = g.length, w = g[0].length;
  for (let x = 0; x < w; x++) g[0][x] = g[h - 1][x] = '#';
  for (let y = 0; y < h; y++) g[y][0] = g[y][w - 1] = '#';
  const m = { w, h };
  const comp = new Int32Array(w * h).fill(-1);
  let best = -1, bestN = 0, id = 0;
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      if (g[y][x] !== '.' || comp[x + y * w] >= 0) continue;
      const d = bfs(m, x, y, (a, b) => g[b][a] === '.');
      let n = 0;
      for (let k = 0; k < d.length; k++) if (d[k] >= 0) comp[k] = id, n++;
      if (n > bestN) bestN = n, best = id;
      id++;
    }
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (g[y][x] === '.' && comp[x + y * w] !== best) g[y][x] = '#';
  return bestN;
}

// ── a floor ──────────────────────────────────────────────────────────────
function generate(floor) {
  const theme = THEMES[Math.min(Math.floor((floor - 1) / PER_THEME), THEMES.length - 1)];
  const T = THEME[theme];
  const local = (floor - 1) % PER_THEME; // 0..19 within the tileset
  for (let attempt = 0; attempt < 60; attempt++) {
    const seed = (floor * 7919 + (attempt + (RESEED[floor] || 0)) * 104729) >>> 0;
    const r = rng(seed);
    const size = Math.min(44, 24 + Math.floor(floor / 8) + r.int(0, 5));
    const w = Math.min(46, size + r.int(4, 8)), h = Math.min(44, size);
    const layout = floor % 10 === 0 ? 'citadel' : r.pick(T.layouts);
    const g = LAYOUT[layout](w, h, r);
    const area = keepLargest(g);
    if (area < w * h * 0.3) continue;
    const m = { floor, theme, w, h, seed, g, start: [], exit: [], spawn: [], mon: [], item: [], tele: [], trap: [], trapwall: [], prop: [], light: [], ambush: [] };
    if (!furnish(m, r, T, local)) continue;
    return m;
  }
  throw new Error(`floor ${floor}: no layout passed in 60 attempts`);
}

const isFloor = (m, x, y) => x >= 0 && y >= 0 && x < m.w && y < m.h && m.g[y][x] === '.';
const open8 = (m, x, y) => { let n = 0; for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) n += isFloor(m, x + dx, y + dy) ? 1 : 0; return n; };

function furnish(m, r, T, local) {
  const { w, h, g } = m;
  const tiles = [];
  for (let y = 1; y < h - 1; y++) for (let x = 1; x < w - 1; x++) if (g[y][x] === '.') tiles.push([x, y]);
  // the start: an open tile near one side, in a space big enough for four players
  const opens = tiles.filter(([x, y]) => open8(m, x, y) === 9);
  if (opens.length < 20) return false;
  const corner = r.int(0, 3);
  const key = ([x, y]) => (corner & 1 ? w - x : x) + (corner & 2 ? h - y : y) * 1.3;
  opens.sort((a, b) => key(a) - key(b));
  const st = opens[r.int(0, Math.min(4, opens.length - 1))];
  m.start.push(st);
  const pass = (x, y) => g[y][x] === '.';
  const d0 = bfs(m, st[0], st[1], pass);
  // the exit: among the farthest open tiles
  const far = tiles.filter(([x, y]) => open8(m, x, y) >= 6).sort((a, b) => d0[b[0] + b[1] * w] - d0[a[0] + a[1] * w]);
  if (!far.length || d0[far[0][0] + far[0][1] * w] < 25) return false;
  const ex = far[r.int(0, Math.min(3, far.length - 1))];
  m.exit.push([ex[0], ex[1], 1]);
  // the main path (by steps back down the distance field)
  const path = [];
  {
    const dx_ = bfs(m, ex[0], ex[1], pass);
    let [x, y] = st;
    path.push([x, y]);
    while (!(x === ex[0] && y === ex[1])) {
      const n = DIRS4.map(([a, b]) => [x + a, y + b]).filter(([a, b]) => isFloor(m, a, b) && dx_[a + b * w] === dx_[x + y * w] - 1);
      if (!n.length) return false;
      [x, y] = n[0];
      path.push([x, y]);
    }
  }
  const onPath = new Set(path.map(([x, y]) => x + y * w));
  const used = new Set([st[0] + st[1] * w, ex[0] + ex[1] * w]);
  // ── doors on the main path, each a corridor's full width, each one a cut between the start and the exit ──
  const ndoors = Math.min(5, (m.floor === 1 ? 1 : 1 + Math.floor(local / 5) + (r() < 0.4 ? 1 : 0)));
  const doors = [];
  for (let i = Math.floor(path.length * 0.18); i < Math.floor(path.length * 0.9) && doors.length < ndoors; i++) {
    if (doors.length && i - doors[doors.length - 1].i < Math.max(8, Math.floor(path.length / (ndoors + 2)))) continue;
    if (r() < 0.6) continue;
    const [x, y] = path[i], [nx, ny] = path[i + 1] || path[i];
    const px = ny - y, py = -(nx - x); // across the path
    if (!px && !py) continue;
    const cut = [[x, y]];
    let ok = true;
    for (const sgn of [1, -1]) {
      let k = 1;
      for (; k <= 3; k++) { const cx = x + px * k * sgn, cy = y + py * k * sgn; if (!isFloor(m, cx, cy)) break; cut.push([cx, cy]); }
      if (k > 3) ok = false;
    }
    if (!ok || cut.some(([a, b]) => used.has(a + b * w) || Math.abs(a - st[0]) + Math.abs(b - st[1]) < 4)) continue;
    // a door only where both sides are walled (it fills the corridor)
    for (const [a, b] of cut) g[b][a] = 'D';
    const dd = bfs(m, st[0], st[1], pass);
    if (dd[ex[0] + ex[1] * w] >= 0) { for (const [a, b] of cut) g[b][a] = '.'; continue; }
    doors.push({ i, cut });
    for (const [a, b] of cut) used.add(a + b * w);
  }
  // keys: key j where doors j.. are shut and the earlier ones open, off the main path where it can be
  const keyTiles = [];
  for (let j = 0; j < doors.length; j++) {
    const passJ = (x, y) => g[y][x] === '.' || (g[y][x] === 'D' && doors.slice(0, j).some((dr) => dr.cut.some(([a, b]) => a === x && b === y)));
    const dj = bfs(m, st[0], st[1], passJ);
    const dp = bfs(m, path[0][0], path[0][1], passJ);
    let best = null, bs = -1;
    for (const [x, y] of tiles) {
      const k = x + y * w;
      if (dj[k] < 0 || used.has(k) || g[y][x] !== '.') continue;
      // far from the path's tiles (a side trip), not too near the start
      let near = 99;
      for (let q = 0; q < path.length; q += 2) { const dd = Math.abs(path[q][0] - x) + Math.abs(path[q][1] - y); if (dd < near) near = dd; }
      const score = Math.min(near, 8) * 3 + Math.min(dj[k], 40) * 0.25 + r() * 4;
      if (score > bs) bs = score, best = [x, y];
    }
    if (!best) return false;
    keyTiles.push(best);
    used.add(best[0] + best[1] * w);
    m.item.push({ x: best[0], y: best[1], kind: 'key' });
    void dp;
  }
  // side vaults: a dead-end pocket behind a door of its own, treasure inside, its key in reach before the first door
  const pockets = [];
  if (local >= 2 && r() < 0.6) {
    for (const [x, y] of r.shuffle(tiles.slice())) {
      if (pockets.length >= 2) break;
      if (onPath.has(x + y * w) || used.has(x + y * w) || g[y][x] !== '.') continue;
      // a 1 or 2 wide neck: shut it and see what is cut off
      const neck = [[x, y]];
      for (const [dx, dy] of [[1, 0], [0, 1]]) if (isFloor(m, x + dx, y + dy) && !isFloor(m, x - dx, y - dy) && r() < 0.5) neck.push([x + dx, y + dy]);
      for (const [a, b] of neck) g[b][a] = 'D';
      const dd = bfs(m, st[0], st[1], pass);
      const cutoff = tiles.filter(([a, b]) => g[b][a] === '.' && dd[a + b * w] < 0);
      const okPocket = dd[ex[0] + ex[1] * w] >= 0 && cutoff.length >= 4 && cutoff.length <= 40 && !cutoff.some(([a, b]) => used.has(a + b * w) || onPath.has(a + b * w));
      if (!okPocket) { for (const [a, b] of neck) g[b][a] = '.'; continue; }
      for (const [a, b] of neck) used.add(a + b * w);
      pockets.push(cutoff);
      // its treasure
      for (let k = 0; k < Math.min(4, cutoff.length - 1); k++) {
        const [a, b] = cutoff[k * 3 % cutoff.length];
        if (used.has(a + b * w)) continue;
        used.add(a + b * w);
        m.item.push({ x: a, y: b, kind: k === 0 ? (r() < 0.35 ? `elixir_${r.pick(['armor', 'shot', 'speedshot', 'magic', 'melee', 'speed'])}` : 'chest') : r.pick(['gold', 'gold', 'cup', 'food_plate', 'potion']) });
      }
      // its key: anywhere reachable before the first main door
      const d1 = bfs(m, st[0], st[1], (a, b) => g[b][a] === '.');
      const cand = tiles.filter(([a, b]) => g[b][a] === '.' && d1[a + b * w] > 6 && !used.has(a + b * w));
      if (!cand.length) return false;
      const k = r.pick(cand);
      used.add(k[0] + k[1] * w);
      m.item.push({ x: k[0], y: k[1], kind: 'key' });
    }
  }
  // ── a skip exit now and then, in the far corner of the floor from both ──
  if (m.floor + 4 <= COUNT && (m.floor % 5 === 3 || (m.floor % 10 === 7 && m.floor + 8 <= COUNT))) {
    const skip = m.floor % 10 === 7 && m.floor + 8 <= COUNT ? 8 : 4;
    const dd = bfs(m, st[0], st[1], pass);
    const c = tiles.filter(([x, y]) => g[y][x] === '.' && dd[x + y * w] > 15 && !used.has(x + y * w) && !onPath.has(x + y * w) && open8(m, x, y) >= 5 && Math.abs(x - ex[0]) + Math.abs(y - ex[1]) > 10);
    if (c.length) { const e = r.pick(c); m.exit.push([e[0], e[1], skip]); used.add(e[0] + e[1] * w); }
  }
  // ── spawners: on open ground, away from the start, apart from each other ──
  const spawnable = T.roster.filter(([, from]) => local + 1 >= from).map(([k]) => k);
  const nsp = Math.min(16, Math.round(3 + m.floor * 0.08 + r() * 3 + tiles.length / 200));
  const pStr2 = Math.min(0.8, Math.max(0, (m.floor - 3) / 45)), pStr3 = Math.min(0.5, Math.max(0, (m.floor - 25) / 120));
  const dstart = bfs(m, st[0], st[1], (x, y) => g[y][x] !== '#' && g[y][x] !== 'T');
  const cands = r.shuffle(tiles.filter(([x, y]) => g[y][x] === '.' && open8(m, x, y) >= 8 && dstart[x + y * w] >= 9 && !used.has(x + y * w)));
  for (const [x, y] of cands) {
    if (m.spawn.length >= nsp) break;
    if (m.spawn.some((s) => Math.abs(s.x - x) + Math.abs(s.y - y) < 5)) continue;
    if (onPath.has(x + y * w) && r() < 0.7) continue;
    const kind = r.pick(spawnable);
    const str = Math.min(kind === 'ogre' ? 2 : 3, 1 + (r() < pStr2 ? 1 : 0) + (r() < pStr3 ? 1 : 0)); // the heaviest come at strength 2 at most
    m.spawn.push({ x, y, kind, str });
    used.add(x + y * w);
  }
  if (m.spawn.length < Math.min(3, nsp)) return false;
  // monsters standing about at the start, round the spawners
  const nmon = Math.min(60, Math.floor(m.floor * 0.5 + 4));
  for (let k = 0; k < nmon; k++) {
    const s = r.pick(m.spawn);
    const x = s.x + r.int(-3, 3), y = s.y + r.int(-3, 3);
    if (!isFloor(m, x, y) || used.has(x + y * w) || dstart[x + y * w] < 8) continue;
    m.mon.push({ x, y, kind: s.kind, str: s.str });
  }
  if (m.floor >= 9 && r() < Math.min(0.7, 0.15 + m.floor / 150)) { // a dread waits deep in the floor
    const c = tiles.filter(([x, y]) => g[y][x] === '.' && dstart[x + y * w] > 30 && !used.has(x + y * w));
    if (c.length) { const [x, y] = r.pick(c); m.mon.push({ x, y, kind: 'dread', str: 1 }); }
  }
  // ── rooms that spring: open ground on the way, bait in the middle, monsters rising all round when you step in ──
  if (local >= 2) {
    const nam = Math.min(3, 1 + Math.floor(local / 8) + (r() < 0.3 ? 1 : 0));
    const cand = path.slice(Math.floor(path.length * 0.25), Math.floor(path.length * 0.85)).filter(([x, y]) => open8(m, x, y) === 9 && !used.has(x + y * w));
    r.shuffle(cand);
    const amb = [];
    for (const [x, y] of cand) {
      if (amb.length >= nam) break;
      if (amb.some(([a, b]) => Math.abs(a - x) + Math.abs(b - y) < 12) || m.spawn.some((s) => Math.abs(s.x - x) + Math.abs(s.y - y) < 5)) continue;
      amb.push([x, y]);
      used.add(x + y * w);
      const kind = r.pick(spawnable.filter((k) => k !== 'ogre' && k !== 'bloater')) || 'ghoul';
      m.ambush.push({ x, y, kind, str: 1 + (r() < pStr2 ? 1 : 0), n: Math.min(30, 8 + Math.floor(m.floor / 5)) });
      m.item.push({ x, y, kind: r() < 0.5 ? 'chest' : 'cup' }); /* the bait */
    }
  }
  // ── food, potions, treasure, charms ──
  const freeTile = (pred = () => true) => {
    for (let t = 0; t < 400; t++) {
      const [x, y] = r.pick(tiles);
      if (g[y][x] === '.' && !used.has(x + y * w) && pred(x, y)) { used.add(x + y * w); return [x, y]; }
    }
    return null;
  };
  const nfood = 3 + Math.floor(tiles.length / 110) + Math.floor(m.floor / 25) + r.int(0, 2);
  for (let k = 0; k < nfood; k++) { const t = freeTile((x, y) => dstart[x + y * w] > 6); if (t) m.item.push({ x: t[0], y: t[1], kind: r.pick(['food_roast', 'food_roast', 'food_bread', 'food_flagon', 'food_flagon', 'food_plate']) }); }
  const npot = 1 + r.int(0, 2) + (m.floor > 30 ? 1 : 0);
  for (let k = 0; k < npot; k++) { const t = freeTile((x, y) => !onPath.has(x + y * w)); if (t) m.item.push({ x: t[0], y: t[1], kind: 'potion' }); }
  const deadEnd = (x, y) => DIRS4.filter(([a, b]) => isFloor(m, x + a, y + b)).length <= 1;
  const ntre = 4 + r.int(0, 5) + Math.floor(tiles.length / 150);
  for (let k = 0; k < ntre; k++) {
    const t = freeTile((x, y) => k < 3 ? deadEnd(x, y) || open8(m, x, y) <= 5 : true);
    if (!t) continue;
    const v = r();
    m.item.push({ x: t[0], y: t[1], kind: v < 0.55 ? 'gold' : v < 0.8 ? 'cup' : r() < 1 / 6 ? 'chest_trap' : 'chest' });
  }
  const ncharm = 2 + r.int(0, 2) + (m.floor > 40 ? 1 : 0);
  const charmPool = m.floor < 6 ? [0, 1, 2, 8, 13] : m.floor < 15 ? [0, 1, 2, 3, 4, 8, 10, 11, 13] : [...Array(16).keys()];
  for (let k = 0; k < ncharm; k++) { const t = freeTile((x, y) => dstart[x + y * w] > 5); if (t) m.item.push({ x: t[0], y: t[1], kind: `charm_${r.pick(charmPool)}` }); }
  if (m.floor % 4 === 0) { const t = freeTile((x, y) => deadEnd(x, y)); if (t) m.item.push({ x: t[0], y: t[1], kind: `elixir_${r.pick(['armor', 'shot', 'speedshot', 'magic', 'melee', 'speed'])}` }); }
  // ── walls that break, walls that vanish, teleporters ──
  // the regions the doors make (doors shut): a shortcut, a vanishing wall or a teleporter never joins two of them, so
  // every door stays the only way on
  const region = new Int32Array(w * h).fill(-1);
  for (let y = 0, id = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      if (g[y][x] !== '.' || region[x + y * w] >= 0) continue;
      const d = bfs(m, x, y, (a, b) => g[b][a] === '.');
      for (let k = 0; k < d.length; k++) if (d[k] >= 0) region[k] = id;
      id++;
    }
  const same = (ax, ay, bx, by) => region[ax + ay * w] >= 0 && region[ax + ay * w] === region[bx + by * w];
  const between = [];
  for (let y = 1; y < h - 1; y++)
    for (let x = 1; x < w - 1; x++) {
      if (g[y][x] !== '#') continue;
      if ((isFloor(m, x - 1, y) && isFloor(m, x + 1, y) && !isFloor(m, x, y - 1) && !isFloor(m, x, y + 1) && same(x - 1, y, x + 1, y)) ||
          (isFloor(m, x, y - 1) && isFloor(m, x, y + 1) && !isFloor(m, x - 1, y) && !isFloor(m, x + 1, y) && same(x, y - 1, x, y + 1))) between.push([x, y]);
    }
  r.shuffle(between);
  const nbreak = Math.min(between.length, 2 + Math.floor(local / 3));
  for (let k = 0; k < nbreak; k++) { const [x, y] = between.pop(); g[y][x] = 'B'; }
  if (local >= 3 && r() < 0.45 && between.length >= 2) {
    const grp = 1;
    const tw = between.splice(0, Math.min(between.length, r.int(2, 4)));
    for (const [x, y] of tw) { g[y][x] = 'T'; m.trapwall.push([x, y, grp]); }
    const [x0, y0] = tw[0];
    const c = tiles.filter(([x, y]) => g[y][x] === '.' && !used.has(x + y * w) && Math.abs(x - x0) + Math.abs(y - y0) <= 8 && Math.abs(x - x0) + Math.abs(y - y0) >= 3);
    if (c.length) { const t = r.pick(c); used.add(t[0] + t[1] * w); m.trap.push([t[0], t[1], grp]); }
    else for (const [x, y] of tw) g[y][x] = '#', m.trapwall.length = 0;
  }
  if (local >= 5 && r() < 0.35) {
    const c = r.shuffle(tiles.filter(([x, y]) => g[y][x] === '.' && open8(m, x, y) >= 8 && !used.has(x + y * w) && dstart[x + y * w] >= 8));
    const tp = [];
    for (const t of c) { if (tp.length >= 3) break; if ((!tp.length || same(tp[0][0], tp[0][1], t[0], t[1])) && tp.every((q) => Math.abs(q[0] - t[0]) + Math.abs(q[1] - t[1]) > 12)) tp.push(t); }
    if (tp.length >= 2) for (const t of tp) { used.add(t[0] + t[1] * w); m.tele.push(t); }
  }
  // ── the tileset's torches (on wall faces the camera sees) and decor (in corners) ──
  const torches = [];
  const torchCand = [];
  for (const [x, y] of tiles) {
    if (g[y][x] !== '.') continue;
    if (g[y - 1][x] === '#') torchCand.push([x, y, 'n']);
    if (g[y][x - 1] === '#' && r() < 0.35) torchCand.push([x, y, 'w']);
    if (g[y][x + 1] === '#' && r() < 0.35) torchCand.push([x, y, 'e']);
  }
  r.shuffle(torchCand);
  const ntorch = Math.min(90, Math.floor(tiles.length / 14));
  for (const [x, y, s] of torchCand) {
    if (torches.length >= ntorch) break;
    if (torches.some((t) => Math.abs(t[0] - x) + Math.abs(t[1] - y) < 5)) continue;
    torches.push([x, y, s]);
    m.prop.push({ x, y, kind: 'torch', frame: 0, side: s });
    const off = { n: [0, -0.35], s: [0, 0.35], e: [0.35, 0], w: [-0.35, 0] }[s];
    void off;
    m.light.push({ x, y, h: 1.0, r: T.torch[0], g: T.torch[1], b: T.torch[2], energy: T.torchE, range: 8 });
  }
  let ndec = 0;
  for (const [x, y] of r.shuffle(tiles.slice())) {
    if (ndec >= Math.floor(tiles.length / 30)) break;
    if (g[y][x] !== '.' || used.has(x + y * w) || onPath.has(x + y * w)) continue;
    const walls = DIRS4.filter(([a, b]) => g[y + b][x + a] === '#').length;
    if (walls < 2 || DIRS4.filter(([a, b]) => isFloor(m, x + a, y + b)).length < 1) continue;
    if (walls >= 3) continue; // never block a dead end's only way out of view
    used.add(x + y * w);
    m.prop.push({ x, y, kind: 'decor', frame: r.int(0, 3), side: String(r.int(0, 3)) });
    ndec++;
  }
  for (const s of m.spawn) if (m.light.length < 120) { const c = KIND_GLOW[s.kind]; m.light.push({ x: s.x, y: s.y, h: 1.0, r: c[0], g: c[1], b: c[2], energy: 18 + 8 * s.str, range: 4.5 }); }
  for (const [x, y, skip] of m.exit) m.light.push({ x, y, h: 0.6, r: skip > 1 ? 0.5 : 1.0, g: skip > 1 ? 0.7 : 0.85, b: skip > 1 ? 1.0 : 0.6, energy: 40, range: 5 });
  // the proof the level check runs
  const mm = toMaze(m);
  const s = solve(mm, { adversarial: true });
  if (!s.ok) return false;
  return true;
}

function toMaze(m) { return parseMaze(write(m)); }

function write(m) {
  const L = [];
  L.push('crawler-maze 1');
  L.push(`# floor ${m.floor}: written by tools/gen-levels.mjs (seed ${m.seed}); edit the generator or tools/levels/, not this file`);
  L.push(`floor ${m.floor}`);
  L.push(`theme ${m.theme}`);
  L.push(`size ${m.w} ${m.h}`);
  L.push(`seed ${m.seed}`);
  L.push('grid');
  for (const row of m.g) L.push(row.join('').replace(/\s+$/, ''));
  L.push('end');
  for (const [x, y] of m.start) L.push(`start ${x} ${y}`);
  for (const [x, y, s] of m.exit) L.push(`exit ${x} ${y} ${s}`);
  for (const s of m.spawn) L.push(`spawner ${s.x} ${s.y} ${s.kind} ${s.str}`);
  for (const s of m.mon) L.push(`monster ${s.x} ${s.y} ${s.kind} ${s.str}`);
  for (const it of m.item) L.push(`item ${it.x} ${it.y} ${it.kind}`);
  for (const [x, y] of m.tele) L.push(`teleport ${x} ${y}`);
  for (const [x, y, gr] of m.trap) L.push(`trap ${x} ${y} ${gr}`);
  for (const [x, y, gr] of m.trapwall) L.push(`trapwall ${x} ${y} ${gr}`);
  for (const a of m.ambush || []) L.push(`ambush ${a.x} ${a.y} ${a.kind} ${a.str} ${a.n}`);
  for (const p of m.prop) L.push(`prop ${p.x} ${p.y} ${p.kind} ${p.frame} ${p.side}`);
  const f = (v) => String(Math.round(v * 100) / 100);
  for (const l of m.light) L.push(`light ${l.x} ${l.y} ${f(l.h)} ${f(l.r)} ${f(l.g)} ${f(l.b)} ${f(l.energy)} ${f(l.range)}`);
  return L.join('\n') + '\n';
}

// a floor drawn by hand: its grid and records as written; torches, decor and lights added when it has none
function handmade(floor, text) {
  const p = parseMaze(text);
  const m = { floor, theme: p.theme, w: p.w, h: p.h, seed: 0, g: p.grid.map((row) => row.map((t) => ({ floor: '.', wall: '#', door: 'D', break: 'B', trapwall: 'T', void: ' ' })[t])),
              start: p.start, exit: p.exit, spawn: p.spawn, mon: p.mon, item: p.item, tele: p.tele, trap: p.trap, trapwall: p.trapwall, prop: p.prop, light: p.light, ambush: p.ambush };
  if (!m.prop.length) {
    const T = THEME[m.theme], r = rng(floor * 31337);
    const torches = [];
    for (let y = 1; y < m.h - 1; y++)
      for (let x = 1; x < m.w - 1; x++)
        if (m.g[y][x] === '.' && m.g[y - 1][x] === '#' && torches.every((t) => Math.abs(t[0] - x) + Math.abs(t[1] - y) >= 5) && r() < 0.5) {
          torches.push([x, y]);
          m.prop.push({ x, y, kind: 'torch', frame: 0, side: 'n' });
          m.light.push({ x, y, h: 1.0, r: T.torch[0], g: T.torch[1], b: T.torch[2], energy: T.torchE, range: 8 });
        }
    for (const s of m.spawn) { const c = KIND_GLOW[s.kind]; m.light.push({ x: s.x, y: s.y, h: 1.0, r: c[0], g: c[1], b: c[2], energy: 18 + 8 * s.str, range: 4.5 }); }
    for (const [x, y] of m.exit) m.light.push({ x, y, h: 0.6, r: 1, g: 0.85, b: 0.6, energy: 40, range: 5 });
  }
  if (!solve(toMaze(m), { adversarial: true }).ok) throw new Error(`tools/levels/${String(floor).padStart(3, '0')}.txt: the exit cannot be reached with the keys on the way`);
  return m;
}

mkdirSync(OUT, { recursive: true });
let written = 0;
for (let f = 1; f <= COUNT; f++) {
  if (ONLY && !ONLY.has(f)) continue;
  const hand = join(HAND, `${String(f).padStart(3, '0')}.txt`);
  const m = existsSync(hand) ? handmade(f, readFileSync(hand, 'utf8')) : generate(f);
  writeFileSync(join(OUT, levelName(f)), write(m));
  written++;
}
writeFileSync(join(OUT, 'count.txt'), `${COUNT}\n`);
console.log(`gen-levels: wrote ${written} floors (of ${COUNT}) to ${OUT}`);
