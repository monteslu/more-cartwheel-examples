// A procedural face for a skinned character: a sculpted head surface (eye sockets, brow ridge, nose with alae and
// nostrils, lips with a real mouth opening, cheekbones, jaw, chin), eyeballs, eyelids that close, upper and lower teeth,
// a mouth interior with a tongue, modelled ears, and the expression bones that move them (jaw, four lids, two brows, two
// mouth corners, the upper lip). The head is a grid over (angle around the head, height) whose rows and columns are packed
// densely where the face is, so the face gets most of the triangles and most of the texels: the face texture is painted
// in that same grid space (paintFace), so the nose underside, the lid area and the lips each get their own pixels.
//
// Generic: it needs only a head envelope (ring(t) -> a horizontal superellipse ring, t -1 chin to +1 crown), its size,
// a parameter set for the face and a bone index map. Used by make-characters.mjs.
import { add, sub, mul, norm, cross, dot, len } from './charmesh.mjs';
import { Tex, hash2 } from './texlib.mjs';

const PI = Math.PI, FRONT = PI / 2;
const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
const lerp = (a, b, t) => a + (b - a) * t;
const ss = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
const G1 = (x, w) => Math.exp(-(x * x) / (w * w));
const G2 = (x, y, wx, wy) => Math.exp(-(x * x) / (wx * wx) - (y * y) / (wy * wy));
const box = (x, w, soft) => ss(w + soft, w - soft, Math.abs(x));
const spe = (x, e) => Math.sign(x) * Math.pow(Math.abs(x), 2 / e);
/** distance from p to the segment a-b in 2D, and the position along it (0..1) */
const seg = (px, py, ax, ay, bx, by) => {
  const vx = bx - ax, vy = by - ay, t = clamp(((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy));
  return [Math.hypot(px - ax - vx * t, py - ay - vy * t), t];
};
/** value noise without tiling (2D), and a few octaves of it */
const vn = (x, y, s) => {
  const xi = Math.floor(x), yi = Math.floor(y), fx = x - xi, fy = y - yi, u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
  return lerp(lerp(hash2(xi, yi, s), hash2(xi + 1, yi, s), u), lerp(hash2(xi, yi + 1, s), hash2(xi + 1, yi + 1, s), u), v);
};
const fbn = (x, y, oct, s) => { let a = 0.5, t = 0, n = 0; for (let o = 0; o < oct; o++) { t += a * vn(x, y, s + o * 31); n += a; x *= 2.03; y *= 2.03; a *= 0.5; } return t / n; };

/** an inverse-CDF warp of [x0, x1] by a density: fwd(u 0..1) -> x, inv(x) -> u */
export function warp(x0, x1, dens, N = 4096) {
  const cdf = new Float64Array(N + 1), dx = (x1 - x0) / N;
  for (let i = 1; i <= N; i++) cdf[i] = cdf[i - 1] + 0.5 * (dens(x0 + (i - 1) * dx) + dens(x0 + i * dx)) * dx;
  for (let i = 1; i <= N; i++) cdf[i] /= cdf[N];
  return {
    fwd(u) {
      u = clamp(u); let lo = 0, hi = N;
      while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cdf[m] < u) lo = m; else hi = m; }
      const f = (u - cdf[lo]) / Math.max(1e-12, cdf[hi] - cdf[lo]);
      return x0 + (lo + f) * dx;
    },
    inv(x) { const p = clamp((x - x0) / (x1 - x0)) * N, i = Math.min(N - 1, Math.floor(p)); return lerp(cdf[i], cdf[i + 1], p - i); },
  };
}

/** the face parameters with their defaults; every number is in head units (R across, HY up) */
export function faceParams(f = {}) {
  const o = {
    eyeX: 0.375, eyeY: 0.03, eyeR: 0.175, tilt: 0.05, lidUp: 0.3, lidLow: -0.42, prot: 0.42, socket: 1, mono: 0, fold: 1,
    brow: 1, browY: 0.2, browArch: 0.04, browThick: 0.065, browTilt: 0, browGap: 0.1, browLen: 0.62, browSparse: 0,
    noseLen: 1, noseW: 1, noseProj: 1, hook: 0, crook: 0, tipUp: 0, nostril: 1,
    mw: 0.25, tm: -0.58, lipU: 0.78, lipL: 0.76, muzzle: 0.8, relax: 1,
    cheek: 1, hollow: 0, jawAng: 1, chin: 1, chinW: 1, cleft: 0, jowl: 0, bags: 0, temple: 1, naso: 1, age: 0,
    ear: 1, earOut: 1, cauli: 0,
    ...f,
  };
  // the character sheets give a narrower face's numbers: a real eye is a sixth of the head's width and shows about a third of its
  // height, and a mouth reaches the pupils' inner edge, so they all open up from the sheet's values
  o.mw *= 1.22; o.prot += 0.12; o.lidUp += 0.07; o.lidLow -= 0.04; o.socket *= 1.15;
  return o;
}

// ── the sculpt: a radial displacement (in R) at a front-of-head position (xn across, yn = t up) ──
function noseRows(F) {
  const yR = 0.08, yT = -0.3 * F.noseLen - 0.03 + 0.02 * F.tipUp, yB = yT - 0.085 * F.noseLen;
  return { yR, yT, yB };
}
export function disp(xn, yn, F) {
  const ax = Math.abs(xn);
  let d = 0;
  // forehead: two soft frontal bosses, temple hollows
  d += 0.018 * G2(ax - 0.3, yn - 0.42, 0.28, 0.2) - 0.035 * F.temple * G2(ax - 0.8, yn - 0.18, 0.13, 0.22);
  // brow ridge over each eye (its heaviest over the inner half), the glabella between them
  const by = F.browY - 0.04 * (ax - 0.3);
  d += 0.038 * F.brow * G2(ax - 0.3, yn - by + 0.02, 0.27, 0.06) + 0.022 * F.brow * G2(xn, yn - (F.browY - 0.03), 0.13, 0.07);
  // the eye sockets: a round hollow under the ridge, deepest at the inner corner; bags under them
  d -= F.socket * 0.1 * G2(ax - F.eyeX + 0.05, yn - F.eyeY + 0.01, 0.2, 0.12);
  d += F.bags * 0.03 * G2(ax - F.eyeX, yn - (F.eyeY - 0.15), 0.14, 0.04);
  // cheekbones, the hollow under them
  d += 0.05 * F.cheek * G2(ax - 0.62, yn + 0.14, 0.2, 0.11);
  d -= 0.04 * F.hollow * G2(ax - 0.55, yn + 0.44, 0.17, 0.13);
  // the nose: nasion, bridge, tip, columella; alae; nostrils
  const { yR, yT, yB } = noseRows(F), nw = F.noseW, np = F.noseProj;
  let h = 0, w = 0.075 * nw;
  if (yn <= yR + 0.12 && yn >= yB - 0.05) {
    if (yn > yR) { h = 0.075 * ss(yR + 0.12, yR, yn); }
    else if (yn > yT) { const u = (yR - yn) / (yR - yT); h = lerp(0.07, 0.235, Math.pow(u, 1.05)) + 0.05 * F.hook * G1(u - 0.42, 0.18) + 0.035 * G1(u - 1, 0.16); w = lerp(0.07, 0.1, u * u) * nw; }
    else if (yn > yB) { const u = (yT - yn) / (yT - yB); h = lerp(0.27, 0.05, Math.pow(u, 1.5)); w = lerp(0.1, 0.11, u) * nw; }
    else { h = 0.05 * ss(yB - 0.05, yB, yn); w = 0.11 * nw; }
  }
  const cx = F.crook * 0.05 * ss(yR, yT, yn);
  d += h * np * G1(xn - cx, w);
  d += 0.058 * nw * G2(ax - 0.145 * nw, yn - (yB + 0.045), 0.07, 0.06) - 0.016 * nw * G2(ax - 0.235 * nw, yn - (yB + 0.05), 0.025, 0.055); // alae, with the crease where they meet the cheek
  d -= 0.06 * F.nostril * G2(ax - 0.065 * nw, yn - (yB + 0.012), 0.035, 0.022); // nostrils
  // nasolabial fold: a crease from the alae to past the mouth corners, a fuller cheek outside it
  const [nd, nt] = seg(ax, yn, 0.2 * nw, yB + 0.02, F.mw + 0.07, F.tm - 0.06);
  d += F.naso * (-0.014 * G1(nd, 0.025) + 0.016 * G1(nd - 0.05, 0.04)) * ss(0, 0.15, nt) * (ax > 0.15 ? 1 : 0.4);
  // the mouth: the dental arch carries the mouth forward, the lips, philtrum, corners, the groove under the lower lip
  const tm = F.tm, mw = F.mw;
  d += 0.055 * F.muzzle * G2(xn, yn - (tm + 0.03), 0.42, 0.22);
  d += 0.045 * F.lipU * box(xn, mw * 0.8, mw * 0.25) * G1(yn - (tm + 0.042 * F.lipU), 0.03);
  d += 0.042 * F.lipL * box(xn, mw * 0.7, mw * 0.3) * G1(yn - (tm - 0.055 * F.lipL), 0.04);
  d -= 0.016 * G2(xn, yn - (yB - 0.05), 0.03, 0.045);
  d += 0.006 * G2(ax - 0.04, yn - (yB - 0.05), 0.02, 0.045);
  d -= 0.022 * G2(ax - mw * 1.04, yn - tm, 0.04, 0.03);
  d -= 0.026 * box(xn, 0.3, 0.1) * G1(yn - (tm - 0.155), 0.04);
  // chin, the jaw's angle, jowls
  d += 0.06 * F.chin * G2(xn, yn + 0.85, 0.27 * F.chinW, 0.1);
  d -= 0.018 * F.cleft * G2(xn, yn + 0.87, 0.028, 0.06);
  d += 0.028 * F.jawAng * G2(ax - 0.74, yn + 0.52, 0.12, 0.15);
  d += 0.035 * F.jowl * G2(ax - 0.56, yn - (tm - 0.12), 0.15, 0.1);
  return d;
}

