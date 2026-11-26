// The Battle map: open country about 240 m across, five command posts 60 to 90 m apart, lanes between them with cover
// along each one, low hills between the lanes to break the long sight lines, a field wall round the edge and a steep
// berm behind it. Unlike the three arenas (rooms cut out of solid blocks) the ground is a height field, written here as
// meshes in the .cwcsg format (one per surface: grass, dirt, gravel), so the game draws, collides and paths over it as it
// does over any baked level piece. What stands on it is CSG (ruined farm buildings, cast concrete cover walls, the field
// wall) and the arena props. Writes:
//   levels/battle.cwscene          the CSG shapes, baked by cwcsg into levels/battle_ruins.cwcsg and battle_cover.cwcsg
//   levels/battle_grass.cwcsg, battle_dirt.cwcsg, battle_gravel.cwcsg   the ground by surface
//   levels/battle_rings.cwcsg      the posts' capture rings, painted on the gravel
//   levels/battle.lvl              posts, spawns, items, props, flag poles (each base on the ground under it)
// and returns a flow report over the posts and lanes (connected, loops, no dead ends, every post pair 60 m or more apart,
// each post on at least two lanes 60 to 90 m long).
//
// The ground's heights are worked out once, and every placement reads the surface on the same triangles the game's mesh
// has (each quad split from its (i, j + 1) corner to its (i + 1, j)), so a prop's base is exactly on the ground.
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

const f = (v) => (Math.round(v * 1000) / 1000).toString();
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const lerp = (a, b, t) => a + (b - a) * t;

export const BATTLE = {
  half: 120,       // the playing area's half side: the field wall stands at WALL, the berm rises behind it
  wall: 108,
  ext: 140,        // the ground's half side: the berm and the hills behind it
  spacing: 1.25,
  // the five posts: A and E are each side's home post; the rest start neutral
  posts: [
    { name: 'FARM', x: -80, z: 20, home: 0 },
    { name: 'RELAY', x: -30, z: -54, home: -1 },
    { name: 'CROSSROADS', x: 0, z: 0, home: -1 },
    { name: 'DEPOT', x: 30, z: 54, home: -1 },
    { name: 'QUARRY', x: 80, z: -20, home: 1 },
  ],
  ring: 8,
  // the lanes: the post pairs a walking route joins (two triangles round the middle post, two long flanks)
  lanes: [[0, 1], [0, 2], [1, 2], [2, 3], [2, 4], [3, 4], [1, 4], [0, 3]],
};

function rng(seed) { let s = seed >>> 0; return () => { s = (s * 1664525 + 1013904223) >>> 0; return (s >>> 8) / 16777216; }; }
function segDist(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
  const t = Math.max(0, Math.min(1, ((px - ax) * dx + (pz - az) * dz) / l2));
  const qx = ax + dx * t - px, qz = az + dz * t - pz;
  return Math.sqrt(qx * qx + qz * qz);
}

