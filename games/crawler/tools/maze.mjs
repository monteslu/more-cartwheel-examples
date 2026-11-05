// crawler floors in JavaScript: the .cwmaze parser (the format is maze.h's), its bake hash, and the walks the
// generator and the level check share. Plain ESM, no dependencies.

export const THEMES = ['crypt', 'sewer', 'foundry', 'grotto', 'cathedral', 'abyss'];
export const TILE = { '.': 'floor', '#': 'wall', D: 'door', B: 'break', T: 'trapwall', ' ': 'void' };
export const ITEMS = ['key', 'potion', 'gold', 'cup', 'chest', 'chest_trap', 'food_roast', 'food_bread', 'food_flagon', 'food_plate'];
export const ELIXIRS = ['armor', 'shot', 'speedshot', 'magic', 'melee', 'speed'];
// the monsters the game knows (sim.c's KIND_NAME) and which a spawner may make
export const MONSTERS = ['shade', 'ghoul', 'imp', 'slinger', 'hexer', 'dread', 'pilferer', 'ogre', 'hound', 'bloater', 'skitter', 'sporeling'];
export const SPAWNABLE = ['shade', 'ghoul', 'imp', 'slinger', 'hexer', 'ogre', 'hound', 'bloater', 'skitter', 'sporeling'];

const pad3 = (n) => String(n).padStart(3, '0');
export const levelName = (n) => `${pad3(n)}.cwmaze`;
export const bakeName = (n) => `${pad3(n)}.cwlm`;

/** parse a floor's text; throws with the line on a bad record */
export function parseMaze(text) {
  const lines = text.split('\n');
  const m = { floor: 0, theme: null, w: 0, h: 0, seed: 0, grid: [], start: [], exit: [], spawn: [], mon: [], item: [], tele: [], trap: [], trapwall: [], prop: [], light: [], ambush: [] };
  let i = 0;
  const bad = (msg) => { throw new Error(`line ${i + 1}: ${msg}`); };
  let header = false;
  for (; i < lines.length; i++) {
    const ln = lines[i].replace(/[\s\r]+$/, '');
    if (!ln || ln[0] === '#') continue;
    const w = ln.trim().split(/\s+/);
    const n = (k) => { const v = Number(w[k]); if (!Number.isFinite(v)) bad(`not a number: ${w[k]}`); return v; };
    if (!header) { if (w[0] !== 'crawler-maze') bad('no crawler-maze header'); header = true; continue; }
    switch (w[0]) {
      case 'floor': m.floor = n(1); break;
      case 'seed': m.seed = n(1); break;
      case 'name': break;
      case 'theme': if (!THEMES.includes(w[1])) bad(`unknown theme ${w[1]}`); m.theme = w[1]; break;
      case 'size': m.w = n(1); m.h = n(2); if (m.w < 4 || m.h < 4 || m.w > 48 || m.h > 48) bad('bad size'); break;
      case 'grid':
        for (let y = 0; y < m.h; y++) {
          i++;
          if (i >= lines.length) bad('the grid ends early');
          const row = lines[i].replace(/[\s\r]+$/, '');
          if (row.length > m.w) bad(`grid row ${y} is ${row.length} wide, not ${m.w}`);
          const r = [];
          for (let x = 0; x < m.w; x++) {
            const ch = x < row.length ? row[x] : ' ';
            if (!(ch in TILE)) bad(`unknown tile '${ch}'`);
            r.push(TILE[ch]);
          }
          m.grid.push(r);
        }
        i++;
        if ((lines[i] || '').trim() !== 'end') bad('the grid must close with end');
        break;
      case 'start': m.start.push([n(1), n(2)]); break;
      case 'exit': m.exit.push([n(1), n(2), n(3)]); break;
      case 'spawner': case 'monster': {
        if (!MONSTERS.includes(w[3])) bad(`unknown monster ${w[3]}`);
        const s = n(4); if (s < 1 || s > 3) bad('strength 1..3');
        (w[0] === 'spawner' ? m.spawn : m.mon).push({ x: n(1), y: n(2), kind: w[3], str: s });
        break;
      }
      case 'item': {
        const k = w[3];
        if (!(ITEMS.includes(k) || (k.startsWith('elixir_') && ELIXIRS.includes(k.slice(7))) || (/^charm_(\d+)$/.test(k) && Number(k.slice(6)) < 16))) bad(`unknown item ${k}`);
        m.item.push({ x: n(1), y: n(2), kind: k });
        break;
      }
      case 'teleport': m.tele.push([n(1), n(2)]); break;
      case 'trap': m.trap.push([n(1), n(2), n(3)]); break;
      case 'trapwall': m.trapwall.push([n(1), n(2), n(3)]); break;
      case 'ambush': if (!MONSTERS.includes(w[3])) bad(`unknown monster ${w[3]}`); m.ambush.push({ x: n(1), y: n(2), kind: w[3], str: n(4), n: n(5) }); break;
      case 'prop': {
        if (!['decor', 'torch'].includes(w[3])) bad(`unknown prop ${w[3]}`);
        m.prop.push({ x: n(1), y: n(2), kind: w[3], frame: n(4), side: w[5] });
        break;
      }
      case 'light': m.light.push({ x: n(1), y: n(2), h: n(3), r: n(4), g: n(5), b: n(6), energy: n(7), range: n(8) }); break;
      default: bad(`unknown record ${w[0]}`);
    }
  }
  if (!header) throw new Error('empty file');
  if (!m.theme) throw new Error('no theme');
  if (m.grid.length !== m.h) throw new Error('no grid');
  if (!m.start.length) throw new Error('no start');
  if (!m.exit.length) throw new Error('no exit');
  return m;
}