/**
 * The head: geometry, the lookup used by the painter and the bone positions.
 *   H: { hc, R, RZ, HY, ring(t) } (ring returns {c, U, V, ru, rv, e, mod})
 *   F: faceParams(...)
 */
export function headModel(H, F) {
  const { hc, R, RZ, HY, ring } = H;
  const Aw = warp(-PI, PI, (a) => 0.4 + 5.2 * G1(a, 0.68)); // the columns and rows go where the face is
  const Tw = warp(-1, 1, (t) => 1.6 + 2.2 * ss(-1.0, -0.9, t) * ss(0.42, 0.26, t));
  const base = (aRel, t) => {
    const r = ring(t), a = FRONT + aRel, m = r.mod ? r.mod(a) : 1;
    const P = add(r.c, add(mul(r.U, r.ru * spe(Math.cos(a), r.e ?? 2) * m), mul(r.V, r.rv * spe(Math.sin(a), r.e ?? 2) * m)));
    return { P, r, a };
  };
  const point = (aRel, t, seam = false) => {
    const { P, r, a } = base(aRel, t);
    const xn = P[0] / R, yn = t, fm = ss(-0.1, 0.5, Math.sin(a));
    let d = disp(xn, yn, F) * fm;
    if (seam) d -= 0.035 * box(xn, F.mw, 0.05) * fm; // a deeper line between the lips
    const nh = [P[0] - r.c[0], 0, P[2] - r.c[2]], l = len(nh);
    // two small vertical moves the radial sculpt cannot make (each smooth enough that rows never cross): the mouth line
    // curves up into the corners (a relaxed mouth, not a sulk), and the rows under the nose tip tuck up (a deeper underside)
    const ax = Math.abs(xn), { yB } = noseRows(F);
    const dy = F.relax * 0.022 * ss(0.35 * F.mw, F.mw, ax) * G1(yn - F.tm, 0.09) * G1(ax - F.mw, 0.2) + 0.035 * G1(xn, 0.14) * G1(yn - (yB + 0.015), 0.05);
    const Q = l > 1e-6 ? add(P, mul(nh, (d * R) / l)) : P.slice();
    Q[1] += dy * HY * fm;
    return { P: Q, xn, yn, front: Math.sin(a) };
  };
  /** the column angle where the front of the head is at xn (0 is the middle; +x is the character's left) */
  const aAt = (xn, t) => { let lo = -PI / 2, hi = PI / 2; for (let i = 0; i < 40; i++) { const m = (lo + hi) / 2; if (base(m, t).P[0] > xn * R) lo = m; else hi = m; } return (lo + hi) / 2; };
  const surf = (xn, yn) => point(aAt(xn, yn), yn).P;
  /** texture coordinates of a point near the head (for attached parts: lids, ears) */
  const projUV = (P) => {
    const t = clamp((P[1] - hc[1]) / HY, -1, 1), r = ring(t), a = Math.atan2((P[2] - r.c[2]) / r.rv, P[0] / r.ru);
    let aRel = a - FRONT; if (aRel < -PI) aRel += 2 * PI; if (aRel > PI) aRel -= 2 * PI;
    return [Aw.inv(aRel), 1 - Tw.inv(t)];
  };
  // landmarks
  const re = F.eyeR * R;
  const eye = (sx) => {
    const S = surf(sx * F.eyeX, F.eyeY);
    const Fe = [0, 0, 1], th = F.tilt, Xe = [Math.cos(th), sx * Math.sin(th), 0], Ye = cross(Fe, Xe);
    return { c: sub(S, mul(Fe, re * (1 - F.prot))), Xe, Ye, Fe };
  };
  const E = { L: eye(1), R: eye(-1) };
  const Sm = surf(0, F.tm);
  const bones = {
    Jaw: [0, hc[1] - 0.24 * HY, hc[2] - 0.08 * RZ],
    LidUL: E.L.c, LidLL: E.L.c, LidUR: E.R.c, LidLR: E.R.c,
    BrowL: surf(0.36, F.browY), BrowR: surf(-0.36, F.browY),
    MouthL: surf(F.mw, F.tm), MouthR: surf(-F.mw, F.tm), LipU: surf(0, F.tm + 0.06),
  };
  return { H, F, Aw, Tw, base, point, surf, projUV, E, re, Sm, bones };
}

/** skin weights of a head-surface vertex: the head bone plus the expression bones that move it */
function headWeights(v, F, BI, seam) {
  const { xn, yn, front } = v, ax = Math.abs(xn), sd = xn >= 0 ? 'L' : 'R', fm = ss(-0.15, 0.45, front);
  const tm = F.tm, mw = F.mw, w = [];
  // the jaw: everything below the mouth line on the front, fading out toward the ears and up the cheeks
  let j;
  if (seam === 'lower') j = lerp(1, 0.5, ss(mw * 0.55, mw * 1.0, ax));
  else if (seam === 'upper') j = 0.5 * ss(mw * 0.55, mw * 1.0, ax);
  else if (yn < tm) j = lerp(1, 0.5, ss(mw * 0.55, mw * 1.0, ax) * ss(tm - 0.12, tm, yn));
  else j = 0.5 * ss(mw * 0.55, mw * 1.0, ax) * ss(tm + 0.2, tm, yn);
  j *= ss(-0.45, 0.3, front) * ss(1.05, 0.7, ax * (front > 0 ? 1 : 0) + (front > 0 ? 0 : 1)) * ss(-0.05, -0.25, yn);
  if (yn < tm && front > -0.2) j = Math.max(j, ss(-0.2, 0.25, front) * ss(tm - 0.05, tm - 0.25, yn) * ss(1.0, 0.75, ax));
  if (j > 0) w.push([BI.Jaw, j]);
  // mouth corners, the upper lip, the brows
  const mc = 0.85 * G2(ax - mw, yn - tm, 0.13, 0.09) * fm;
  if (mc > 0.01) w.push([BI['Mouth' + sd], mc * (1 - j * 0.5)]);
  // the upper lip: its row of the mouth line takes it fully in the mouth, both rows share it past the corners (no crack)
  const side = ss(mw * 0.6, mw * 1.0, ax), lu0 = 0.8 * G2(xn, yn - (tm + 0.06), 0.24, 0.07) * fm;
  const lu = seam === 'upper' ? lu0 * (1 - 0.5 * side) : seam === 'lower' ? lu0 * 0.5 * side : yn < tm ? lu0 * 0.5 * side * ss(tm - 0.08, tm, yn) : lu0;
  if (lu > 0.01) w.push([BI.LipU, lu]);
  const bw = 0.9 * G2(ax - 0.36, yn - F.browY, 0.22, 0.085) * fm * (yn > F.eyeY + 0.07 ? 1 : ss(F.eyeY + 0.02, F.eyeY + 0.07, yn));
  if (bw > 0.01) w.push([BI['Brow' + sd], bw]);
  const tot = w.reduce((s, x) => s + x[1], 0);
  if (tot > 1) for (const x of w) x[1] /= tot;
  w.push([BI.Head, Math.max(0, 1 - Math.min(1, tot))]);
  return w;
}

/** push a grid of vertices (rows x cols, rows bottom to top) into a SkinMesh group; skip(k, j) drops a quad */
function emitGrid(m, mat, V, NC, skip, orient, open = false, weld = null, alias = null) {
  const g = m.group(mat), base = g.pos.length / 3, rows = V.length, cols = NC + 1;
  const tris = [];
  for (let k = 0; k < rows - 1; k++) for (let j = 0; j < NC; j++) {
    if (skip && skip(k, j)) continue;
    const id = (kk, jj) => { const r = alias && alias(kk, jj); return r ? r[0] * cols + r[1] : kk * cols + jj; };
    const a = id(k, j), b = id(k, j + 1), c = id(k + 1, j + 1), d = id(k + 1, j);
    tris.push(orient ? [a, b, c] : [a, c, b], orient ? [a, c, d] : [a, d, c]);
  }
  const P = V.flat().map((v) => v.P), N = P.map(() => [0, 0, 0]);
  for (const [a, b, c] of tris) { const fn = cross(sub(P[b], P[a]), sub(P[c], P[a])); for (const x of [a, b, c]) { N[x][0] += fn[0]; N[x][1] += fn[1]; N[x][2] += fn[2]; } }
  // a doubled row (the mouth line) shares its normals wherever its two copies are joined
  if (weld) for (let j = 0; j <= NC; j++) { if (skip(weld, Math.min(j, NC - 1)) && (j === 0 || skip(weld, j - 1))) continue; const a = weld * cols + j, b = a + cols, s2 = add(N[a], N[b]); N[a] = s2.slice(); N[b] = s2.slice(); }
  if (!open) for (let k = 0; k < rows; k++) { const a = k * cols, b = a + NC, s = add(N[a], N[b]); N[a] = s.slice(); N[b] = s.slice(); }
  const all = V.flat();
  all.forEach((v, i) => {
    g.pos.push(...v.P); g.nrm.push(...norm(len(N[i]) > 1e-14 ? N[i] : sub(v.P, v.c ?? [0, 0, 0]))); g.uv.push(...v.uv); g.tag.push(v.tag ?? null);
    let w = v.w.filter((x) => x[1] > 1e-4).sort((x, y) => y[1] - x[1]).slice(0, 4);
    const s = w.reduce((q, x) => q + x[1], 0) || 1;
    for (let i2 = 0; i2 < 4; i2++) { g.joints.push(w[i2] ? w[i2][0] : 0); g.weights.push(w[i2] ? w[i2][1] / s : 0); }
  });
  for (const [a, b, c] of tris) g.idx.push(base + a, base + b, base + c);
}