// write false: the report only (tools/battle-check.mjs)
export function battle(OUT, write = true) {
  const B = BATTLE, P = B.posts;
  const N = (2 * B.ext) / B.spacing, S = N + 1, org = -B.ext;
  const laneD = (x, z) => Math.min(...B.lanes.map(([a, b]) => segDist(x, z, P[a].x, P[a].z, P[b].x, P[b].z)));
  const postD = (x, z) => Math.min(...P.map((p) => Math.hypot(x - p.x, z - p.z)));

  // ── the ground ────────────────────────────────────────────────────────────────────────────────────────────────────
  const base = (x, z) => 2.4 * Math.sin(x * 0.031 + 0.7) * Math.sin(z * 0.027 + 1.3) + 1.1 * Math.sin(x * 0.071 + z * 0.05 + 2) +
    0.5 * Math.sin(x * 0.13 - z * 0.11) + 0.25 * Math.sin(x * 0.31 + 1) * Math.sin(z * 0.29 + 2);
  // hills between the lanes, never on one: candidates on a jittered grid, kept where no lane or post is near
  const R = rng(0xBA771E), hills = [];
  for (let gz = -105; gz <= 105; gz += 26)
    for (let gx = -105; gx <= 105; gx += 26) {
      const x = gx + (R() - 0.5) * 16, z = gz + (R() - 0.5) * 16;
      const r = 13 + R() * 9, h = 3.5 + R() * 5.5;
      if (laneD(x, z) < r * 0.55 + 9 || postD(x, z) < 30) continue;
      hills.push({ x, z, r, h });
    }
  const hillAt = (x, z) => {
    let h = 0;
    for (const k of hills) { const d = Math.hypot(x - k.x, z - k.z) / k.r; if (d < 1) h += k.h * (1 - smooth(0, 1, d)); }
    return h * smooth(5, 13, laneD(x, z)); // a lane keeps 5 m either side of its line clear, rising to the hills by 13 m
  };
  const edgeAt = (x, z) => {
    const e = Math.max(Math.abs(x), Math.abs(z)) - 109;
    const far = 0.5 * Math.sin(x * 0.05 + 1) * Math.sin(z * 0.043) + 0.5;
    // past the field wall the country rises in long grassy slopes (under 30 degrees: the ground's top-down texture holds)
    return e > 0 ? (0.42 * e + 5 * smooth(0, 26, e) + 3 * Math.sin(x * 0.045 + z * 0.03) * smooth(4, 20, e)) * (0.8 + 0.4 * far) : 0;
  };
  const raw = (x, z) => base(x, z) + hillAt(x, z) + edgeAt(x, z);

  // flat pads: each post (its ring and the cover round it), each structure, each cover site; the ground eases into them
  const pads = [];
  for (const p of P) { p.y = base(p.x, p.z); pads.push({ x: p.x, z: p.z, r: 15, blend: 12, y: p.y }); }

  // ── what stands on it (worked out before the ground is final: each piece asks for its pad) ───────────────────────────
  const csg = { ruins: [], cover: [] }, props = [], spawns = [], items = [], postWalls = [];
  const R2 = rng(0x5EED77);
  // each post: a flag at the middle (drawn by the game), four low walls round the ring with gaps between them, a ruined
  // building on the side away from the lanes, crates and barrels for close cover inside the walls
  P.forEach((p, pi) => {
    // the bearing away from every lane leaving this post: the ruin stands there
    let ax = 0, az = 0;
    for (const [a, b] of B.lanes) {
      if (a !== pi && b !== pi) continue;
      const o = P[a === pi ? b : a], l = Math.hypot(o.x - p.x, o.z - p.z);
      ax += (o.x - p.x) / l, az += (o.z - p.z) / l;
    }
    const away = Math.atan2(-ax, -az);
    for (let k = 0; k < 4; k++) {
      const a = away + Math.PI / 4 + (k * Math.PI) / 2, r = 11.5;
      postWalls.push({ x: p.x + Math.sin(a) * r, z: p.z + Math.cos(a) * r, yaw: a, len: 5.5, h: 1.15, t: 0.45, y: p.y });
    }
    {
      const r = 20, x = p.x + Math.sin(away) * r, z = p.z + Math.cos(away) * r;
      const y = base(x, z) * 0.5 + p.y * 0.5;
      csg.ruins.push({ x, z, w: 9, d: 7, h: 3.4, y, yaw: away, seed: pi });
      pads.push({ x, z, r: 8.5, blend: 7, y });
    }
    // close cover inside the post: two crate stacks and barrels at r 5.5 off the lane bearings
    for (let k = 0; k < 3; k++) {
      const a = away + Math.PI * 0.6 + k * Math.PI * 0.4, r = 5.5 + k * 0.4;
      const x = p.x + Math.sin(a) * r, z = p.z + Math.cos(a) * r;
      props.push(k === 1 ? { kind: 'barrels', x, z, y: p.y } : { kind: 'crates', x, z, yaw: a, y: p.y });
    }
    // three spawns behind the post's walls (on the ruin's side), items by the ruin
    for (let k = -1; k <= 1; k++) {
      const a = away + k * 0.5, r = 13.6;
      spawns.push({ x: p.x + Math.sin(a) * r, z: p.z + Math.cos(a) * r, y: p.y });
    }
    const ia = away + 1.1, ib = away - 1.1;
    items.push({ kind: 'health', x: p.x + Math.sin(ia) * 3.2, z: p.z + Math.cos(ia) * 3.2, y: p.y });
    items.push({ kind: 'ammo', x: p.x + Math.sin(ib) * 3.2, z: p.z + Math.cos(ib) * 3.2, y: p.y });
  });
  // power items on the middle posts' outskirts (out in the open, a reason to leave cover)
  items.push({ kind: 'rocket', x: P[2].x, z: P[2].z - 9.5, y: P[2].y });
  items.push({ kind: 'rail', x: P[1].x + 9.5, z: P[1].z, y: P[1].y });
  items.push({ kind: 'rail', x: P[3].x - 9.5, z: P[3].z, y: P[3].y });
  items.push({ kind: 'armor', x: P[2].x, z: P[2].z + 9.5, y: P[2].y });
  items.push({ kind: 'scatter', x: P[1].x - 9.5, z: P[1].z, y: P[1].y });
  items.push({ kind: 'scatter', x: P[3].x + 9.5, z: P[3].z, y: P[3].y });

  // the lanes: cover every 11 to 15 m, alternating sides 3.5 to 6 m off the line, the piece chosen in turn: a concrete wall
  // across the lane at an angle, a pair of barriers, a crate stack, a half buried wall with barrels; a ruin halfway along
  // the long lanes
  const coverSites = [];
  B.lanes.forEach(([a, b], li) => {
    const pa = P[a], pb = P[b], dx = pb.x - pa.x, dz = pb.z - pa.z, L = Math.hypot(dx, dz), ux = dx / L, uz = dz / L, nx = -uz, nz = ux;
    let s = 20 + R2() * 3, side = li & 1 ? 1 : -1, kind = li % 4;
    while (s < L - 20) {
      const off = side * (3.5 + R2() * 2.5);
      const x = pa.x + ux * s + nx * off, z = pa.z + uz * s + nz * off;
      if (postD(x, z) > 18) coverSites.push({ x, z, yaw: Math.atan2(ux, uz), kind, side });
      s += 11 + R2() * 4, side = -side, kind = (kind + 1) % 4;
    }
    if (L > 100) {
      const x = pa.x + ux * L * 0.5 - nx * 13, z = pa.z + uz * L * 0.5 - nz * 13;
      const y = base(x, z);
      csg.ruins.push({ x, z, w: 8, d: 6, h: 3.0, y, yaw: Math.atan2(ux, uz), seed: 10 + li });
      pads.push({ x, z, r: 7.5, blend: 6, y });
    }
  });
  for (const c of coverSites) {
    c.y = base(c.x, c.z) + hillAt(c.x, c.z);
    pads.push({ x: c.x, z: c.z, r: 3.4, blend: 4, y: c.y });
  }

  // ── the final ground: the raw field eased into every pad (later pads over earlier ones) ─────────────────────────────
  const Hm = new Float32Array(S * S);
  for (let j = 0; j < S; j++)
    for (let i = 0; i < S; i++) {
      const x = org + i * B.spacing, z = org + j * B.spacing;
      let h = raw(x, z);
      for (const p of pads) {
        const d = Math.hypot(x - p.x, z - p.z);
        if (d < p.r + p.blend) h = lerp(h, p.y, 1 - smooth(p.r, p.r + p.blend, d));
      }
      Hm[j * S + i] = Math.fround(h);
    }
  const qh = (i, j) => Hm[Math.min(S - 1, Math.max(0, j)) * S + Math.min(S - 1, Math.max(0, i))];
  // the surface at (x, z) on the triangles the mesh is made of (each quad split from its (i, j + 1) corner to its (i + 1, j))
  const ground = (x, z) => {
    const gx = (x - org) / B.spacing, gz = (z - org) / B.spacing, i = Math.floor(gx), j = Math.floor(gz), u = gx - i, v = gz - j;
    const h00 = qh(i, j), h10 = qh(i + 1, j), h01 = qh(i, j + 1), h11 = qh(i + 1, j + 1);
    return u + v <= 1 ? h00 + u * (h10 - h00) + v * (h01 - h00) : h11 + (1 - u) * (h01 - h11) + (1 - v) * (h10 - h11);
  };
  // the lowest ground under the four probe points the grounding check casts (world axes, h/2 + 4 cm out): a prop's base
  const footing = (x, z, hx, hz) => {
    const h = Math.min(hx, hz) * 0.5 + 0.04;
    return Math.min(ground(x - h, z - h), ground(x + h, z - h), ground(x - h, z + h), ground(x + h, z + h));
  };
  const lowest = (x, z, r) => { let m = Infinity; for (let a = 0; a < 8; a++) m = Math.min(m, ground(x + Math.sin(a * 0.785) * r, z + Math.cos(a * 0.785) * r)); return Math.min(m, ground(x, z)); };

  // the ground as three meshes by surface: grass fields, churned dirt along the lanes, round the posts and on steep ground,
  // gravel in the posts' rings. Each triangle goes to one surface by a noisy test at its middle (the borders wander)
  const surf = { grass: [], dirt: [], gravel: [] };
  for (let j = 0; j < N; j++)
    for (let i = 0; i < N; i++)
      for (const tri of [[[i, j], [i, j + 1], [i + 1, j]], [[i + 1, j], [i, j + 1], [i + 1, j + 1]]]) {
        const x = org + ((tri[0][0] + tri[1][0] + tri[2][0]) / 3) * B.spacing, z = org + ((tri[0][1] + tri[1][1] + tri[2][1]) / 3) * B.spacing;
        const n = 0.5 + 0.5 * Math.sin(x * 0.21 + Math.sin(z * 0.17) * 2) * Math.cos(z * 0.19 - x * 0.07);
        const n2 = 0.5 + 0.5 * Math.sin(x * 0.83 + z * 0.61) * Math.sin(z * 0.97 - x * 0.37);
        const sx = qh(tri[0][0] + 1, tri[0][1]) - qh(tri[0][0] - 1, tri[0][1]), sz = qh(tri[0][0], tri[0][1] + 1) - qh(tri[0][0], tri[0][1] - 1);
        const slope = Math.hypot(sx, sz) / (2 * B.spacing), pd = postD(x, z);
        let k = 'grass';
        if (pd < B.ring + 1.6 + n2 * 1.2) k = 'gravel';
        else if (laneD(x, z) < 0.9 + 1.8 * n + 0.8 * n2 || pd < 11 + 2.5 * n + n2 || slope > 1.2) k = 'dirt'; // (the hills' sides stay grass: only bare banks steeper than 50 degrees show dirt)
        surf[k].push(tri);
      }
  const meshFile = (name, tris) => {
    const used = new Map(), pos = [], nor = [], uv = [], idx = [];
    const vert = (i, j) => {
      const key = j * S + i;
      if (!used.has(key)) {
        used.set(key, pos.length / 3);
        const x = org + i * B.spacing, z = org + j * B.spacing, y = qh(i, j);
        const nx = qh(i - 1, j) - qh(i + 1, j), nz = qh(i, j - 1) - qh(i, j + 1), ny = 2 * B.spacing, l = Math.hypot(nx, ny, nz);
        pos.push(x, y, z), nor.push(nx / l, ny / l, nz / l), uv.push(x, z);
      }
      return used.get(key);
    };
    for (const t of tris) for (const [i, j] of t) idx.push(vert(i, j));
    if (write) writeCwcsg(join(OUT, `battle_${name}.cwcsg`), pos, nor, uv, idx);
  };

  // ── the CSG scene ─────────────────────────────────────────────────────────────────────────────────────────────────
  const lines = ['[scene format=1]', '', '[node name="Level" type="Node3D"]', ''];
  const root = (name) => lines.push(`[node name="${name}" parent="." type="CSGCombiner3D"]`, `bake_file = "battle_${name.toLowerCase()}.cwcsg"`, '');
  let nb = 0;
  const box = (parent, cx, cy, cz, sx, sy, sz, yaw, op) => lines.push(`[node name="B${nb++}" parent="${parent}" type="CSGBox3D"]`,
    ...(op ? [`operation = ${op}`] : []), `position = vec3(${f(cx)}, ${f(cy)}, ${f(cz)})`, ...(yaw ? [`rotation = vec3(0, ${f(yaw)}, 0)`] : []),
    `size = vec3(${f(sx)}, ${f(sy)}, ${f(sz)})`, '');
  // a local frame: along (the yaw's x axis) and across (its z axis)
  const at = (o, lx, lz) => [o.x + Math.cos(o.yaw) * lx + Math.sin(o.yaw) * lz, o.z - Math.sin(o.yaw) * lx + Math.cos(o.yaw) * lz];
  // ruins: a stone shell (a solid block minus its inside), a doorway in each long wall, windows, the tops broken off
  // unevenly, a corner gone, part of a floor slab left over one end. Their bottoms go 0.6 m into the ground
  root('Ruins');
  for (const r of csg.ruins) {
    const grp = `Ruin${r.seed}`, rr = rng(0x7000 + r.seed);
    lines.push(`[node name="${grp}" parent="Ruins" type="CSGCombiner3D"]`, `position = vec3(${f(r.x)}, 0, ${f(r.z)})`, `rotation = vec3(0, ${f(r.yaw)}, 0)`, '');
    const y0 = lowest(r.x, r.z, Math.hypot(r.w, r.d) * 0.5) - 0.6, top = r.y + r.h;
    r.floor = r.y;
    box(`Ruins/${grp}`, 0, (y0 + top) / 2, 0, r.w, top - y0, r.d);
    box(`Ruins/${grp}`, 0, (r.y + top + 1) / 2, 0, r.w - 1.0, top + 1 - r.y, r.d - 1.0, 0, 'SUBTRACTION');
    for (const s of [-1, 1]) {
      box(`Ruins/${grp}`, s * (r.w * 0.18), r.y + 1.3, s * (r.d / 2), 1.8, 2.6, 1.4, 0, 'SUBTRACTION');                  // doorways
      box(`Ruins/${grp}`, -s * (r.w * 0.28), r.y + 1.6, s * (r.d / 2), 1.3, 1.1, 1.4, 0, 'SUBTRACTION');                  // windows
      box(`Ruins/${grp}`, s * (r.w / 2), r.y + 1.6, 0, 1.4, 1.1, 1.4, 0, 'SUBTRACTION');
    }
    // broken tops: notches out of the wall heads, a whole corner down to a stump
    for (let k = 0; k < 5; k++) {
      const lx = (rr() - 0.5) * r.w, lz = rr() < 0.5 ? -r.d / 2 : r.d / 2, dep = 0.5 + rr() * 1.3;
      box(`Ruins/${grp}`, lx, top - dep / 2 + 0.3, lz, 1.2 + rr() * 2.2, dep + 0.6, 1.4, rr() * 0.4 - 0.2, 'SUBTRACTION');
    }
    box(`Ruins/${grp}`, r.w / 2, top - 0.9, -r.d / 2, 3.2, 2.4, 3.2, 0.6, 'SUBTRACTION');
    // a slab of the upper floor left over one end, resting on the walls
    lines.push(`[node name="Slab${r.seed}" parent="Ruins/${grp}" type="CSGBox3D"]`, `position = vec3(${f(-r.w / 2 + 1.6)}, ${f(top - 0.85)}, 0)`, `size = vec3(3.2, 0.3, ${f(r.d - 0.2)})`, '');
  }
  // cover walls: cast concrete, 1.15 m (crouch-free cover to the chest), sunk 0.5 m under their lowest ground
  root('Cover');
  const wall = (o) => {
    const [ax, az] = at(o, -o.len / 2, 0), [bx, bz] = at(o, o.len / 2, 0);
    const y0 = Math.min(lowest(ax, az, 0.4), lowest(bx, bz, 0.4), lowest(o.x, o.z, 0.4)) - 0.5, top = o.y + o.h;
    box('Cover', o.x, (y0 + top) / 2, o.z, o.len, top - y0, o.t, o.yaw);
    o.top = top;
  };
  for (const w of postWalls) wall(w);
  for (const c of coverSites) {
    if (c.kind === 0) { const o = { x: c.x, z: c.z, yaw: c.yaw + (c.side > 0 ? 1.1 : -1.1), len: 4.2, h: 1.2, t: 0.5, y: c.y }; wall(o); }
    else if (c.kind === 1) {
      for (const s of [-1, 1]) {
        const a = c.yaw + Math.PI / 2 + s * 0.35, x = c.x + Math.sin(c.yaw) * s * 1.2, z = c.z + Math.cos(c.yaw) * s * 1.2;
        props.push({ kind: 'jersey', x, z, yaw: a });
      }
    } else if (c.kind === 2) props.push({ kind: 'crates', x: c.x, z: c.z, yaw: c.yaw + 0.3, y: c.y });
    else {
      const o = { x: c.x, z: c.z, yaw: c.yaw + Math.PI / 2, len: 2.6, h: 0.9, t: 0.5, y: c.y };
      wall(o);
      props.push({ kind: 'barrels', x: c.x + Math.sin(c.yaw) * 1.3 - 0.3, z: c.z + Math.cos(c.yaw) * 1.3 - 0.3, y: c.y });
    }
  }
  // the field wall round the edge: stone, 2.6 m over the higher ground under each 6 m segment, sunk under the lower
  root('Edge');
  for (const sx of [-1, 1])
    for (let t = -B.wall; t < B.wall - 0.01; t += 6) {
      for (const along of [0, 1]) {
        const a = t + 3, x = along ? a : sx * B.wall, z = along ? sx * B.wall : a;
        const e0 = along ? [t, z] : [x, t], e1 = along ? [t + 6, z] : [x, t + 6];
        const g0 = Math.min(ground(...e0), ground(...e1), ground(x, z)), g1 = Math.max(ground(...e0), ground(...e1), ground(x, z));
        const lo = g0 - 0.8, top = g1 + 2.6;
        box('Edge', x, (lo + top) / 2, z, along ? 6.02 : 0.8, top - lo, along ? 0.8 : 6.02);
      }
    }
  const scene = lines.join('\n') + '\n';

  // ── the level file ────────────────────────────────────────────────────────────────────────────────────────────────
  const o = [`# battle: written by tools/make-levels.mjs (tools/battle-map.mjs), do not edit`, `half ${B.half}`, `top 4.1`, `storey 3.6`,
    'csg battle_grass.cwcsg leaf', 'csg battle_dirt.cwcsg floor', 'csg battle_gravel.cwcsg clad', 'csg battle_rings.cwcsg trim',
    'csg battle_ruins.cwcsg wall', 'csg battle_cover.cwcsg accent', 'csg battle_edge.cwcsg pillar'];
  // posts: index, name, centre on the ground, ring radius, home side (-1: neutral)
  // (the game reads them for the Battle mode; the arenas have none)
  P.forEach((p, i) => o.push(`post ${i} ${p.name} ${f(p.x)} ${f(ground(p.x, p.z))} ${f(p.z)} ${B.ring} ${p.home}`));
  // each post's flag pole on a concrete footing, at the ring's middle
  for (const p of P) {
    const y = footing(p.x, p.z, 1.0, 1.0);
    o.push(`block accent ${f(p.x)} ${f(y - 0.25)} ${f(p.z)} 1.0 0.5 1.0`, `block metal ${f(p.x)} ${f(y + 0.25)} ${f(p.z)} 0.14 8.5 0.14`);
  }
  for (const s of spawns) o.push(`spawn ${f(s.x)} ${f(ground(s.x, s.z) + 0.05)} ${f(s.z)}`);
  for (const it of items) o.push(`item ${it.kind} ${f(it.x)} ${f(ground(it.x, it.z))} ${f(it.z)}`);
  let nprops = 0;
  for (const p of props) {
    if (p.kind === 'crates') {
      // the three crates' probe points (crate_stack: two side by side, one on top): the stack's base is the lowest
      const cy = Math.cos(p.yaw), sy = Math.sin(p.yaw);
      const y = Math.min(footing(p.x, p.z, 1, 1), footing(p.x + 1.05 * cy, p.z - 1.05 * sy, 1, 1));
      o.push(`crates ${f(p.x)} ${f(p.z)} ${f(p.yaw)} ${f(y)}`), nprops += 3;
    } else if (p.kind === 'barrels') {
      const y = Math.min(footing(p.x, p.z, 0.6, 0.6), footing(p.x + 0.65, p.z + 0.2, 0.6, 0.6), footing(p.x + 0.3, p.z - 0.6, 0.6, 0.6));
      o.push(`barrels ${f(p.x)} ${f(p.z)} ${f(y)}`), nprops += 3;
    } else if (p.kind === 'jersey') {
      o.push(`prop jersey ${f(p.x)} ${f(footing(p.x, p.z, 2.0, 0.6))} ${f(p.z)} ${f(p.yaw)} 1 2.0 0.8 0.6`), nprops++;
    }
  }
  const lvl = o.join('\n') + '\n';

  if (write) writeFileSync(join(OUT, 'battle.cwscene'), scene), writeFileSync(join(OUT, 'battle.lvl'), lvl);
  for (const k of Object.keys(surf)) meshFile(k, surf[k]);
  // the capture rings: a band 0.3 m wide painted 1 cm proud of the gravel (flat: the posts' pads are level)
  {
    const pos = [], nor = [], uv = [], idx = [], SEG = 64;
    for (const p of P) {
      const y = ground(p.x, p.z) + 0.01, v0 = pos.length / 3;
      for (let k = 0; k <= SEG; k++) {
        const a = (k / SEG) * Math.PI * 2, c = Math.cos(a), sn = Math.sin(a);
        for (const r of [B.ring - 0.15, B.ring + 0.15]) pos.push(p.x + c * r, y, p.z + sn * r), nor.push(0, 1, 0), uv.push((k / SEG) * Math.PI * 2 * B.ring, r - B.ring);
      }
      for (let k = 0; k < SEG; k++) { const a = v0 + k * 2; idx.push(a, a + 2, a + 1, a + 1, a + 2, a + 3); }
    }
    if (write) writeCwcsg(join(OUT, 'battle_rings.cwcsg'), pos, nor, uv, idx);
  }

  // ── the flow report over the posts and lanes ──────────────────────────────────────────────────────────────────────
  const deg = P.map((_, i) => B.lanes.filter(([a, b]) => a === i || b === i).length);
  const parent = P.map((_, i) => i), find = (v) => (parent[v] === v ? v : (parent[v] = find(parent[v])));
  for (const [a, b] of B.lanes) parent[find(a)] = find(b);
  const comps = new Set(P.map((_, i) => find(i))).size, loops = B.lanes.length - P.length + comps;
  const dist = (a, b) => Math.hypot(P[a].x - P[b].x, P[a].z - P[b].z);
  let minPair = Infinity;
  for (let a = 0; a < P.length; a++) for (let b = a + 1; b < P.length; b++) minPair = Math.min(minPair, dist(a, b));
  const near = B.lanes.filter(([a, b]) => dist(a, b) <= 90).map(([a, b]) => Math.round(dist(a, b)));
  const each = P.map((_, i) => B.lanes.filter(([a, b]) => (a === i || b === i) && dist(a, b) >= 60 && dist(a, b) <= 90).length);
  const dead = P.filter((_, i) => deg[i] < 2).map((p) => p.name);
  const span = [Math.min(...P.map((p) => p.x)), Math.max(...P.map((p) => p.x)), Math.min(...P.map((p) => p.z)), Math.max(...P.map((p) => p.z))];
  const rep = {
    arena: 'battle', size_m: 2 * B.half, posts: P.length, lanes: B.lanes.length, lane_m_60_90: near, loops, components: comps, dead_ends: dead,
    min_post_gap_m: Math.round(minPair), cover_sites: coverSites.length, ruins: csg.ruins.length, hills: hills.length, solid_props: nprops,
    mesh: { quads: N, spacing: B.spacing, tris: Object.fromEntries(Object.entries(surf).map(([k, v]) => [k, v.length])) }, post_span: span,
    pass: comps === 1 && loops >= 3 && dead.length === 0 && minPair >= 60 && each.every((n) => n >= 2) && nprops <= 240 && spawns.length <= 16 && items.length <= 32,
  };
  Object.defineProperty(rep, 'ground', { value: ground }); // (the ground height at x, z: tools/battle-shots.mjs)
  return rep;
}