/** FNV-1a 64 over the theme, size, grid, prop and light lines (maze.c's maze_bake_hash): 16 hex digits */
export function bakeHash(text) {
  let h = 0xcbf29ce484222325n;
  const P = 0x100000001b3n, M = (1n << 64n) - 1n;
  const feed = (s) => { for (const c of Buffer.from(s + '\n', 'latin1')) h = ((h ^ BigInt(c)) * P) & M; };
  const lines = text.split('\n');
  let grid = false, rows = 0, hgt = 0;
  for (const raw of lines) {
    const ln = raw.replace(/[\s\r]+$/, '');
    const s = ln.replace(/^[ \t]+/, '');
    if (grid) {
      if (s === 'end') grid = false;
      else if (rows++ < hgt) feed(ln);
      continue;
    }
    if (s === 'grid') { grid = true; continue; }
    if (s.startsWith('size ')) { hgt = Number(s.split(/\s+/)[2]); feed(s); continue; }
    if (s.startsWith('theme ') || s.startsWith('prop ') || s.startsWith('light ')) feed(s);
  }
  return h.toString(16).padStart(16, '0');
}

export const DIRS4 = [[1, 0], [-1, 0], [0, 1], [0, -1]];
export const inside = (m, x, y) => x >= 0 && y >= 0 && x < m.w && y < m.h;

/** breadth-first steps from (sx, sy) over tiles pass(x, y) says can be walked; -1 unreached */
export function bfs(m, sx, sy, pass) {
  const d = new Int32Array(m.w * m.h).fill(-1);
  if (!pass(sx, sy)) return d;
  const q = [sx + sy * m.w];
  d[q[0]] = 0;
  for (let h = 0; h < q.length; h++) {
    const c = q[h], x = c % m.w, y = (c / m.w) | 0;
    for (const [dx, dy] of DIRS4) {
      const nx = x + dx, ny = y + dy;
      if (!inside(m, nx, ny)) continue;
      const k = nx + ny * m.w;
      if (d[k] >= 0 || !pass(nx, ny)) continue;
      d[k] = d[c] + 1;
      q.push(k);
    }
  }
  return d;
}

/** the doors as groups: every door tile touching another (4-way) is one door, opened by one key */
export function doorGroups(m) {
  const id = new Int32Array(m.w * m.h).fill(-1);
  const groups = [];
  for (let y = 0; y < m.h; y++)
    for (let x = 0; x < m.w; x++) {
      if (m.grid[y][x] !== 'door' || id[x + y * m.w] >= 0) continue;
      const g = [], q = [[x, y]];
      id[x + y * m.w] = groups.length;
      while (q.length) {
        const [cx, cy] = q.pop();
        g.push([cx, cy]);
        for (const [dx, dy] of DIRS4) {
          const nx = cx + dx, ny = cy + dy;
          if (inside(m, nx, ny) && m.grid[ny][nx] === 'door' && id[nx + ny * m.w] < 0) { id[nx + ny * m.w] = groups.length; q.push([nx, ny]); }
        }
      }
      groups.push(g);
    }
  return { groups, id };
}