/** a tube along a polyline (radius per point), through SkinMesh.loft */
function tube(m, mat, pts, rad, o = {}) {
  const rings = pts.map((p, i) => {
    const t = norm(sub(pts[Math.min(pts.length - 1, i + 1)], pts[Math.max(0, i - 1)]));
    const up = Math.abs(t[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0], U = norm(cross(t, up)), V = norm(cross(t, U));
    return { c: p, U, V, ru: rad[i] * (o.ratio ?? 1), rv: rad[i] };
  });
  m.loft(mat, rings, { n: o.n ?? 8, ...o });
}

/**
 * Build the head into SkinMesh m: the sculpted surface ('face'), eyes ('eye'), lids ('face' + 'lash'), teeth ('teeth'),
 * the mouth interior ('mouth'), ears ('face'); spec extras: piercings, cauliflower ears.
 */
export function buildHead(m, M, BI, o = {}) {
  const { H, F, Aw, Tw, point, projUV, E, re, Sm } = M;
  const { hc, R, HY } = H;
  const NC = o.cols ?? 84, NR = o.rows ?? 66;
  // rows (bottom to top) with the mouth line snapped onto one row and doubled
  const ts = []; for (let i = 0; i <= NR; i++) ts.push(Tw.fwd(i / NR));
  let km = 0; for (let i = 1; i < NR; i++) if (Math.abs(ts[i] - F.tm) < Math.abs(ts[km] - F.tm)) km = i;
  ts[km] = F.tm;
  const rows = ts.map((t, i) => ({ t, seam: i === km ? 'lower' : null }));
  rows.splice(km + 1, 0, { t: F.tm, seam: 'upper' });
  const V = rows.map((row) => {
    const out = [];
    for (let j = 0; j <= NC; j++) {
      const u = j / NC, aRel = Aw.fwd(u);
      const v = point(aRel, row.t, !!row.seam);
      out.push({ P: v.P, c: hc, uv: [u, 1 - Tw.inv(row.t)], w: headWeights(v, F, BI, row.seam), xn: v.xn, front: v.front, tag: { g: 1, xn: v.xn, yn: row.t, front: v.front, seam: row.seam } });
    }
    return out;
  });
  // orientation: the front quad's normal must point forward
  const kf = Math.floor(rows.length / 2), jf = Math.round(NC / 2);
  const a = V[kf][jf].P, b = V[kf][jf + 1].P, c = V[kf + 1][jf + 1].P, orient = cross(sub(b, a), sub(c, a))[2] > 0;
  // the mouth line is one row of vertices except inside the mouth, where the upper lip's copy separates from the lower's
  const gap = (j) => j >= 0 && j < NC && Math.abs(V[km][j].xn + V[km][j + 1].xn) / 2 < F.mw && V[km][j].front > 0;
  emitGrid(m, 'face', V, NC, (k, j) => rows[k].seam === 'lower' && rows[k + 1].seam === 'upper', orient, false, null,
    (k, j) => (rows[k].seam === 'upper' && !(gap(j - 1) && gap(j)) ? [k - 1, j] : null));

  // eyes: a ball each, the lids are shells around it hinged on the eye's side to side axis
  for (const sd of ['L', 'R']) {
    const e = E[sd];
    m.ellipsoid(o.glowEyes ? 'glow' : 'eye', e.c, re, re, re, { axis: e.Fe, U: e.Xe, V: e.Ye, steps: 10, n: 18, bone: BI.Head,
      uvFn: (p) => { const q = sub(p, e.c); return [0.5 + dot(q, e.Xe) / (2.1 * re), 0.5 - dot(q, e.Ye) / (2.1 * re)]; } });
    const lid = (e0, dir, list, bone, tg) => { // rings at elevation e0 + dir*de (radius in re), as arcs over the eye's front
      const ring = ([de, r]) => { const el = e0 + dir * de; return { c: e.c, U: e.Xe, V: add(mul(e.Fe, Math.cos(el)), mul(e.Ye, Math.sin(el))), ru: r * re, rv: r * re }; };
      const uv = (p) => projUV(p);
      m.loft('lash', list.slice(0, 2).map(ring), { n: 18, arc: [0.02, PI - 0.02], bone, uvFn: uv, tag: 'lash' + tg });
      m.loft('face', list.slice(1).map(ring), { n: 18, arc: [0.02, PI - 0.02], bone, uvFn: uv, tag: 'lid' + tg });
    };
    lid(F.lidUp, 1, [[0, 1.0], [0.06, 1.1], [0.16, 1.14], [0.32, 1.15], [0.62, 1.15], [1.0, 1.12], [1.45, 1.06]], BI['LidU' + sd], 'U' + sd);
    lid(F.lidLow, -1, [[0, 1.0], [0.04, 1.07], [0.14, 1.1], [0.32, 1.1], [0.62, 1.08], [1.0, 1.04]], BI['LidL' + sd], 'L' + sd);
  }

  // teeth: upper on the head, lower on the jaw; the mouth interior and the tongue
  const zf = Sm[2] - 0.09 * R; // behind the closed lips
  for (const [row, bone, dy, hh] of [[0, BI.Head, 1, 0.1], [1, BI.Jaw, -1, 0.085]]) {
    const n = 8;
    for (let i = 0; i < n; i++) {
      const x = (-1 + (2 * (i + 0.5)) / n) * 0.27 * R, k = Math.abs(x) / (0.27 * R);
      const wdt = (0.062 - 0.012 * k) * R, h = hh * R * (1 - 0.2 * k), z = zf - 0.13 * R * k * k - row * 0.012 * R;
      m.ellipsoid('teeth', [x, Sm[1] + dy * (h * 0.5 - 0.012 * R), z], wdt * 0.5, h * 0.5, 0.032 * R, { bone, steps: 3, n: 8, e: 3.2 });
    }
  }
  const mc = [0, Sm[1] - 0.02 * R, Sm[2] - 0.2 * R];
  m.ellipsoid('mouth', mc, 0.3 * R, 0.24 * R, 0.2 * R, { steps: 6, n: 14, flip: true, weights: (k, P) => { const t = ss(Sm[1] + 0.03 * R, Sm[1] - 0.05 * R, P[1]); return [[BI.Head, 1 - t], [BI.Jaw, t]]; } });
  m.ellipsoid('mouth', [0, Sm[1] - 0.085 * R, Sm[2] - 0.15 * R], 0.17 * R, 0.05 * R, 0.15 * R, { steps: 5, n: 12, bone: BI.Jaw });

  // ears: a cupped plate with a rim (helix) and a lobe, tilted back and standing off the head
  for (const sx of [1, -1]) {
    const t0 = -0.1, aE = sx > 0 ? -PI / 2 - 0.12 : PI / 2 + 0.12;
    const S = point(aE, t0).P, out = [sx, 0, 0], tb = 0.22, up = [0, Math.cos(tb), -Math.sin(tb)], back = [0, -Math.sin(tb), -Math.cos(tb)];
    const ang = 0.32 * F.earOut, O = norm(add(mul(out, Math.cos(ang)), mul(back, -Math.sin(ang) * 0.4))), Bk = norm(cross(O, up)), Bb = dot(Bk, back) < 0 ? mul(Bk, -1) : Bk;
    const sz = F.ear * (1 + 0.15 * F.cauli), ry = 0.29 * HY * sz, rz = 0.19 * R * sz;
    const cen = add(add(S, mul(O, 0.02 * R)), mul(Bb, 0.07 * R));
    const lumpy = (a) => 1 + 0.12 * F.cauli * Math.sin(a * 5 + sx) * Math.sin(a * 3);
    m.ellipsoid('face', cen, 0.07 * R * (1 + F.cauli), ry, rz, { axis: up, U: O, V: Bb, steps: 7, n: 14, bone: BI.Head, mod: lumpy, uvFn: projUV });
    const rim = [], rr = [];
    for (let i = 0; i <= 10; i++) {
      const ph = lerp(-0.35, 3.75, i / 10); // from the front top, over the top, down the back
      rim.push(add(add(add(cen, mul(up, Math.cos(ph) * ry * 0.93)), mul(Bb, Math.sin(ph) * rz * 0.93)), mul(O, 0.05 * R + 0.012 * R * F.cauli)));
      rr.push((0.03 + 0.015 * F.cauli) * R * (i < 2 ? 0.6 + 0.2 * i : 1));
    }
    tube(m, 'face', rim, rr, { bone: BI.Head, n: 8, uvFn: projUV });
    m.ellipsoid('face', add(add(cen, mul(up, -ry * 0.92)), mul(Bb, 0.01 * R)), 0.04 * R, 0.07 * R * sz, 0.06 * R * sz, { axis: up, U: O, V: Bb, steps: 5, n: 10, bone: BI.Head, uvFn: projUV });
    m.ellipsoid('face', add(add(cen, mul(up, -0.05 * HY)), mul(Bb, -rz * 0.85)), 0.035 * R, 0.05 * R, 0.035 * R, { axis: up, U: O, V: Bb, steps: 4, n: 8, bone: BI.Head, uvFn: projUV }); // tragus
  }
  if (o.piercings) for (const [xn, yn, r] of o.piercings) {
    const P = M.surf(xn, yn); m.ellipsoid('buckle', add(P, [0, 0, 0.004]), r * R, r * R, r * R, { bone: BI.Head, steps: 4, n: 8 });
  }
  if (o.lipRing) { // a ring through the lower lip, off centre
    const P = M.surf(0.12, F.tm - 0.05), pts = [], rad = [];
    for (let i = 0; i <= 12; i++) { const a = (i / 12) * PI * 2; pts.push(add(P, [0, -0.035 * R + Math.cos(a) * 0.035 * R, 0.01 * R + Math.sin(a) * 0.035 * R])); rad.push(0.008 * R); }
    tube(m, 'buckle', pts, rad, { n: 6, weights: () => [[BI.Jaw, 1]] });
  }
}

/**
 * A shell over the front of the face that follows its bumps but not its hollows (a bandana, a beard): rows t0..t1, columns
 * over +-aHalf around the front, `off` metres out. drape: below the nose it hangs from the nose tip rather than tucking in.
 * o.jaw / o.head: bone indexes; the part below the mouth line rides the jaw.
 */
export function shellGrid(m, mat, M, t0, t1, aHalf, off, o = {}) {
  const { H, F, base } = M, { R, hc } = H, NC = o.cols ?? 36, NR = o.rows ?? 14;
  const V = [], hang = new Float64Array(NC + 1);
  for (let k = NR; k >= 0; k--) { // top to bottom, so a drape can carry the nose down
    const t = lerp(t0, t1, k / NR), row = [];
    for (let j = 0; j <= NC; j++) {
      const aRel = lerp(-aHalf, aHalf, j / NC), { P, r, a } = base(aRel, t), xn = P[0] / R, fm = ss(-0.1, 0.5, Math.sin(a));
      let d = Math.max(0, disp(xn, t, F)) * fm;
      if (o.drape) { hang[j] = Math.max(d, hang[j] * (1 - 0.6 * (1 / NR))); d = Math.max(d, hang[j]); }
      const nh = [P[0] - r.c[0], 0, P[2] - r.c[2]], l = len(nh) || 1;
      const fold = (o.folds ?? 0) * Math.sin(aRel * 9 + t * 4) * ss(t1, t0, t);
      const Q = add(P, mul(nh, (d * R + off + fold * R + (o.rough ?? 0) * R * (vn(aRel * 9, t * 9, 5) - 0.5)) / l));
      const jw = ss(F.tm + 0.05, F.tm - 0.12, t) * ss(1.1, 0.7, Math.abs(aRel));
      row.push({ P: Q, c: hc, uv: [(j / NC) * 2, (1 - k / NR) * 0.8], w: [[o.jaw, jw], [o.head, 1 - jw]], xn, front: Math.sin(a) });
    }
    V.unshift(row);
  }
  // the grid is an open patch: no seam averaging, outward winding
  const a = V[1][Math.floor(NC / 2)].P, b = V[1][Math.floor(NC / 2) + 1].P, c = V[2][Math.floor(NC / 2) + 1].P;
  emitGrid(m, mat, V, NC, null, cross(sub(b, a), sub(c, a))[2] > 0, true);
}

/**
 * A cap of hair that grows out of the scalp: from a hairline edge(a) (the ring height at loft angle a) up to the crown. It follows the
 * sculpted skin, starts under it and rises through it over a band, so the line where hair meets skin is that curve and has no raised edge;
 * the face texture paints a band of fine strands under it (paintFace hairEdge). o: lift (metres over the skin), rise (the band, in t),
 * rough (uneven hair), rows, cols, weights.
 */
export function hairCap(m, mat, M, edge, o = {}) {
  const { H, base, point } = M, { R, hc } = H, NC = o.cols ?? 36, NR = o.rows ?? 12, lift = o.lift ?? 0.005, rise = o.rise ?? 0.14, Vv = [];
  const Ac = warp(-PI, PI, () => 1);
  for (let k = 0; k <= NR; k++) {
    const row = [], v = k / NR;
    for (let j = 0; j <= NC; j++) {
      const aRel = lerp(-PI, PI, j / NC), { a } = base(aRel, 0), e = edge(((a % (2 * PI)) + 2 * PI) % (2 * PI)), t = Math.min(0.998, lerp(e - 0.03, 1, Math.pow(v, 1.25)));
      const q = point(aRel, t), b2 = base(aRel, t), nh = sub(q.P, [b2.r.c[0], b2.r.c[1] - 0.18 * H.HY, b2.r.c[2]]), n = norm(nh);
      const up = ss(e - 0.015, e + rise, t), off = -0.0025 + (lift + 0.0025) * up + (o.rough ?? 0) * R * (vn(aRel * 7, t * 7, 3) - 0.5) * up;
      row.push({ P: add(q.P, mul(n, off)), c: hc, uv: [(aRel * 0.09) / 0.25, (t * 0.12) / 0.25], w: o.weights ?? [[o.bone, 1]], xn: 0, front: 0 });
    }
    Vv.push(row);
  }
  const jm = Math.floor(NC / 2), a = Vv[2][jm].P, b = Vv[2][jm + 1].P, c = Vv[3][jm + 1].P;
  emitGrid(m, mat, Vv, NC, null, cross(sub(b, a), sub(c, a))[1] + cross(sub(b, a), sub(c, a))[2] > 0);
}

// ── the painter: albedo, height (for the normal map), roughness, occlusion in the head grid's own texture space ──
/**
 * P: { skin, hair, brow, eyes, lip, makeup: {shadow, liner, rouge, lipstick}, stubble, beardZone, freckles, age,
 *      scars: [[x0,y0,x1,y1,width]], tattoos: [{kind, x, y, s, color}], war, mole: [x,y], seed, scalp }
 */
export function paintFace(M, P, N = 512) {
  const { H, F, Aw, Tw, base } = M;
  const { R, HY } = H;
  const t = new Tex(N, N), seed = P.seed ?? 1;
  const skin = P.skin, brow = P.brow ?? P.hair.map((c) => c * 0.6), age = P.age ?? 0;
  const { yR, yT, yB } = noseRows(F);
  const tm = F.tm, mw = F.mw;
  const mix = (c, k, a) => { c[0] = lerp(c[0], k[0], a); c[1] = lerp(c[1], k[1], a); c[2] = lerp(c[2], k[2], a); };
  const mulc = (c, k) => { c[0] *= k; c[1] *= k; c[2] *= k; };
  for (let y = 0; y < N; y++) {
    const tt = Tw.fwd(1 - (y + 0.5) / N);
    for (let x = 0; x < N; x++) {
      const i = y * N + x, aRel = Aw.fwd((x + 0.5) / N), b = base(aRel, tt);
      const xn = b.P[0] / R, yn = tt, ax = Math.abs(xn), sxn = xn >= 0 ? 1 : -1, front = Math.sin(b.a), fm = ss(-0.1, 0.45, front);
      // metric surface coordinates for noise (metres): around and up
      const mu = aRel * R * 1.1, mv = tt * HY;
      const c = skin.slice();
      // tone: low frequency blotches, warm cheeks, nose and ears, a cooler jaw, a yellower forehead
      const n1 = fbn(mu * 18, mv * 18, 4, seed), n2 = fbn(mu * 70, mv * 70, 3, seed + 7);
      mulc(c, 0.985 + 0.12 * (n1 - 0.5) + 0.04 * (n2 - 0.5));
      const red = fm * (0.55 * G2(ax - 0.5, yn + 0.18, 0.2, 0.16) + 0.6 * G2(xn, yn - (yT - 0.02), 0.11, 0.08) + 0.3 * G2(xn, yn + 0.85, 0.2, 0.1) + 0.25 * G2(ax - 0.36, yn - F.eyeY, 0.18, 0.1))
        + 0.55 * G2(Math.abs(aRel) - PI / 2 - 0.12, yn + 0.1, 0.25, 0.3) + (P.ruddy ?? 0) * fm * G2(xn, yn + 0.15, 0.6, 0.35);
      mix(c, [skin[0] * 1.03, skin[1] * 0.82, skin[2] * 0.8], clamp(red) * 0.3);
      mix(c, [skin[0] * 1.02, skin[1] * 0.98, skin[2] * 0.82], fm * G2(xn, yn - 0.4, 0.5, 0.2) * 0.25);
      let h = 0, ro = 0.55 - 0.08 * fm * (G2(xn, yn - 0.38, 0.4, 0.18) + G2(xn, yn - (yT + 0.05), 0.12, 0.2)), ao = 1;
      // pores and fine skin
      const pz = hash2(Math.floor(mu * 2600), Math.floor(mv * 2600), seed + 3), pore = pz > 0.86 ? (pz - 0.86) / 0.14 : 0;
      h += -0.14 * pore * (0.5 + fm) + 0.07 * (n2 - 0.5);
      ro += 0.08 * pore;
      // under-eye darkness (more with age), the lid crease
      const ue = fm * G2(ax - F.eyeX, yn - (F.eyeY - 0.12), 0.15, 0.05);
      mix(c, [skin[0] * 0.78, skin[1] * 0.66, skin[2] * 0.72], ue * (0.18 + 0.35 * age));
      if (!F.mono) { const cr = fm * G2(ax - F.eyeX, yn - (F.eyeY + 0.135), 0.15, 0.018) * F.fold; mulc(c, 1 - 0.32 * cr); h -= 0.8 * cr; }
      { const rim = fm * G2(ax - F.eyeX, yn - F.eyeY, 0.21, 0.1); mix(c, [skin[0] * 0.55, skin[1] * 0.44, skin[2] * 0.42], rim * 0.4); }
      // occlusion: sockets, under the brow, nose sides, the nasolabial line, mouth corners, under the chin, ear folds
      ao -= fm * (0.22 * G2(ax - F.eyeX, yn - (F.eyeY + 0.06), 0.24, 0.1) + 0.18 * G2(ax - 0.13, yn - (yT + 0.02), 0.06, 0.12) + 0.2 * G2(ax - mw, yn - tm, 0.05, 0.04)
        + 0.12 * G2(xn, yn - (yB - 0.03), 0.18, 0.03));
      ao -= 0.25 * ss(-0.9, -1.0, yn);
      mulc(c, 1 - fm * (0.13 * G2(ax - 0.14, yn - (yT + 0.08), 0.05, 0.14) + 0.1 * G2(ax - 0.62, yn + 0.4, 0.2, 0.11) + 0.1 * G2(xn, yn - (tm - 0.15), 0.2, 0.035) + 0.12 * ss(0.62, 0.86, ax) * ss(-0.25, -0.5, yn)) - 0.22 * ss(-0.92, -1, yn));
      // nostrils (the nose's underside has its own rows) and the shadow under the nose
      const nos = fm * G2(ax - 0.06 * F.noseW, yn - (yB + 0.014), 0.034 * F.noseW, 0.016);
      mix(c, [skin[0] * 0.22, skin[1] * 0.12, skin[2] * 0.1], clamp(nos * 1.6) * F.nostril); ao -= 0.4 * nos;
      // nasolabial line, forehead lines, crow's feet, frown lines (age and temper)
      const [nd, nt] = seg(ax, yn, 0.17 * F.noseW, yB + 0.02, mw + 0.07, tm - 0.06);
      const naso = fm * G1(nd, 0.012) * ss(0, 0.15, nt) * (0.3 + age);
      mulc(c, 1 - 0.15 * naso); h -= 1.2 * naso;
      if (age > 0) {
        for (const fy of [0.3, 0.37, 0.44]) { const wl = fm * G1(yn - fy - 0.012 * Math.sin(xn * 6 + fy * 9), 0.006) * box(xn, 0.42, 0.15); h -= 1.4 * age * wl; mulc(c, 1 - 0.1 * age * wl); }
        const ca = Math.atan2(yn - F.eyeY, ax - F.eyeX - 0.17), cd = Math.hypot(yn - F.eyeY, ax - F.eyeX - 0.17);
        const crow = fm * Math.pow(Math.abs(Math.sin(ca * 7)), 18) * G1(cd - 0.08, 0.05) * (Math.abs(ca) < 0.9 ? 1 : 0);
        h -= 1.0 * age * crow;
        const jw = fm * G2(ax - mw - 0.05, yn - (tm - 0.16), 0.02, 0.07); h -= 0.8 * age * jw;
      }
      if (P.frown) { const fl = fm * G2(ax - 0.055, yn - (F.browY - 0.02), 0.009, 0.06); h -= 1.2 * fl; mulc(c, 1 - 0.12 * fl); }
      // freckles
      if (P.freckles) {
        const fz = hash2(Math.floor(mu * 900), Math.floor(mv * 900), seed + 11), zone = fm * (G2(xn, yn - (yT + 0.12), 0.25, 0.15) + G2(ax - 0.45, yn + 0.12, 0.2, 0.15));
        if (fz > 1 - 0.18 * P.freckles * clamp(zone)) mix(c, [skin[0] * 0.72, skin[1] * 0.52, skin[2] * 0.38], 0.55);
      }
      // stubble and beard shadow: the zone is jaw, chin, upper lip and lower cheeks, never the lips
      // lip outlines: the upper with a cupid's bow, both tapering into the corners
      const q = clamp(1 - Math.pow(ax / mw, 2)), Hu = F.lipU * (0.058 * Math.pow(q, 0.55) + 0.012 * G1(ax - 0.055, 0.035) - 0.014 * G1(xn, 0.022)), Hl = F.lipL * 0.08 * Math.pow(q, 0.5);
      const lipU = (yn >= tm ? 1 : 0) * ss(tm + Hu + 0.008, tm + Hu - 0.006, yn) * ss(mw + 0.01, mw - 0.02, ax);
      const lipL = (yn < tm ? 1 : 0) * ss(tm - Hl - 0.01, tm - Hl + 0.006, yn) * ss(mw + 0.005, mw - 0.03, ax);
      const lipM = clamp(lipU + lipL) * ss(0.05, 0.3, front); // the front only: the back of the head has the same x and height
      if (P.stubble || P.shadow) {
        const top = lerp(lerp(yB - 0.01, tm - 0.13, ss(0.16, 0.55, ax)), -0.08, ss(0.74, 0.97, ax)); // the beard line: under the nose, down round the cheeks, up to the sideburns
        const zone = clamp(fm * ss(top + 0.08, top - 0.08, yn) * ss(0.98, 0.72, ax) * (1 - lipM) * (1 - G2(ax - mw * 1.05, yn - tm, 0.03, 0.03)) * (1 - 0.55 * G2(xn, yn - (yB - 0.05), 0.2, 0.05)));
        const shadow = (P.shadow ?? 0.5) * zone;
        mix(c, [skin[0] * 0.66, skin[1] * 0.66, skin[2] * 0.74], shadow * 0.32);
        if (P.stubble) {
          const sz = hash2(Math.floor(mu * 3400), Math.floor(mv * 3400), seed + 5);
          if (sz > 1 - 0.3 * zone * P.stubble) { mix(c, P.hair, 0.3); h += 0.12; ro = lerp(ro, 0.7, 0.5); }
        }
      }
      if (P.goatee) { const gz = fm * box(xn, 0.16, 0.05) * ss(-0.68, -0.72, yn) * ss(-1.05, -0.95, yn) + fm * box(xn, mw + 0.03, 0.03) * G1(yn - (tm + 0.1), 0.03) * 0.7; if (hash2(Math.floor(mu * 1700), Math.floor(mv * 1700), seed + 9) < clamp(gz) * 0.9) { mix(c, P.hair, 0.85); h += 0.4; } }
      if (P.chinstrap) { const cz = fm * G1(yn + 0.78 + 0.22 * Math.pow(ax, 2) - 0.1 * ax, 0.05) * ss(1.0, 0.8, ax); if (hash2(Math.floor(mu * 1700), Math.floor(mv * 1700), seed + 9) < clamp(cz)) { mix(c, P.hair, 0.85); h += 0.4; } }
      // lips: colour, the vermilion edge, vertical lines, gloss on lipstick
      if (lipM > 0) {
        const lipC = P.lipstick ?? [skin[0] * 0.9, skin[1] * 0.58, skin[2] * 0.6];
        mix(c, lipC, lipM * (P.lipstick ? 0.9 : 0.75));
        h += lipM * (0.06 * Math.sin(xn * 160 + 3 * n2) - 0.2 * G1(yn - tm, 0.008));
        ro = lerp(ro, P.lipstick ? 0.32 : 0.42, lipM);
      }
      mix(c, [skin[0] * 0.3, skin[1] * 0.15, skin[2] * 0.13], fm * box(xn, mw, 0.03) * G1(yn - tm, 0.005) * 0.8);
      // brows: a stroke field along each brow's curve
      {
        const bx = (ax - F.browGap) / (F.browLen - F.browGap);
        if (bx > -0.08 && bx < 1.1 && fm > 0) {
          const cy = F.browY + F.browArch * Math.sin(clamp(bx) * PI * 0.9) - F.browTilt * (bx - 0.4) - 0.01;
          const th = F.browThick * (1 - 0.55 * clamp(bx)) * (1 - 0.3 * clamp(-bx * 10));
          const dyb = yn - cy, inb = ss(th, th * 0.55, Math.abs(dyb)) * ss(-0.08, 0.04, bx) * ss(1.1, 0.95, bx);
          const strand = vn(mu * 300 * 1.2 + dyb * 40 * sxn, mv * 900 - mu * 600 * sxn, seed + 13 + (sxn > 0 ? 0 : 50));
          const dens = inb * clamp(0.45 + 1.1 * (strand - 0.5) + 0.2) * (1 - 0.5 * (P.browSparse ?? 0) * vn(mu * 400, mv * 400, seed + 2));
          mix(c, brow, clamp(dens * 1.3) * 0.92); h += 0.6 * dens; ro = lerp(ro, 0.7, dens);
          if (P.browSlit && sxn * P.browSlit > 0 && Math.abs(bx - 0.62) < 0.05) mix(c, skin, inb * 0.9);
        }
      }
      // eye makeup: shadow on the lids and up to the brow, liner along the lashes, rouge, war paint
      if (P.shadowCol) { const sm = fm * G2(ax - F.eyeX - 0.02, yn - (F.eyeY + 0.06), 0.22, 0.1) * (P.shadowAmt ?? 0.6); mix(c, P.shadowCol, clamp(sm)); }
      if (P.liner) { const lm = fm * G2(ax - F.eyeX, yn - F.eyeY, 0.22, 0.09) * ss(F.eyeY + 0.12, F.eyeY + 0.06, yn); mix(c, [0.03, 0.02, 0.03], clamp(lm * P.liner)); }
      if (P.rouge) { const rm = fm * G2(ax - 0.52, yn + 0.18, 0.15, 0.1); mix(c, P.rouge, rm * 0.45); }
      if (P.war) { const wm = fm * box(yn - (F.eyeY + 0.01), 0.07, 0.015) * box(ax - 0.42, 0.33, 0.04) * (0.85 + 0.15 * n2); mix(c, P.war, wm * 0.85); ro = lerp(ro, 0.5, wm); }
      // scars: raised, paler, with stitch marks
      for (const [x0, y0, x1, y1, wd, stitches] of P.scars ?? []) {
        const [d, s] = seg(xn, yn, x0, y0, x1, y1), k = G1(d, wd) * (fm > 0.2 ? 1 : 0);
        if (k > 0.02) { mix(c, [Math.min(1, skin[0] * 1.08 + 0.05), skin[1] * 0.75, skin[2] * 0.78], k * 0.75); h += 0.9 * k - 0.6 * G1(d, wd * 0.35); ro = lerp(ro, 0.35, k); }
        if (stitches) { const along = s * Math.hypot(x1 - x0, y1 - y0); if (Math.abs(((along * 40) % 1) - 0.5) < 0.12 && d < wd * 3.2 && d > wd * 0.6) { mix(c, [skin[0] * 0.5, skin[1] * 0.3, skin[2] * 0.3], 0.7); h += 0.3; } }
      }
      // tattoos: ink just under the skin (no bump), slightly soft
      for (const tt2 of P.tattoos ?? []) {
        const dx = (xn - tt2.x) / tt2.s, dy = (yn - tt2.y) / tt2.s; let ink = 0;
        if (tt2.kind === 'tear') { const r = Math.hypot(dx, (dy + 0.25) * 0.8); ink = ss(0.55, 0.45, r - Math.max(0, dy) * 0.6); }
        else if (tt2.kind === 'star') { const a = Math.atan2(dy, dx), r = Math.hypot(dx, dy), k = 0.55 + 0.45 * Math.cos(5 * a + PI / 2); ink = ss(0.05, -0.05, r - (0.25 + 0.5 * Math.pow(k, 2.5))) - ss(0.05, -0.05, r - (0.25 + 0.5 * Math.pow(k, 2.5)) + 0.14); }
        else if (tt2.kind === 'tribal') { for (let q = 0; q < 3; q++) { const r = Math.hypot(dx - 0.2 * q, dy + 0.15 * q), ring = Math.abs(r - 0.6 + 0.12 * q); ink = Math.max(ink, ss(0.09, 0.05, ring) * (dx * sxn * (tt2.flip ?? 1) > -0.2 + 0.2 * q ? 1 : 0)); } }
        else if (tt2.kind === 'bars') { ink = ss(0.6, 0.55, Math.abs(dx)) * (Math.abs(((dy * 3 + 10) % 1) - 0.5) < 0.18 ? 1 : 0) * ss(1.0, 0.95, Math.abs(dy)); }
        else if (tt2.kind === 'cross') { ink = Math.max(ss(0.16, 0.1, Math.abs(dx)) * ss(1, 0.94, Math.abs(dy)), ss(0.16, 0.1, Math.abs(dy + 0.3)) * ss(0.6, 0.54, Math.abs(dx))); }
        if (ink > 0) mix(c, tt2.color ?? [0.06, 0.07, 0.12], clamp(ink) * 0.82 * fm);
      }
      if (P.mole) { const mm = G2(xn - P.mole[0], yn - P.mole[1], 0.014, 0.014); mix(c, [0.16, 0.08, 0.06], mm); h += 0.4 * mm; }
      // a shaved scalp: a grey-blue shadow of hair above the hairline (bald heads stop reading as a mannequin's dome)
      // a painted fade under a slicked hairline (P.hairEdge(a): the ring height where the hair shell meets the skin at loft
      // angle a): fine dark strands thinning out over a band below it, so hair never meets skin at a hard edge
      if (P.hairEdge) {
        const d = P.hairEdge(((b.a % (2 * PI)) + 2 * PI) % (2 * PI)) - tt, k = ss(P.hairFade ?? 0.14, -0.01, d) * ss(-0.08, 0.0, d);
        if (k > 0) {
          const st = hash2(Math.floor(mu * 2600), Math.floor(mv * 260), seed + 23), st2 = hash2(Math.floor(mu * 900), 7, seed + 29);
          mix(c, P.hair, Math.min(1, k * (0.45 + 0.75 * st * (0.5 + 0.5 * st2))));
          h += 0.15 * k;
        }
      }
      if (P.hairline) { const hy = 0.44 + 0.07 * G1(ax - 0.42, 0.16), hd = fm * ss(hy - 0.06, hy + 0.01, yn); const hz = hash2(Math.floor(mu * 1500), Math.floor(mv * 1500), seed + 19); if (hz < hd * 1.1) { mix(c, P.hair, 0.8); h += 0.2; } mix(c, [skin[0] * 0.7, skin[1] * 0.68, skin[2] * 0.7], hd * 0.4); }
      if (P.scalp) { const sc = ss(0.38, 0.5, yn - 0.06 * Math.cos(aRel)) * (1 - fm * ss(0.5, 0.4, yn)) + ss(-0.3, 0.1, -front) * ss(-0.35, -0.2, yn) * ss(-1, 0.4, front); const sz = hash2(Math.floor(mu * 1500), Math.floor(mv * 1500), seed + 17); mix(c, [skin[0] * 0.6, skin[1] * 0.6, skin[2] * 0.66], clamp(sc) * P.scalp * 0.5); if (sz > 1 - 0.4 * clamp(sc) * P.scalp) mix(c, P.hair, 0.5); }
      const ii = i;
      t.a[0][ii] = clamp(c[0]); t.a[1][ii] = clamp(c[1]); t.a[2][ii] = clamp(c[2]);
      t.height[ii] = h; t.rough[ii] = clamp(ro, 0.25, 0.9); t.ao[ii] = clamp(ao, 0.3, 1); t.metal[ii] = 0;
    }
  }
  return t;
}

/** an eyeball: sclera, a fibrous iris with a dark limbal ring, the pupil (front at uv 0.5, 0.5; 1 = the ball's edge seen from the front) */
export function paintEye(iris, N = 128, seed = 3, glow = false) {
  const t = new Tex(N, N);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = (x + 0.5) / N - 0.5, v = (y + 0.5) / N - 0.5, r = Math.hypot(u, v) * 2.1, a = Math.atan2(v, u), i = y * N + x;
    let c = [0.66, 0.62, 0.58], h = 0, ro = 0.2;
    const vein = Math.pow(1 - Math.abs(2 * vn(a * 9, r * 6, seed) - 1), 14) * ss(0.6, 0.95, r);
    c = [lerp(c[0], 0.78, vein * 0.6 + 0.2 * ss(0.7, 1.0, r)), lerp(c[1], 0.48, vein * 0.6 + 0.15 * ss(0.7, 1.0, r)), lerp(c[2], 0.46, vein * 0.6 + 0.15 * ss(0.7, 1.0, r))];
    const IR = 0.54, PR = 0.2;
    if (r < IR + 0.03) {
      const fib = vn(a * 40 / PI, r * 3, seed + 1), k = ss(IR + 0.03, IR - 0.02, r);
      const ic = [iris[0] * (0.5 + 0.45 * fib), iris[1] * (0.5 + 0.45 * fib), iris[2] * (0.5 + 0.45 * fib)];
      const collar = G1(r - 0.25, 0.05) * 0.3, limb = ss(IR - 0.12, IR, r);
      for (let q = 0; q < 3; q++) c[q] = lerp(c[q], ic[q] * (1 + collar) * (1 - 0.65 * limb), k);
      h -= 0.3 * k;
      if (r < PR + 0.015) { const pk = ss(PR + 0.015, PR - 0.01, r); c = c.map((x) => lerp(x, 0.012, pk)); }
    }
    const lidS = lerp(0.5, 1, ss(-0.26, 0.04, v)) * lerp(0.72, 1, ss(0.9, 0.5, Math.abs(u) * 2.1)); // the upper lid's shadow on the ball, the corners sunk: an eye sits in its socket
    t.a[0][i] = clamp(c[0] * lidS); t.a[1][i] = clamp(c[1] * lidS); t.a[2][i] = clamp(c[2] * lidS); t.height[i] = h; t.rough[i] = ro; t.ao[i] = lerp(0.6, 1, ss(-0.26, 0.04, v));
  }
  if (glow) { const e = t.emissive(); for (let i = 0; i < N * N; i++) { e[0][i] = t.a[0][i]; e[1][i] = t.a[1][i]; e[2][i] = t.a[2][i]; } }
  return t;
}

