// Geometry builder for textured static meshes: groups per material, world-scaled UVs, flat boxes, smooth cylinders.
// Used by make-world.mjs (stage scenery and props). Y up, facing +Z toward the camera.

export class Mesher {
  constructor() { this.groups = new Map(); }
  g(mat) {
    if (!this.groups.has(mat)) this.groups.set(mat, { pos: [], nrm: [], uv: [], idx: [] });
    return this.groups.get(mat);
  }
  /** a quad a,b,c,d (counter clockwise seen from the normal side) with a uv per corner */
  quad(mat, a, b, c, d, n, uva, uvb, uvc, uvd, vn) {
    const g = this.g(mat), base = g.pos.length / 3;
    let pts = [[a, uva, vn ? vn[0] : n], [b, uvb, vn ? vn[1] : n], [c, uvc, vn ? vn[2] : n], [d, uvd, vn ? vn[3] : n]];
    // wind the quad so its face normal agrees with the requested normal
    const e1 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], e2 = [d[0] - a[0], d[1] - a[1], d[2] - a[2]];
    const fn = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    const ref = vn ? [vn[0][0] + vn[1][0] + vn[2][0] + vn[3][0], vn[0][1] + vn[1][1] + vn[2][1] + vn[3][1], vn[0][2] + vn[1][2] + vn[2][2] + vn[3][2]] : n;
    if (fn[0] * ref[0] + fn[1] * ref[1] + fn[2] * ref[2] < 0) pts = [pts[0], pts[3], pts[2], pts[1]];
    for (const [p, t, nn] of pts) { g.pos.push(...p); g.nrm.push(...nn); g.uv.push(...t); }
    g.idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  tri(mat, a, b, c, n, ua, ub, uc) {
    const g = this.g(mat), base = g.pos.length / 3;
    const e1 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], e2 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    const fn = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    let pts = [[a, ua], [b, ub], [c, uc]];
    if (fn[0] * n[0] + fn[1] * n[1] + fn[2] * n[2] < 0) pts = [pts[0], pts[2], pts[1]];
    for (const [p, t] of pts) { g.pos.push(...p); g.nrm.push(...n); g.uv.push(...t); }
    g.idx.push(base, base + 1, base + 2);
  }
  /**
   * An axis aligned box. mats: one material, or {front, back, left, right, top, bottom, side}.
   * uv: metres per texture tile (number), or {s: tile, ox, oy} to offset; faces map world metres to uv so texel density is even.
   * facade: {tile, ox, oy} maps the front face as x/tile, -y/tile (a building face with windows).
   */
  box(mats, c, s, o = {}) {
    const m = typeof mats === 'string' ? { all: mats } : mats;
    const pick = (k) => m[k] ?? m.side ?? m.all;
    const [cx, cy, cz] = c, [hx, hy, hz] = [s[0] / 2, s[1] / 2, s[2] / 2];
    const t = o.uv ?? 2, ox = o.ox ?? 0, oy = o.oy ?? 0;
    const U = (a, b) => [a / t + ox, b / t + oy];
    const f = o.facade;
    // front +z
    if (f) {
      const F = (x, y) => [x / f.tile + (f.ox ?? 0), -y / f.tile + (f.oy ?? 0)];
      this.quad(pick('front'), [cx - hx, cy - hy, cz + hz], [cx + hx, cy - hy, cz + hz], [cx + hx, cy + hy, cz + hz], [cx - hx, cy + hy, cz + hz], [0, 0, 1],
        F(cx - hx, cy - hy), F(cx + hx, cy - hy), F(cx + hx, cy + hy), F(cx - hx, cy + hy));
    } else this.quad(pick('front'), [cx - hx, cy - hy, cz + hz], [cx + hx, cy - hy, cz + hz], [cx + hx, cy + hy, cz + hz], [cx - hx, cy + hy, cz + hz], [0, 0, 1], U(cx - hx, -(cy - hy)), U(cx + hx, -(cy - hy)), U(cx + hx, -(cy + hy)), U(cx - hx, -(cy + hy)));
    this.quad(pick('back'), [cx + hx, cy - hy, cz - hz], [cx - hx, cy - hy, cz - hz], [cx - hx, cy + hy, cz - hz], [cx + hx, cy + hy, cz - hz], [0, 0, -1], U(-(cx + hx), -(cy - hy)), U(-(cx - hx), -(cy - hy)), U(-(cx - hx), -(cy + hy)), U(-(cx + hx), -(cy + hy)));
    this.quad(pick('right'), [cx + hx, cy - hy, cz + hz], [cx + hx, cy - hy, cz - hz], [cx + hx, cy + hy, cz - hz], [cx + hx, cy + hy, cz + hz], [1, 0, 0], U(-(cz + hz), -(cy - hy)), U(-(cz - hz), -(cy - hy)), U(-(cz - hz), -(cy + hy)), U(-(cz + hz), -(cy + hy)));
    this.quad(pick('left'), [cx - hx, cy - hy, cz - hz], [cx - hx, cy - hy, cz + hz], [cx - hx, cy + hy, cz + hz], [cx - hx, cy + hy, cz - hz], [-1, 0, 0], U(cz - hz, -(cy - hy)), U(cz + hz, -(cy - hy)), U(cz + hz, -(cy + hy)), U(cz - hz, -(cy + hy)));
    this.quad(pick('top'), [cx - hx, cy + hy, cz + hz], [cx + hx, cy + hy, cz + hz], [cx + hx, cy + hy, cz - hz], [cx - hx, cy + hy, cz - hz], [0, 1, 0], U(cx - hx, cz + hz), U(cx + hx, cz + hz), U(cx + hx, cz - hz), U(cx - hx, cz - hz));
    this.quad(pick('bottom'), [cx - hx, cy - hy, cz - hz], [cx + hx, cy - hy, cz - hz], [cx + hx, cy - hy, cz + hz], [cx - hx, cy - hy, cz + hz], [0, -1, 0], U(cx - hx, cz - hz), U(cx + hx, cz - hz), U(cx + hx, cz + hz), U(cx - hx, cz + hz));
  }
  /** a vertical (or axis 'x'/'z') smooth cylinder with end caps; uv wraps once around, tiled by height */
  cyl(mat, c, r, h, o = {}) {
    const n = o.n ?? 20, axis = o.axis ?? 'y', t = o.uv ?? 2, r1 = o.r1 ?? r;
    const P = (a, y, rr) => { const ca = Math.cos(a), sa = Math.sin(a); return axis === 'y' ? [c[0] + ca * rr, c[1] + y, c[2] + sa * rr] : axis === 'x' ? [c[0] + y, c[1] + ca * rr, c[2] + sa * rr] : [c[0] + ca * rr, c[1] + sa * rr, c[2] + y]; };
    const N = (a) => { const ca = Math.cos(a), sa = Math.sin(a); return axis === 'y' ? [ca, 0, sa] : axis === 'x' ? [0, ca, sa] : [ca, sa, 0]; };
    const circ = Math.PI * 2 * r / t;
    for (let i = 0; i < n; i++) {
      const a0 = (i / n) * Math.PI * 2, a1 = ((i + 1) / n) * Math.PI * 2;
      const u0 = (i / n) * circ, u1 = ((i + 1) / n) * circ;
      const n0 = N(a0), n1 = N(a1);
      this.quad(mat, P(a0, -h / 2, r), P(a1, -h / 2, r), P(a1, h / 2, r1), P(a0, h / 2, r1), null,
        [u0, -h / 2 / t], [u1, -h / 2 / t], [u1, h / 2 / t], [u0, h / 2 / t], [n0, n1, n1, n0]);
      if (o.caps !== false) {
        const top = axis === 'y' ? [0, 1, 0] : axis === 'x' ? [1, 0, 0] : [0, 0, 1];
        const cen = (y) => axis === 'y' ? [c[0], c[1] + y, c[2]] : axis === 'x' ? [c[0] + y, c[1], c[2]] : [c[0], c[1], c[2] + y];
        const uvc = (a, rr) => [0.5 + Math.cos(a) * rr / t, 0.5 + Math.sin(a) * rr / t];
        this.tri(mat, cen(h / 2), P(a0, h / 2, r1), P(a1, h / 2, r1), top, [0.5, 0.5], uvc(a0, r1), uvc(a1, r1));
        this.tri(mat, cen(-h / 2), P(a1, -h / 2, r), P(a0, -h / 2, r), top.map((x) => -x), [0.5, 0.5], uvc(a1, r), uvc(a0, r));
      }
    }
  }
  /** a box sloped on its top (awnings, ramps): front lower than back by drop */
  /** output [[geometry, material], ...] for Glb.mesh. Triangles with zero area from the degenerate cap quads are harmless. */
  prims(matIndex) { return [...this.groups].map(([m, g]) => [g, matIndex[m]]); }
}
