// Smooth skinned geometry for the characters: lofts (rings of superellipses along a path, closed or open arcs, with a
// per-angle radius modifier for muscles, folds and pockets), ellipsoids, capsules and tapered cones, with smooth normals,
// metric UVs and up to four bone weights per vertex. Used by make-characters.mjs.
export const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
export const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
export const len = (a) => Math.hypot(a[0], a[1], a[2]);
export const norm = (a) => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
const lerp = (a, b, t) => a + (b - a) * t;
const sp = (x, e) => Math.sign(x) * Math.pow(Math.abs(x), 2 / e);

/** an orthonormal frame (U, V) perpendicular to dir */
export function frame(dir) {
  const d = norm(dir), up = Math.abs(d[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0];
  const U = norm(cross(d, up)), V = norm(cross(d, U));
  return [U, V];
}

export class SkinMesh {
  constructor() { this.groups = new Map(); }
  group(mat) {
    if (!this.groups.has(mat)) this.groups.set(mat, { pos: [], nrm: [], uv: [], idx: [], joints: [], weights: [], tag: [] });
    return this.groups.get(mat);
  }
  /** vertices with explicit normals, uvs, weights and tags plus triangles (local indices): verts [{p, n, uv, w, tag}] */
  raw(mat, verts, tris) {
    const g = this.group(mat), base = g.pos.length / 3;
    for (const v of verts) {
      g.pos.push(...v.p); g.nrm.push(...v.n); g.uv.push(...v.uv); g.tag.push(v.tag ?? null);
      if (v.t) (g.tan ||= []).push(...v.t);
      const acc = new Map();
      for (const [b, x] of v.w) if (x > 1e-4) acc.set(b, (acc.get(b) || 0) + x);
      const w = [...acc].sort((x, y) => y[1] - x[1]).slice(0, 4), sum = w.reduce((q, x) => q + x[1], 0) || 1;
      for (let i = 0; i < 4; i++) { g.joints.push(w[i] ? w[i][0] : 0); g.weights.push(w[i] ? w[i][1] / sum : 0); }
    }
    for (const [a, b, c] of tris) g.idx.push(base + a, base + b, base + c);
  }
  tris() { let n = 0; for (const g of this.groups.values()) n += g.idx.length / 3; return n; }
  /**
   * rings: [{c, U, V, ru, rv, e, mod(a)}]: loops of superellipses; mod(a) multiplies the radius at angle a (0 along U,
   * PI/2 along V). opts: n (points per ring), arc [a0, a1] (an open strip instead of a closed loop), tile (uv metres),
   * weights(k, P, a) -> [[bone, w]...] or bone (one bone), uvFn(P, k, j, n, a) -> [u, v], flip
   */
  loft(mat, allRings, o = {}) {
    // a level of detail (this.lod < 1, crowd models): fewer points per ring and every other ring, ends kept
    const lod = this.lod ?? 1, step = lod < 1 ? Math.max(1, Math.round(1 / lod)) : 1;
    const keep = allRings.map((_, k) => k).filter((k) => k === 0 || k === allRings.length - 1 || k % step === 0);
    const rings = keep.map((k) => allRings[k]);
    const g = this.group(mat), n = Math.max(3, Math.round((o.n ?? 20) * (lod < 1 ? Math.max(0.35, lod) : 1))), K = rings.length, tile = o.tile ?? 0.5;
    const arc = o.arc, closed = !arc;
    const a0 = arc ? arc[0] : 0, a1 = arc ? arc[1] : Math.PI * 2;
    const base = g.pos.length / 3;
    const P = [], UV = [], A = [];
    let dist = 0;
    for (let k = 0; k < K; k++) {
      const r = rings[k];
      if (k > 0) dist += len(sub(r.c, rings[k - 1].c));
      const b0 = r.arc ? r.arc[0] : a0, b1 = r.arc ? r.arc[1] : a1; // a ring may narrow or widen the strip (a wrap front)
      const perim = (b1 - b0) * Math.max(0.01, (r.ru + r.rv) / 2);
      for (let j = 0; j <= n; j++) {
        const a = b0 + (j / n) * (b1 - b0), e = r.e ?? 2;
        const m = r.mod ? r.mod(a) : 1;
        const sx = sp(Math.cos(a), e), sz = sp(Math.sin(a), e);
        const p = add(r.c, add(mul(r.U, r.ru * sx * m), mul(r.V, r.rv * sz * m)));
        P.push(p); A.push(a);
        UV.push(o.uvFn ? o.uvFn(p, k, j, n, a) : [(j / n) * perim / tile, dist / tile]);
      }
    }
    const idx = (k, j) => k * (n + 1) + j;
    const tris = [];
    for (let k = 0; k < K - 1; k++) for (let j = 0; j < n; j++) {
      const a = idx(k, j), b = idx(k, j + 1), c = idx(k + 1, j + 1), d = idx(k + 1, j);
      tris.push([a, b, c], [a, c, d]);
    }
    let flip = o.flip;
    if (flip === undefined) {
      let s = 0;
      for (const [a, b, c] of tris) {
        const fn = cross(sub(P[b], P[a]), sub(P[c], P[a]));
        const k = Math.floor(a / (n + 1));
        s += dot(fn, sub(P[a], rings[k].c));
      }
      flip = s < 0;
    }
    const N = P.map(() => [0, 0, 0]);
    for (const [a, b, c] of tris) {
      const fn = cross(sub(P[b], P[a]), sub(P[c], P[a]));
      for (const v of [a, b, c]) { N[v][0] += fn[0]; N[v][1] += fn[1]; N[v][2] += fn[2]; }
    }
    if (closed) for (let k = 0; k < K; k++) { // close the seam
      const a = idx(k, 0), b = idx(k, n), s = add(N[a], N[b]);
      N[a] = s.slice(); N[b] = s.slice();
    }
    for (let v = 0; v < P.length; v++) {
      let nn = norm(N[v]); if (flip) nn = mul(nn, -1);
      const k = Math.floor(v / (n + 1));
      if (len(N[v]) < 1e-12) nn = norm(sub(P[v], rings[k].c));
      g.pos.push(...P[v]); g.nrm.push(...nn); g.uv.push(...UV[v]); g.tag.push(o.tag ?? null);
      let w = o.weights ? o.weights(keep[k], P[v], A[v]) : [[o.bone, 1]];
      const acc = new Map();
      for (const [b, x] of w) if (x > 1e-4) acc.set(b, (acc.get(b) || 0) + x);
      w = [...acc].sort((x, y) => y[1] - x[1]).slice(0, 4);
      const sum = w.reduce((q, x) => q + x[1], 0) || 1;
      for (let i = 0; i < 4; i++) { g.joints.push(w[i] ? w[i][0] : 0); g.weights.push(w[i] ? w[i][1] / sum : 0); }
    }
    for (const [a, b, c] of tris) g.idx.push(base + a, ...(flip ? [base + c, base + b] : [base + b, base + c]));
  }
  /** an ellipsoid about c with radii (rx, ry, rz), rings stacked along y (or along o.axis with frame o.U/o.V) */
  ellipsoid(mat, c, rx, ry, rz, o = {}) {
    const steps = o.steps ?? 8, rings = [];
    const ax = o.axis ?? [0, 1, 0], U = o.U ?? [1, 0, 0], V = o.V ?? [0, 0, 1];
    for (let k = 0; k <= steps; k++) {
      const th = (k / steps) * Math.PI;
      rings.push({ c: sub(c, mul(ax, ry * Math.cos(th))), U, V, ru: Math.max(0.0015, rx * Math.sin(th)), rv: Math.max(0.0015, rz * Math.sin(th)), e: o.e, mod: o.mod });
    }
    this.loft(mat, rings, { n: o.n ?? 14, ...o });
  }
  /** a capsule from a to b, radii rA and rB (ratio stretches the depth), rounded ends; weights(t, P, a) with t 0..1 along a to b */
  capsule(mat, a, b, rA, rB, o = {}) {
    const d = sub(b, a), dir = norm(d);
    const [U, V] = o.U ? [o.U, o.V] : frame(dir);
    const hs = o.caps ?? 4, m = o.rings ?? 4, ratio = o.ratio ?? 1, rings = [], ts = [];
    for (let i = 0; i <= hs; i++) { const an = (Math.PI / 2) * (i / hs); rings.push({ c: sub(a, mul(dir, rA * Math.cos(an))), U, V, ru: Math.max(0.0015, rA * Math.sin(an)), rv: Math.max(0.0015, rA * Math.sin(an) * ratio), e: o.e, mod: o.mod }); ts.push(0); }
    for (let i = 1; i < m; i++) { const t = i / m; rings.push({ c: add(a, mul(d, t)), U, V, ru: lerp(rA, rB, t), rv: lerp(rA, rB, t) * ratio, e: o.e, mod: o.mod }); ts.push(t); }
    for (let i = 0; i <= hs; i++) { const an = (Math.PI / 2) * (i / hs); rings.push({ c: add(b, mul(dir, rB * Math.sin(an))), U, V, ru: Math.max(0.0015, rB * Math.cos(an)), rv: Math.max(0.0015, rB * Math.cos(an) * ratio), e: o.e, mod: o.mod }); ts.push(1); }
    const w = o.weights ? (k, P, an) => o.weights(ts[k], P, an) : undefined;
    this.loft(mat, rings, { n: o.n ?? 14, ...o, weights: w });
  }
  /** a cone (spike, hair clump) from base centre a to tip b; base radius r, optional ratio; rounded base */
  cone(mat, a, b, r, o = {}) {
    const d = sub(b, a), dir = norm(d);
    const [U, V] = o.U ? [o.U, o.V] : frame(dir);
    const ratio = o.ratio ?? 1, m = o.rings ?? 5, rings = [];
    rings.push({ c: sub(a, mul(dir, r * 0.5)), U, V, ru: 0.0015, rv: 0.0015 });
    rings.push({ c: sub(a, mul(dir, r * 0.2)), U, V, ru: r * 0.8, rv: r * 0.8 * ratio });
    for (let i = 0; i <= m; i++) {
      const t = i / m, k = Math.pow(1 - t, o.pow ?? 1.2);
      const bend = o.bend ? mul(o.bend, t * t) : [0, 0, 0];
      rings.push({ c: add(add(a, mul(d, t)), bend), U, V, ru: Math.max(0.0015, r * k), rv: Math.max(0.0015, r * k * ratio) });
    }
    this.loft(mat, rings, { n: o.n ?? 8, ...o });
  }
  prims(matIndex) { return [...this.groups].filter(([m, g]) => g.idx.length).map(([m, g]) => [g, matIndex[m]]); }
}