// ── expressions: bone offsets in head units (p in R, r in radians); the clip library mixes them in ──
export const FACE_BONES = ['Jaw', 'LidUL', 'LidLL', 'LidUR', 'LidLR', 'BrowL', 'BrowR', 'MouthL', 'MouthR', 'LipU'];
const sym = (o) => o; // poses are written for both sides explicitly
export const EXPRESSIONS = {
  neutral: {},
  // attack: brows down and in, eyes narrowed, the upper lip lifts off the teeth, corners pulled back
  snarl: sym({ BrowL: { p: [-0.03, -0.045, 0.01], r: [0, 0, 0.18] }, BrowR: { p: [0.03, -0.045, 0.01], r: [0, 0, -0.18] }, LidUL: { r: [0.2, 0, 0] }, LidUR: { r: [0.2, 0, 0] },
    LidLL: { r: [-0.14, 0, 0] }, LidLR: { r: [-0.14, 0, 0] }, LipU: { p: [0, 0.055, 0.01] }, Jaw: { r: [0.1, 0, 0] }, MouthL: { p: [0.035, 0.012, -0.025] }, MouthR: { p: [-0.035, 0.012, -0.025] } }),
  // a big effort: mouth open in a shout
  shout: sym({ BrowL: { p: [-0.03, -0.05, 0.01], r: [0, 0, 0.2] }, BrowR: { p: [0.03, -0.05, 0.01], r: [0, 0, -0.2] }, LidUL: { r: [0.22, 0, 0] }, LidUR: { r: [0.22, 0, 0] },
    LidLL: { r: [-0.16, 0, 0] }, LidLR: { r: [-0.16, 0, 0] }, LipU: { p: [0, 0.06, 0.015] }, Jaw: { r: [0.36, 0, 0] }, MouthL: { p: [0.045, 0, -0.025] }, MouthR: { p: [-0.045, 0, -0.025] } }),
  // hit: eyes squeezed shut, inner brows up, a grimace with the teeth clenched
  wince: sym({ BrowL: { p: [-0.02, -0.025, 0], r: [0, 0, -0.22] }, BrowR: { p: [0.02, -0.025, 0], r: [0, 0, 0.22] }, LidUL: { r: [0.58, 0, 0] }, LidUR: { r: [0.58, 0, 0] },
    LidLL: { r: [-0.22, 0, 0] }, LidLR: { r: [-0.22, 0, 0] }, LipU: { p: [0, 0.04, 0] }, Jaw: { r: [0.12, 0, 0] }, MouthL: { p: [0.06, -0.02, -0.035] }, MouthR: { p: [-0.06, -0.02, -0.035] } }),
  // knocked out: lids half down, jaw slack, mouth corners fallen
  ko: sym({ BrowL: { p: [0, 0.02, 0], r: [0, 0, -0.1] }, BrowR: { p: [0, 0.02, 0], r: [0, 0, 0.1] }, LidUL: { r: [0.4, 0, 0] }, LidUR: { r: [0.36, 0, 0] },
    LidLL: { r: [-0.04, 0, 0] }, LidLR: { r: [-0.04, 0, 0] }, Jaw: { r: [0.24, 0, 0.03] }, MouthL: { p: [0, -0.03, 0] }, MouthR: { p: [0, -0.03, 0] } }),
  dead: sym({ BrowL: { p: [0, 0.01, 0] }, BrowR: { p: [0, 0.01, 0] }, LidUL: { r: [0.66, 0, 0] }, LidUR: { r: [0.66, 0, 0] }, LidLL: { r: [-0.05, 0, 0] }, LidLR: { r: [-0.05, 0, 0] },
    Jaw: { r: [0.18, 0, 0.04] }, MouthL: { p: [0, -0.025, 0] }, MouthR: { p: [0, -0.025, 0] } }),
  // a taunt: one brow up, a crooked grin
  grin: sym({ BrowL: { p: [0, 0.06, 0], r: [0, 0, -0.08] }, BrowR: { p: [0.02, -0.03, 0.01], r: [0, 0, -0.15] }, LidUL: { r: [0.06, 0, 0] }, LidUR: { r: [0.16, 0, 0] },
    LidLL: { r: [-0.1, 0, 0] }, LidLR: { r: [-0.16, 0, 0] }, LipU: { p: [0.01, 0.03, 0.005] }, Jaw: { r: [0.06, 0, 0] }, MouthL: { p: [0.055, 0.07, -0.02] }, MouthR: { p: [-0.03, 0.03, -0.015] } }),
  // sustained effort (holding, lifting, getting up)
  effort: sym({ BrowL: { p: [-0.015, -0.03, 0], r: [0, 0, 0.1] }, BrowR: { p: [0.015, -0.03, 0], r: [0, 0, -0.1] }, LidUL: { r: [0.14, 0, 0] }, LidUR: { r: [0.14, 0, 0] },
    LidLL: { r: [-0.08, 0, 0] }, LidLR: { r: [-0.08, 0, 0] }, LipU: { p: [0, 0.025, 0] }, Jaw: { r: [0.05, 0, 0] }, MouthL: { p: [0.03, -0.01, -0.02] }, MouthR: { p: [-0.03, -0.01, -0.02] } }),
  // the hit reactions (morph targets; only the jaw comes from here): fear drops the jaw, anger clenches it, bliss parts it
  fear: { Jaw: { r: [0.4, 0, 0] } }, anger: { Jaw: { r: [0.04, 0, 0] } }, bliss: { Jaw: { r: [0.1, 0, 0] } },
  // held or thrown: a pained open mouth
  grimace: sym({ BrowL: { p: [-0.02, 0.0, 0], r: [0, 0, -0.25] }, BrowR: { p: [0.02, 0.0, 0], r: [0, 0, 0.25] }, LidUL: { r: [0.3, 0, 0] }, LidUR: { r: [0.34, 0, 0] },
    LidLL: { r: [-0.12, 0, 0] }, LidLR: { r: [-0.12, 0, 0] }, LipU: { p: [0, 0.05, 0] }, Jaw: { r: [0.26, 0, 0] }, MouthL: { p: [0.05, -0.03, -0.03] }, MouthR: { p: [-0.05, -0.03, -0.03] } }),
};