/**
 * Can the team reach an exit with the keys it finds on the way? A key opens any one door group (the game's rule:
 * walking into a door with a key opens it). Greedy is exact for that rule: open any door you can reach while you
 * hold a key, collect every key you can reach. Breakable walls count as open (two hits), trap walls as shut, spawners
 * as open (they can be shot down). Returns { ok, order: [the door groups in the order opened], keysFirst, reach }.
 */
export function solve(m, { adversarial = false } = {}) {
  const { groups, id } = doorGroups(m);
  const open = new Uint8Array(groups.length);
  const keyAt = new Map();
  for (const it of m.item) if (it.kind === 'key') keyAt.set(it.x + it.y * m.w, (keyAt.get(it.x + it.y * m.w) || 0) + 1);
  const taken = new Set();
  let keys = 0;
  const order = [];
  const [sx, sy] = m.start[0];
  for (let round = 0; round < 1000; round++) {
    const pass = (x, y) => {
      const t = m.grid[y][x];
      if (t === 'floor' || t === 'break') return true;
      if (t === 'door') return open[id[x + y * m.w]] === 1;
      return false;
    };
    const d = bfs(m, sx, sy, pass);
    for (const [k, n] of keyAt) if (d[k] >= 0 && !taken.has(k)) { taken.add(k); keys += n; }
    if (m.exit.some(([x, y, s]) => s === 1 && d[x + y * m.w] >= 0)) return { ok: true, order, keys, reach: d };
    // a door group next to the reached area (adversarial: the one that gains the least, a player spending a key on
    // a side room first; the floor must still be finishable)
    const openable = [];
    for (let g = 0; g < groups.length; g++) {
      if (open[g]) continue;
      if (groups[g].some(([x, y]) => DIRS4.some(([dx, dy]) => inside(m, x + dx, y + dy) && d[x + dx + (y + dy) * m.w] >= 0))) openable.push(g);
    }
    let next = openable.length ? openable[0] : -1;
    if (adversarial && openable.length > 1) {
      let worst = Infinity;
      for (const g of openable) {
        open[g] = 1;
        const d2 = bfs(m, sx, sy, (x, y) => {
          const t = m.grid[y][x];
          return t === 'floor' || t === 'break' || (t === 'door' && open[id[x + y * m.w]] === 1);
        });
        open[g] = 0;
        let gain = 0;
        for (const [k, n] of keyAt) if (d2[k] >= 0 && !taken.has(k)) gain += n;
        if (m.exit.some(([x, y, s]) => s === 1 && d2[x + y * m.w] >= 0)) gain += 100;
        if (gain < worst) worst = gain, next = g;
      }
    }
    if (next < 0 || keys <= 0) return { ok: false, order, keys, reach: d, stuck: next < 0 ? 'no door to open' : 'a door but no key' };
    open[next] = 1;
    keys--;
    order.push(next);
  }
  return { ok: false, order, keys, stuck: 'too many rounds' };
}

export const worldX = (m, tx) => (tx - m.w / 2 + 0.5) * 2;
export const worldZ = (m, ty) => (ty - m.h / 2 + 0.5) * 2;

/** the bake's full fingerprint (mazebuild.c's mazebuild_bake_hash): the text's hash folded with the bytes of the
 * tileset's wall, torch and decor kits, each followed by its length */
export function bakeHashFull(text, theme, modelsDir, readFile) {
  let h = BigInt('0x' + bakeHash(text));
  const P = 0x100000001b3n, M = (1n << 64n) - 1n;
  for (const part of ['wall', 'torch', 'decor']) {
    let b = Buffer.alloc(0);
    try { b = readFile(`${modelsDir}/kit_${theme}_${part}.cwchar`); } catch { /* a kit not made: its length 0 */ }
    for (const c of b) h = ((h ^ BigInt(c)) * P) & M;
    h = ((h ^ BigInt(b.length)) * P) & M;
  }
  return h.toString(16).padStart(16, '0');
}
