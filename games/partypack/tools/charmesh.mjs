// Smooth skinned geometry for the characters: lofts (rings of superellipses along a path), ellipsoids and capsules, with
// smooth normals, metric UVs and up to four bone weights per vertex. Used by make-characters.mjs.
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const len = (a) => Math.hypot(a[0], a[1], a[2]);
const norm = (a) => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
const lerp = (a, b, t) => a + (b - a) * t;
const sp = (x, e) => Math.sign(x) * Math.pow(Math.abs(x), 2 / e);

export class SkinMesh {
  constructor() { this.groups = new Map(); }
  group(mat) {
    if (!this.groups.has(mat)) this.groups.set(mat, { pos: [], nrm: [], uv: [], idx: [], joints: [], weights: [] });
    return this.groups.get(mat);
  }
  /**
   * rings: [{c, U, V, ru, rv, e}] closed loops; opts: n (points per ring), tile (uv metres), weights(k, P) -> [[bone, w]...],
   * bone (single bone), uvFn(P, k, j, n) -> [u, v], flip
   */
  loft(mat, rings, o = {}) {
    const g = this.group(mat), n = o.n ?? 20, K = rings.length, tile = o.tile ?? 0.5;
    const base = g.pos.length / 3;
    const P = [], UV = [];
    let dist = 0;
    for (let k = 0; k < K; k++) {
      const r = rings[k];
      if (k > 0) dist += len(sub(r.c, rings[k - 1].c));
      const perim = 2 * Math.PI * Math.max(0.01, (r.ru + r.rv) / 2);
      for (let j = 0; j <= n; j++) {
        const a = (j / n) * Math.PI * 2, e = r.e ?? 2;
        const sx = sp(Math.cos(a), e), sz = sp(Math.sin(a), e);
        const p = add(r.c, add(mul(r.U, r.ru * sx), mul(r.V, r.rv * sz)));
        P.push(p);
        UV.push(o.uvFn ? o.uvFn(p, k, j, n) : [(j / n) * perim / tile, dist / tile]);
      }
    }
    const idx = (k, j) => k * (n + 1) + j;
    // outward test: the first ring with a real radius
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
    for (let k = 0; k < K; k++) { // close the seam
      const a = idx(k, 0), b = idx(k, n), s = add(N[a], N[b]);
      N[a] = s.slice(); N[b] = s.slice();
    }
    for (let v = 0; v < P.length; v++) {
      let nn = norm(N[v]); if (flip) nn = mul(nn, -1);
      if (len(N[v]) < 1e-12) { const k = Math.floor(v / (n + 1)); nn = norm(sub(P[v], rings[k].c)); }
      g.pos.push(...P[v]); g.nrm.push(...nn); g.uv.push(...UV[v]);
      const k = Math.floor(v / (n + 1));
      let w = o.weights ? o.weights(k, P[v]) : [[o.bone, 1]];
      w = w.filter((x) => x[1] > 1e-4).sort((x, y) => y[1] - x[1]).slice(0, 4);
      const sum = w.reduce((q, x) => q + x[1], 0);
      for (let i = 0; i < 4; i++) { g.joints.push(w[i] ? w[i][0] : 0); g.weights.push(w[i] ? w[i][1] / sum : 0); }
    }
    for (const [a, b, c] of tris) g.idx.push(base + a, ...(flip ? [base + c, base + b] : [base + b, base + c]));
  }
  /** an ellipsoid about c with radii (rx, ry, rz), rings stacked along y */
  ellipsoid(mat, c, rx, ry, rz, o = {}) {
    const steps = o.steps ?? 8, rings = [];
    for (let k = 0; k <= steps; k++) {
      const th = (k / steps) * Math.PI;
      rings.push({ c: [c[0], c[1] - ry * Math.cos(th), c[2]], U: [1, 0, 0], V: [0, 0, 1], ru: Math.max(0.002, rx * Math.sin(th)), rv: Math.max(0.002, rz * Math.sin(th)), e: o.e });
    }
    this.loft(mat, rings, { n: o.n ?? 14, ...o });
  }
  /** a capsule from a to b, radii rA and rB (ratio stretches the depth), rounded ends; weights(t, P) with t 0..1 along a to b */
  capsule(mat, a, b, rA, rB, o = {}) {
    const d = sub(b, a), L = len(d), dir = norm(d);
    const up = Math.abs(dir[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0];
    const U = norm(cross(dir, up)), V = norm(cross(dir, U));
    const hs = o.caps ?? 4, m = o.rings ?? 4, ratio = o.ratio ?? 1, rings = [], ts = [];
    for (let i = 0; i <= hs; i++) { const an = (Math.PI / 2) * (i / hs); rings.push({ c: sub(a, mul(dir, rA * Math.cos(an))), U, V, ru: Math.max(0.002, rA * Math.sin(an)), rv: Math.max(0.002, rA * Math.sin(an) * ratio), e: o.e }); ts.push(0); }
    for (let i = 1; i < m; i++) { const t = i / m; rings.push({ c: add(a, mul(d, t)), U, V, ru: lerp(rA, rB, t), rv: lerp(rA, rB, t) * ratio, e: o.e }); ts.push(t); }
    for (let i = 0; i <= hs; i++) { const an = (Math.PI / 2) * (i / hs); rings.push({ c: add(b, mul(dir, rB * Math.sin(an))), U, V, ru: Math.max(0.002, rB * Math.cos(an)), rv: Math.max(0.002, rB * Math.cos(an) * ratio), e: o.e }); ts.push(1); }
    const w = o.weights ? (k, P) => o.weights(ts[k], P) : undefined;
    this.loft(mat, rings, { n: o.n ?? 14, ...o, weights: w });
  }
  prims(matIndex) { return [...this.groups].map(([m, g]) => [g, matIndex[m]]); }
}