// ── expressions as morph targets on the head mesh (the game drives them by name; the Jaw bone still opens the mouth) ──
// Each target is a displacement field over the head grid (in head units: the offsets the expression bones used to make)
// plus, for the lids, a turn about the eye's side to side axis. Lid angles: [upper L, upper R, lower L, lower R]
// (+ closes an upper lid, - raises a lower one).
export const FACE_TARGETS = ['blink', 'snarl', 'shout', 'wince', 'ko', 'grin', 'effort', 'grimace', 'fear', 'anger', 'bliss'];
const LIDS = {
  blink: [0.66, 0.66, -0.08, -0.08], snarl: [0.2, 0.2, -0.14, -0.14], shout: [0.22, 0.22, -0.16, -0.16], wince: [0.58, 0.58, -0.22, -0.22],
  ko: [0.4, 0.36, -0.04, -0.04], grin: [0.06, 0.16, -0.1, -0.16], effort: [0.14, 0.14, -0.08, -0.08], grimace: [0.3, 0.34, -0.12, -0.12],
  // cartoon-sized so they read at the street camera: fear opens the lids wide, anger narrows them hard, bliss half closes them
  fear: [-0.3, -0.3, 0.14, 0.14], anger: [0.32, 0.32, -0.24, -0.24], bliss: [0.46, 0.46, -0.1, -0.1],
};
/** how far each target closes the upper lids (the game keeps a blink from adding to an expression that already closes them) */
export const LID_CLOSE = Object.fromEntries(Object.entries(LIDS).map(([k, v]) => [k, (v[0] + v[1]) / 2 / 0.66]));
/** p turned about the x axis through c by a (the lid bones' axis) */
const rotX = (p, c, a) => { const y = p[1] - c[1], z = p[2] - c[2], cs = Math.cos(a), sn = Math.sin(a); return [p[0], c[1] + y * cs - z * sn, c[2] + y * sn + z * cs]; };
/** the displacement (metres, head space) of one head vertex for target `name` */
function faceTarget(name, p, tag, M) {
  const { H, F, E } = M, R = H.R;
  if (typeof tag === 'string') { // lids and lash rims: a turn about the eye's axis
    const m = /^(lid|lash)([UL])([LR])$/.exec(tag);
    if (!m) return [0, 0, 0];
    const a = LIDS[name][(m[2] === 'U' ? 0 : 2) + (m[3] === 'L' ? 0 : 1)];
    return sub(rotX(p, E[m[3]].c, a), p);
  }
  if (!tag || !tag.g) return [0, 0, 0];
  const { xn, yn, front, seam } = tag, ax = Math.abs(xn), sgn = xn >= 0 ? 1 : -1, L = sgn > 0;
  const fm = ss(-0.15, 0.45, front), tm = F.tm, mw = F.mw;
  const up = seam === 'upper' || (seam == null && yn >= tm);
  const brow = 0.95 * G2(ax - 0.36, yn - F.browY, 0.24, 0.1) * fm * (yn > F.eyeY + 0.07 ? 1 : ss(F.eyeY + 0.02, F.eyeY + 0.07, yn));
  const inner = ss(0.5, 0.12, ax);
  const mc = 1.7 * G2(ax - mw, yn - tm, 0.13, 0.09) * fm; // the mouth moves more than the bones did: it has to read at game distance
  const lu = up ? 1.4 * G2(xn, yn - (tm + 0.06), 0.24, 0.07) * fm * ss(mw * 1.25, mw * 0.7, ax) : 0;
  const ll = !up ? 1.4 * G2(xn, yn - (tm - 0.07), 0.26, 0.08) * fm * ss(mw * 1.25, mw * 0.7, ax) : 0;
  const ck = G2(ax - 0.5, yn + 0.12, 0.17, 0.12) * fm;
  const { yR } = noseRows(F), nw = G2(ax - 0.12, yn - (yR - 0.12), 0.08, 0.08) * fm;
  const ue = G2(ax - F.eyeX, yn - (F.eyeY - 0.13), 0.16, 0.06) * fm, ce = G2(ax - F.eyeX, yn - (F.eyeY + 0.12), 0.16, 0.05) * fm;
  const d = [0, 0, 0];
  /** add k times the offset o, written for the character's left side (x toward its middle is -) */
  const add3 = (k, o) => { if (k <= 0) return; d[0] += sgn * o[0] * k; d[1] += o[1] * k; d[2] += o[2] * k; };
  switch (name) {
    case 'blink': add3(ce, [0, -0.012, 0]); break;
    case 'snarl': add3(brow, [-0.03, -0.045 - 0.025 * inner, 0.01]); add3(lu, [0, 0.055, 0.012]); add3(mc, [0.035, 0.014, -0.025]); add3(nw, [0, 0.022, 0.006]); add3(ck, [0, 0.016, 0.01]); add3(ue, [0, 0.014, 0.004]); break;
    case 'shout': add3(brow, [-0.03, -0.05 - 0.02 * inner, 0.01]); add3(lu, [0, 0.06, 0.015]); add3(mc, [0.045, 0, -0.025]); add3(ck, [0, 0.012, 0.006]); add3(nw, [0, 0.01, 0]); break;
    case 'wince': add3(brow, [-0.02, -0.03 + 0.06 * inner, 0]); add3(lu, [0, 0.04, 0]); add3(mc, [0.06, -0.02, -0.035]); add3(ck, [0, 0.026, 0.015]); add3(ue, [0, 0.022, 0.006]); add3(nw, [0, 0.016, 0.004]); break;
    case 'ko': add3(brow, [0, 0.02 + 0.012 * inner, 0]); add3(mc, [0, -0.03, 0]); add3(ll, [0, -0.012, 0.004]); break;
    case 'grin':
      if (L) { add3(brow, [0, 0.06, 0]); add3(mc, [0.055, 0.07, -0.02]); add3(ck, [0, 0.03, 0.014]); add3(ue, [0, 0.014, 0.004]); }
      else { add3(brow, [0.02, -0.03, 0.01]); add3(mc, [0.03, 0.03, -0.015]); add3(ck, [0, 0.012, 0.006]); }
      add3(lu, [0.01 * sgn, 0.03, 0.005]); break;
    case 'effort': add3(brow, [-0.015, -0.03 - 0.012 * inner, 0]); add3(lu, [0, 0.025, 0]); add3(mc, [0.03, -0.01, -0.02]); add3(ck, [0, 0.01, 0.006]); break;
    // fear and horror: brows shot up (the inner ends highest), the upper eye opened, the lower lip dropped, the corners pulled back
    case 'fear': add3(brow, [0, 0.13 + 0.05 * inner, 0.012]); add3(ue, [0, 0.034, 0.006]); add3(lu, [0, 0.045, 0.006]); add3(ll, [0, -0.08, 0.012]); add3(mc, [0.09, -0.05, -0.045]); add3(ck, [0, -0.012, 0]); break;
    // anger: brows slammed down and in, the nose wrinkled, the upper lip off the teeth, the corners turned down
    case 'anger': add3(brow, [-0.05, -0.1 - 0.06 * inner, 0.016]); add3(lu, [0, 0.09, 0.018]); add3(mc, [0.035, -0.05, -0.025]); add3(nw, [0, 0.04, 0.01]); add3(ck, [0, 0.03, 0.016]); add3(ue, [0, 0.02, 0.005]); break;
    // bliss: brows lifted in delight, a wide symmetric grin pushing the cheeks up under half closed eyes
    case 'bliss': add3(brow, [0.01, 0.1, 0]); add3(mc, [0.09, 0.1, -0.03]); add3(ck, [0, 0.055, 0.024]); add3(lu, [0, 0.03, 0.008]); add3(ue, [0, 0.026, 0.008]); break;
    case 'grimace': add3(brow, [-0.02, 0.035 * inner, 0]); add3(lu, [0, 0.05, 0]); add3(mc, [0.05, -0.03, -0.03]); add3(ll, [0, -0.014, 0]); add3(ck, [0, 0.012, 0.006]); break;
  }
  return mul(d, R);
}
/**
 * Morph targets for the head groups of a SkinMesh (the face surface with its lids and ears, the lash rims):
 * { name: { group: { pos, nrm, any } } }, normal deltas from the deformed surface.
 */