// a mesh in the engine's .cwcsg layout (src/scene/csg.c): "CWCSG001", vertex and index counts, an input hash, bounds, then
// positions, normals, colours (white), uvs (metres) and indices
function writeCwcsg(path, pos, nor, uv, idx) {
  const v = pos.length / 3, b = Buffer.alloc(48 + v * 44 + idx.length * 4);
  b.write('CWCSG001', 0, 'latin1');
  b.writeUInt32LE(v, 8), b.writeUInt32LE(idx.length, 12);
  let h = 0xcbf29ce484222325n;
  for (const x of pos) { h ^= BigInt(Math.round(x * 1000) & 0xffffffff); h = (h * 0x100000001b3n) & 0xffffffffffffffffn; }
  b.writeBigUInt64LE(h, 16);
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let k = 0; k < v; k++) for (let c = 0; c < 3; c++) { lo[c] = Math.min(lo[c], pos[k * 3 + c]); hi[c] = Math.max(hi[c], pos[k * 3 + c]); }
  [...lo, ...hi].forEach((x, k) => b.writeFloatLE(x, 24 + k * 4));
  let o = 48;
  for (const x of pos) b.writeFloatLE(x, o), (o += 4);
  for (const x of nor) b.writeFloatLE(x, o), (o += 4);
  for (let k = 0; k < v * 3; k++) b.writeFloatLE(1, o), (o += 4);
  for (const x of uv) b.writeFloatLE(x, o), (o += 4);
  for (const x of idx) b.writeUInt32LE(x, o), (o += 4);
  writeFileSync(path, b);
}