export function faceTargets(m, M, groups = ['face', 'lash']) {
  const out = {};
  for (const name of FACE_TARGETS) {
    out[name] = {};
    for (const gname of groups) {
      const g = m.groups.get(gname);
      if (!g || !g.idx.length) continue;
      const nv = g.pos.length / 3, dp = new Float32Array(nv * 3);
      for (let v = 0; v < nv; v++) {
        const tag = g.tag[v];
        if (!tag) continue;
        const du = faceTarget(name, [g.pos[v * 3], g.pos[v * 3 + 1], g.pos[v * 3 + 2]], tag, M);
        dp[v * 3] = du[0]; dp[v * 3 + 1] = du[1]; dp[v * 3 + 2] = du[2];
      }
      const nrest = accNormals(g.pos, g.idx, g.nrm), pos2 = g.pos.map((x, i) => x + dp[i]), ndef = accNormals(pos2, g.idx, g.nrm);
      const dn = new Float32Array(nv * 3);
      let any = false;
      for (let i = 0; i < nv * 3; i++) { dn[i] = Math.abs(dp[i]) > 0 || Math.abs(ndef[i] - nrest[i]) > 1e-4 ? ndef[i] - nrest[i] : 0; if (Math.abs(dp[i]) > 1e-7) any = true; }
      out[name][gname] = { pos: dp, nrm: dn, any };
    }
  }
  return out;
}
/** face-accumulated normals, welded across seams where the given normals agree (never across the mouth slit) */
function accNormals(pos, idx, nrm0) {
  const nv = pos.length / 3, N = new Float64Array(nv * 3);
  for (let t = 0; t < idx.length; t += 3) {
    const a = idx[t], b = idx[t + 1], c = idx[t + 2];
    const ux = pos[b * 3] - pos[a * 3], uy = pos[b * 3 + 1] - pos[a * 3 + 1], uz = pos[b * 3 + 2] - pos[a * 3 + 2];
    const vx = pos[c * 3] - pos[a * 3], vy = pos[c * 3 + 1] - pos[a * 3 + 1], vz = pos[c * 3 + 2] - pos[a * 3 + 2];
    const fx = uy * vz - uz * vy, fy = uz * vx - ux * vz, fz = ux * vy - uy * vx;
    for (const v of [a, b, c]) { N[v * 3] += fx; N[v * 3 + 1] += fy; N[v * 3 + 2] += fz; }
  }
  const key = (v) => `${Math.round(pos[v * 3] * 1e6)},${Math.round(pos[v * 3 + 1] * 1e6)},${Math.round(pos[v * 3 + 2] * 1e6)},${Math.round(nrm0[v * 3] * 20)},${Math.round(nrm0[v * 3 + 1] * 20)},${Math.round(nrm0[v * 3 + 2] * 20)}`;
  const groups = new Map();
  for (let v = 0; v < nv; v++) { const k = key(v); if (!groups.has(k)) groups.set(k, []); groups.get(k).push(v); }
  const out = new Float32Array(nv * 3);
  for (const vs of groups.values()) {
    let x = 0, y = 0, z = 0; for (const v of vs) { x += N[v * 3]; y += N[v * 3 + 1]; z += N[v * 3 + 2]; }
    const l = Math.hypot(x, y, z) || 1; for (const v of vs) { out[v * 3] = x / l; out[v * 3 + 1] = y / l; out[v * 3 + 2] = z / l; }
  }
  return out;
}
