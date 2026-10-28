// A sculpted head and face for skinned characters (used by make-characters.mjs; written to be copied by other games).
//
// The head is one dense surface (rows of height, columns of angle around the head, densest at the front) shaped in two
// steps: a skull section per height (width, depth, squareness, the jaw angle) and then the face carved and built on
// it as smooth fields: brow ridge, eye sockets, cheekbones and the hollow under them, a nose with bridge, tip, wings,
// nostrils and a real underside, philtrum, lips rolled in to a mouth line that is an actual slit (the lower lip and
// chin ride the Jaw bone, so the mouth opens onto teeth), the groove under the lower lip, chin, jaw line, temples.
// Everything is in "unit head" metres (head scale 1) and scaled per character.
//
// Also: eyeballs on EyeL/EyeR bones (the game aims them), upper and lower lids, lashes, eyebrows made of strands (with
// volume), teeth on the head and the jaw, the mouth interior, ears.
//
// Expressions are morph targets on the head mesh (names: blink, browUp, browDown, browPinch, smile, snarl, shout, wince,
// ko, jawOpen, squint): each is a displacement field over the face in the same unit coordinates, evaluated per vertex
// (faceTargets), so any head this module builds gets them. The jaw opening itself is the Jaw bone.
//
// Coordinates: y up, +z the face's front, +x the character's left. faceBones(world, sp) adds the bones; buildHead(ctx)
// adds the geometry; faceSkinFrame(L) is the texture mapping make-textures' faceSkin paints in (cylindrical about the
// head: u around, v down), so painted lips, brows and stubble land where the geometry has them.
import { add, sub, mul, norm, cross, dot } from './charmesh.mjs';

const PI = Math.PI;
const G = (x, w) => Math.exp(-(x * x) / (w * w));
const ss = (a, b, x) => { const t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const lerp = (a, b, t) => a + (b - a) * t;
const spw = (x, e) => Math.sign(x) * Math.pow(Math.abs(x), 2 / e);
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));

// ── the skull section per height (unit metres; y is the height above the eye line) ──
// [y, half width, half depth, centre z]
const SECT = [[-0.127, 0.004, 0.006, 0.054], [-0.123, 0.02, 0.026, 0.047], [-0.116, 0.032, 0.041, 0.037], [-0.105, 0.043, 0.056, 0.027],
  [-0.09, 0.052, 0.069, 0.016], [-0.075, 0.058, 0.08, 0.008], [-0.058, 0.062, 0.087, 0.004], [-0.038, 0.066, 0.092, 0.002],
  [-0.016, 0.07, 0.096, 0.0], [0.0, 0.0725, 0.098, -0.001], [0.02, 0.0745, 0.1, -0.003], [0.045, 0.0755, 0.101, -0.005], [0.07, 0.072, 0.097, -0.008]];
const CROWN = 0.116;
/** a smooth curve through a table of [x, v...] rows (x ascending or descending), column k: Catmull-Rom, so the surface has
 * no flat steps at the rows (a smoothstep between rows makes bands across the head) */
function spline(T, x, k) {
  const asc = T[T.length - 1][0] > T[0][0], n = T.length;
  let i = 0; while (i < n - 2 && (asc ? T[i + 1][0] < x : T[i + 1][0] > x)) i++;
  const P = (j) => T[Math.max(0, Math.min(n - 1, j))];
  const x0 = P(i)[0], x1 = P(i + 1)[0], t = clamp((x - x0) / (x1 - x0), 0, 1);
  const m = (j) => (P(j + 1)[k] - P(j - 1)[k]) / (P(j + 1)[0] - P(j - 1)[0] || 1) * (x1 - x0);
  const m0 = i === 0 ? (P(1)[k] - P(0)[k]) : m(i), m1 = i + 1 === n - 1 ? (P(n - 1)[k] - P(n - 2)[k]) : m(i + 1);
  const t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * P(i)[k] + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * P(i + 1)[k] + (t3 - t2) * m1;
}
function section(y, L) {
  let w, d, cz;
  if (y > 0.04) { // a rounded dome over the crown (a short cap reads as a flat-topped box on a bald head)
    const k = Math.sqrt(Math.max(0, 1 - ((y - 0.04) / (CROWN - 0.04)) ** 2)), kk = Math.pow(k, 0.85);
    w = Math.max(0.0012, 0.0756 * kk); d = Math.max(0.0015, 0.101 * kk); cz = -0.0045 - 0.009 * (1 - k);
  } else {
    w = spline(SECT, y, 1); d = spline(SECT, y, 2); cz = spline(SECT, y, 3);
  }
  // the jaw: wider and squarer per character below the cheekbones
  const low = ss(-0.03, -0.085, y) * (1 - ss(-0.105, -0.127, y) * 0.6);
  w *= lerp(1, Math.pow(L.jawW, 0.75), low);
  const e = 2.25 + 0.3 * ss(0.03, -0.02, y) + (0.15 + 0.55 * (L.jawW - 0.8)) * low;
  // the front of the face is narrower than the skull behind it (more so toward the muzzle and chin)
  const taper = 0.08 + 0.05 * ss(0.02, -0.04, y) + 0.1 * ss(-0.05, -0.11, y);
  // the back of the skull flows down into the neck (the occiput over the nape) instead of tucking in under it: below the
  // ears the back keeps its depth while the jaw's centre moves forward
  const BK = [[-0.04, 0.094], [-0.06, 0.085], [-0.08, 0.075], [-0.1, 0.067], [-0.115, 0.062], [-0.127, 0.058]];
  let db = d;
  if (y < -0.035) {
    db = lerp(d, Math.max(d, cz + spline(BK, y, 1)), ss(-0.035, -0.055, y));
  }
  return { w, d, db, cz, e, taper };
}
/** the base skull point (no face features) at height y and angle phi (0 = the front, + toward +x) */
function basePoint(y, phi, L) {
  const S = section(y, L), s = Math.sin(phi), c = Math.cos(phi), fr = Math.max(0, c);
  const x = S.w * spw(s, S.e) * (1 - S.taper * fr * fr), z = S.cz + (c >= 0 ? S.d : S.db) * spw(c, S.e);
  return [x, z, S];
}

/** the mouth line height at x (corners a touch up: a relaxed mouth, never sulky) */
const mouthLine = (x, L) => L.mouthY + 0.0016 * Math.min(1.4, (x / L.mouthW) ** 2);
/** soft lip masks at (x, y): upper and lower vermilion, 0..1, and the profile inside each (0 at the mouth line) */
export function lipMasks(x, y, L) {
  const mw = L.mouthW, ax = Math.abs(x), q = clamp(1 - (x / (mw * 1.04)) ** 2, 0, 1);
  const yl = mouthLine(x, L);
  const hu = (0.0072 * L.lips * Math.pow(q, 0.55) + 0.0011 * G(ax - 0.0048, 0.0032) - 0.0007 * G(x, 0.0022)) * (0.25 + 0.75 * ss(0, 0.25, q));
  const hl = 0.0096 * L.lips * Math.pow(q, 0.5) * (0.3 + 0.7 * ss(0, 0.2, q));
  const su = hu > 0.0004 ? (y - yl) / hu : -1, sl = hl > 0.0004 ? (yl - y) / hl : -1;
  const up = su >= 0 ? ss(1.08, 0.92, su) : 0, lo = sl >= 0 ? ss(1.1, 0.88, sl) : 0;
  return { up, lo, su, sl, yl, q, hu, hl };
}

/** the face features as displacements of the base point: [dx scale, dz] in unit metres */
function features(x, y, phi, L) {
  const ax = Math.abs(x), aphi = Math.abs(phi), fr = ss(0.05, 0.6, Math.cos(phi));
  const ex = L.eyeX, mw = L.mouthW;
  let dz = 0, kr = 0; // dz: forward on the front; kr: radial scale on the sides
  // brow ridge over the eyes, the glabella between them, a dip at the root of the nose
  dz += 0.0055 * L.brow * G(y - 0.021, 0.0085) * G(ax - 0.026, 0.026) + 0.0025 * L.brow * G(y - 0.017, 0.01) * G(x, 0.012);
  dz -= 0.0035 * G(y - 0.004, 0.008) * G(x, 0.009);
  // eye sockets: a deep almond where the eyeball sits (lids cover its edges), the hollow under the brow, the tear trough
  {
    const dx = ax - ex, ry = y > 0.001 ? 0.0125 : 0.0098, r = Math.hypot(dx / (0.0205 * L.eyeSize), (y - 0.001) / (ry * L.eyeSize));
    dz -= 0.017 * ss(1.18, 0.62, r);
    dz -= 0.0028 * G(y - 0.0125, 0.005) * G(dx + 0.002, 0.017);
    dz -= 0.0013 * G(y + 0.0175, 0.0045) * G(dx + 0.006, 0.013);
  }
  // cheeks: the bone across (sides), the fullness in front, the hollow beneath (leaner faces)
  kr += 0.05 * L.cheek * G(y + 0.017, 0.013) * G(aphi - 0.98, 0.33);
  dz += 0.0042 * L.cheek * G(y + 0.03, 0.015) * G(ax - 0.039, 0.016);
  kr -= 0.035 * L.hollow * G(y + 0.054, 0.012) * G(aphi - 0.85, 0.28);
  // temples
  kr -= 0.022 * G(y - 0.032, 0.02) * G(aphi - 1.18, 0.25);
  // the jaw: an angle at the back, a tuck under it so the jaw line reads, the chin
  kr += 0.045 * L.jawW * G(y + 0.08, 0.016) * G(aphi - 1.45, 0.38);
  kr -= 0.06 * G(y + 0.107, 0.012) * G(aphi - 1.05, 0.55);
  // the nose: a profile from the root to the tip and back under it to the lip, its width along the way, wings, nostrils
  {
    const nk = L.noseK, P = (yy) => { // projection of the bridge and tip above the face
      const pts = [[0.013, 0], [0.004, 0.0034], [-0.01, 0.0078], [-0.025, 0.0135], [-0.035, 0.0198], [-0.0405, 0.0228], [-0.0445, 0.0218], [-0.0475, 0.0165], [-0.05, 0.0085], [-0.0518, 0.003], [-0.054, 0]];
      if (yy >= pts[0][0] || yy <= pts[pts.length - 1][0]) return 0;
      let i = 0; while (pts[i + 1][0] > yy) i++;
      const A = pts[i], B = pts[i + 1], t = (A[0] - yy) / (A[0] - B[0]), u = t * t * (3 - 2 * t);
      return lerp(A[1], B[1], u);
    };
    const yn = 0.004 + (y - 0.004) / nk; // a longer nose stretches down from the root
    const wn = lerp(0.0078, 0.0115, ss(-0.02, -0.046, yn)) * (0.9 + 0.1 * nk);
    dz += P(yn) * nk * Math.exp(-Math.pow(ax / wn, 2.3));
    dz += 0.0068 * nk * G(ax - 0.0158 * (0.92 + 0.08 * nk), 0.0056) * G(yn + 0.0455, 0.0062); // the wings
    dz -= 0.0018 * G(ax - 0.0205, 0.0016) * G(yn + 0.043, 0.008); // the groove round each wing
    dz -= 0.0055 * G(ax - 0.0076, 0.0031) * G(yn + 0.0493, 0.0022); // nostrils, under the tip
  }
  // philtrum: two ridges and the groove between, from under the nose to the lip
  dz += 0.0011 * G(ax - 0.0046, 0.0019) * G(y - (L.mouthY + 0.013), 0.0065) - 0.0006 * G(x, 0.0022) * G(y - (L.mouthY + 0.013), 0.006);
  // the dental arch under the lips
  dz += 0.0072 * G(y - L.mouthY, 0.02) * G(x, 0.031);
  // nasolabial folds: a crease from each wing down past the mouth corner, the cheek fuller outside it
  {
    const a = [0.0195, -0.046], b = [mw + 0.0085, L.mouthY - 0.008];
    const dxs = b[0] - a[0], dys = b[1] - a[1], t = clamp(((ax - a[0]) * dxs + (y - a[1]) * dys) / (dxs * dxs + dys * dys), 0, 1);
    const px = a[0] + dxs * t, py = a[1] + dys * t, dd = Math.hypot(ax - px, y - py), side = Math.sign((ax - px) * -dys + (y - py) * dxs);
    const along = ss(-0.1, 0.15, t) * ss(1.15, 0.8, t);
    dz -= 0.0011 * (0.15 + L.age) * G(dd, 0.0016) * along;
    if (side > 0) dz += 0.0016 * G(dd - 0.0045, 0.0045) * along;
  }
  // lips: rolled out from the mouth line, the upper with a bow and a fine ridge at its edge, the lower fuller; the line
  // itself turns in to the slit
  {
    const M = lipMasks(x, y, L);
    if (M.su >= 0 && M.su < 1.6) { const s = Math.min(1, M.su); dz += 0.0034 * L.lips * Math.sqrt(Math.max(0, M.q)) * Math.pow(Math.sin(PI * (0.12 + 0.88 * s)), 0.7) * ss(1.5, 1.0, M.su); dz += 0.0005 * G(M.su - 1, 0.12) * M.q; }
    if (M.sl >= 0 && M.sl < 1.6) { const s = Math.min(1, M.sl); dz += 0.0052 * L.lips * Math.sqrt(Math.max(0, M.q)) * Math.pow(Math.sin(PI * (0.1 + 0.9 * s)), 0.6) * ss(1.5, 1.0, M.sl); }
    dz -= 0.0065 * G((y - M.yl) / 0.0015, 1) * Math.sqrt(M.q);
    dz -= 0.0014 * G(ax - mw * 1.02, 0.0028) * G(y - M.yl, 0.003); // the corners
  }
  // under the lower lip, the chin
  dz -= 0.0034 * G(y - (L.mouthY - 0.0175), 0.0045) * G(x, 0.018);
  dz += 0.0075 * L.chin * G(y - (L.mouthY - 0.032), 0.011) * G(x, 0.018);
  return [kr, dz * fr + 0 * (1 - fr)];
}

/** the head layout for a spec (shared by the bones, the mesh and the face texture) */
export function faceLayout(headBone, sp) {
  const HS = (sp.head ?? 1) * 1.08, F = sp.face ?? {};
  const Fw = F.width ?? 1, Fl = F.length ?? 1;
  const yE = headBone[1] + 0.128 * (sp.head ?? 1);
  const L = {
    HS, Fw, Fl, yE, zoff: 0.006 * HS,
    eyeX: 0.0318 * (F.eyeSpread ?? 1), eyeSize: F.eyeSize ?? 1, eyeR: 0.0119 * (F.eyeSize ?? 1),
    mouthY: -0.0705, mouthW: 0.0245 * (F.mouthW ?? 1), noseK: sp.nose ?? 1,
    jawW: sp.jaw ?? 1, brow: F.brow ?? 1, cheek: F.cheek ?? 1, hollow: F.hollow ?? 0.6, lips: F.lips ?? 1, chin: F.chin ?? 1, age: F.age ?? 0,
  };
  // world from unit coordinates, and back
  L.toWorld = (p) => [p[0] * HS * Fw, yE + p[1] * HS * Fl, L.zoff + p[2] * HS];
  L.toUnit = (p) => [p[0] / (HS * Fw), (p[1] - yE) / (HS * Fl), (p[2] - L.zoff) / HS];
  // the ellipsoid hair and hats are placed on (world): centre, half width, half height, half depth
  L.hc = [0, yE + 0.5 * (CROWN - 0.127) * HS * Fl, L.zoff - 0.004 * HS];
  L.HY = 0.5 * (CROWN + 0.127) * HS * Fl; L.R = 0.0755 * HS * Fw; L.RZ = 0.101 * HS;
  return L;
}

/** the bare skull section at world height yw: half width, half depth and centre z in world metres */
export function headSection(L, yw) {
  const yu = clamp((yw - L.yE) / (L.HS * L.Fl), -0.127, CROWN), S = section(yu, L);
  return { w: S.w * L.HS * L.Fw, d: S.d * L.HS, cz: L.zoff + S.cz * L.HS, yu };
}
/** a point on the head surface (unit coords) at height y and angle phi, with the face features */
function headPoint(y, phi, L) {
  const [x0, z0, S] = basePoint(y, phi, L);
  const [kr, dz] = features(x0, y, phi, L);
  return [x0 * (1 + kr), y, S.cz + (z0 - S.cz) * (1 + kr) + dz];
}
/** the front surface point at (x, y) (unit), by bisection on the angle, and its outward normal */
function surfAt(x, y, L, out = 0) {
  let lo = -1.45, hi = 1.45;
  for (let i = 0; i < 44; i++) { const mid = (lo + hi) / 2; if (headPoint(y, mid, L)[0] < x) lo = mid; else hi = mid; }
  const phi = (lo + hi) / 2, p = headPoint(y, phi, L), e = 0.0004;
  const pa = headPoint(y, phi + 0.004, L), pu = headPoint(y + e, phi, L);
  let n = norm(cross(sub(pa, p), sub(pu, p)));
  if (n[2] < 0) n = mul(n, -1);
  return { p: add(p, mul(n, out)), n, phi };
}

/** the face texture's mapping: unit point -> [u, v] (u around the head, the front at 0.5; v from the crown down) */
export function faceUVUnit(p, L) {
  const S = section(clamp(p[1], -0.127, CROWN), L), sx = clamp(p[0] / S.w, -1, 1), sz = clamp((p[2] - S.cz) / (p[2] >= S.cz ? S.d : S.db), -1, 1);
  const a = Math.sign(sx) * Math.pow(Math.abs(sx), S.e / 2), b = Math.sign(sz) * Math.pow(Math.abs(sz), S.e / 2);
  const phi = Math.atan2(a, b);
  return [0.5 + phi / (2 * PI), (0.119 - p[1]) / 0.25];
}
/** for the painter: texel (u, v) -> the unit point it lands on (base surface with features) and the angle */
export function faceSkinFrame(L) {
  return (u, v) => { const phi = (u - 0.5) * 2 * PI, y = 0.119 - v * 0.25; const p = headPoint(clamp(y, -0.127, CROWN), phi, L); return { x: p[0], y, z: p[2], phi }; };
}

/** bone world positions: Jaw (the hinge in front of the ears), eyes, lids and brows */
export function faceBones(world, sp) {
  const L = faceLayout(world.Head, sp);
  world.Jaw = L.toWorld([0, -0.03, -0.008]);
  for (const [sd, sx] of [['L', 1], ['R', -1]]) {
    const ec = eyeCentre(sx, L);
    world['Eye' + sd] = L.toWorld(ec);
    world['Lid' + sd] = L.toWorld(ec);
    world['Brow' + sd] = L.toWorld([ec[0], 0.024, ec[2] + 0.012]);
  }
}
export const FACE_BONES = ['Jaw', 'LidL', 'LidR', 'BrowL', 'BrowR', 'EyeL', 'EyeR'];
function eyeCentre(sx, L) {
  const key = sx > 0 ? "_ecL" : "_ecR";
  if (L[key]) return L[key];
  const s = surfAt(sx * L.eyeX, 0.0, { ...L, eyeSize: 0.001 }); // the surface over the eye before the socket is carved
  return (L[key] = [sx * L.eyeX, 0.001, s.p[2] - 0.0035 - L.eyeR]);
}

/**
 * ctx: { m (SkinMesh), BI (bone index by name), world, sp }
 * Materials it uses: face (the head's skin, painted by faceSkin), eye_<name>, cavity, teeth, lash, brow, skin (ears)
 */
export function buildHead(ctx) {
  const { m, BI, world, sp } = ctx;
  const L = faceLayout(world.Head, sp), W = L.toWorld;
  const lod = m.lod ?? 1, hi = lod >= 1;
  const headB = BI.Head, jawB = BI.Jaw;
  const ufrom = (pw) => faceUVUnit(L.toUnit(pw), L);

  // ── the head surface: rows (heights) by columns (angles), a slit along the mouth ──
  const ys = [];
  const segs = hi ? [[-0.127, -0.116, 0.0028], [-0.116, -0.096, 0.0017], [-0.096, -0.038, 0.00105], [-0.038, 0.036, 0.0014], [0.036, 0.07, 0.0028], [0.07, CROWN, 0.004]]
    : [[-0.127, -0.096, 0.007], [-0.096, -0.038, 0.004], [-0.038, 0.036, 0.005], [0.036, CROWN, 0.009]];
  for (const [a, b, st] of segs) { const n = Math.max(1, Math.round((b - a) / st)); for (let i = 0; i < n; i++) ys.push(a + ((b - a) * i) / n); }
  ys.push(CROWN - 0.0002);
  // the mouth line gets its own row
  let im = 0; for (let i = 0; i < ys.length; i++) if (Math.abs(ys[i] - L.mouthY) < Math.abs(ys[im] - L.mouthY)) im = i;
  ys[im] = L.mouthY;
  const n = hi ? 176 : 56, a = 0.4;
  const phis = []; for (let j = 0; j <= n; j++) { const s = -1 + (2 * j) / n; phis.push(PI * (a * s + (1 - a) * s * s * s)); }
  // rows near the mouth bend with the mouth line
  const warp = (y, x) => y + (mouthLine(x, L) - L.mouthY) * G((y - L.mouthY) / 0.011, 1);
  const K = ys.length, verts = [], id = [];
  const jawW = (x, y, phi) => { // how much of the jaw a point below the mouth carries
    const ax = Math.abs(x), tw = 0.0022 + 0.022 * ss(L.mouthW * 0.6, L.mouthW * 2.2, ax), yl = mouthLine(x, L);
    let j = ss(yl + 0.0004, yl - tw, y) * ss(2.3, 1.35, Math.abs(phi));
    j = Math.max(j, ss(-0.102, -0.125, y) * 0.7);
    return Math.min(1, j);
  };
  const mk = (p, w, tag) => { verts.push({ p: W(p), n: [0, 0, 0], uv: null, w, tag, unit: p }); return verts.length - 1; };
  const slit = [];
  for (let i = 0; i < K; i++) {
    id.push([]);
    for (let j = 0; j <= n; j++) {
      const phi = phis[j], x0 = basePoint(L.mouthY, phi, L)[0];
      const y = i === im ? mouthLine(x0, L) : warp(ys[i], x0);
      const p = headPoint(y, phi, L);
      const jw = i === im ? 0 : jawW(p[0], y, phi);
      const v = mk(p, [[headB, 1 - jw], [jawB, jw]], y >= L.mouthY - 0.0001 && i >= im ? 'U' : 'D');
      verts[v].uv = [0.5 + phi / (2 * PI), (0.119 - y) / 0.25];
      id[i].push(v);
      if (i === im) { // inside the mouth the row is two rows: the upper lip's edge and the lower lip's
        const inMouth = Math.abs(p[0]) < L.mouthW * 0.985 && Math.cos(phi) > 0.5;
        if (inMouth) { const b = mk(p, [[jawB, 1]], 'D'); verts[b].uv = verts[v].uv; slit[j] = b; }
      }
    }
  }
  const tris = [];
  for (let i = 0; i < K - 1; i++) for (let j = 0; j < n; j++) {
    const lo = (jj) => (i === im && slit[jj] !== undefined ? id[i][jj] : id[i][jj]);
    const up = (jj) => (i + 1 === im && slit[jj] !== undefined ? slit[jj] : id[i + 1][jj]);
    const a0 = lo(j), b0 = lo(j + 1), c0 = up(j + 1), d0 = up(j);
    tris.push([a0, c0, d0], [a0, b0, c0]);
  }
  // normals: area weighted, the seam column (0 and n) shared
  smoothNormals(verts, tris, id.map((r) => [r[0], r[n]]));
  m.raw('face', verts.map((v) => ({ p: v.p, n: v.n, uv: v.uv, w: v.w, tag: 'skin' + v.tag })), tris);

  // ── eyes, lids, lashes ──
  const tagLoft = (mat, rings, o) => m.loft(mat, rings, o);
  for (const [sd, sx] of [['L', 1], ['R', -1]]) {
    const ecU = eyeCentre(sx, L), ec = W(ecU), er = L.eyeR * L.HS, eyeB = BI['Eye' + sd];
    // the eyeball: a sphere with the cornea a little proud of it, textured front on
    const rings = [];
    for (let k = 0; k <= 14; k++) { const th = (k / 14) * PI, r = er * (1 + 0.075 * Math.pow(Math.max(0, Math.cos(th)), 6)); rings.push({ c: add(ec, [0, 0, -Math.cos(th) * r]), U: [1, 0, 0], V: [0, 1, 0], ru: Math.max(0.0004, r * Math.sin(th)), rv: Math.max(0.0004, r * Math.sin(th)) }); }
    rings.reverse();
    m.loft('eye_' + sp.name, rings, { n: hi ? 24 : 10, bone: eyeB, tag: 'eye', uvFn: (p) => [0.5 + (p[0] - ec[0]) / (2.2 * er), 0.5 - (p[1] - ec[1]) / (2.2 * er)] });
    // upper lid: a shell over the eyeball with a rounded margin just over the iris; blink turns it down (morph)
    const lidR = er * 1.1, upR = [];
    for (let i = 0; i <= 10; i++) {
      const u = -1 + (2 * i) / 10, w = Math.sqrt(Math.max(0.03, 1 - u * u * 0.86)), a0 = 0.3 + 0.24 * u * u - 0.05 * u * sx;
      upR.push({ c: add(ec, [u * er * 1.28, 0, 0]), U: [0, 0, 1], V: [0, 1, 0], ru: lidR * w, rv: lidR * w, arc: [a0 - 0.12, 2.25], mod: (an) => 1 + 0.09 * G(an - a0 + 0.02, 0.07) - 0.03 * G(an - a0 - 0.1, 0.05) + 0.04 * G(an - (a0 + 0.62), 0.12) });
    }
    tagLoft('face', upR, { n: hi ? 12 : 6, arc: [0.18, 2.25], bone: headB, tag: 'lidU' + sd, uvFn: ufrom });
    // lower lid
    const loR = [];
    for (let i = 0; i <= 10; i++) { const u = -1 + (2 * i) / 10, w = Math.sqrt(Math.max(0.03, 1 - u * u * 0.86)), a1 = -0.5 - 0.15 * u * u; loR.push({ c: add(ec, [u * er * 1.22, 0, 0]), U: [0, 0, 1], V: [0, 1, 0], ru: er * 1.06 * w, rv: er * 1.06 * w, arc: [-2.0, a1 + 0.06], mod: (an) => 1 + 0.06 * G(an - a1, 0.07) }); }
    tagLoft('face', loR, { n: hi ? 8 : 4, arc: [-2.0, -0.44], bone: headB, tag: 'lidD' + sd, uvFn: ufrom });
    // lashes: a dark strip along the upper margin, curling out
    if (hi) {
      const lashR = [];
      for (let i = 0; i <= 12; i++) {
        const u = -1 + (2 * i) / 12, w = Math.sqrt(Math.max(0.03, 1 - u * u * 0.86)), a0 = 0.3 + 0.24 * u * u - 0.05 * u * sx, L0 = 0.0042 * (1 - 0.5 * u * u) * (1 + 0.4 * u * sx);
        const base = add(ec, [u * er * 1.28, Math.sin(a0) * lidR * w * 1.07, Math.cos(a0) * lidR * w * 1.07]);
        const out = norm([u * 0.3 * sx * 0 + u * 0.25, Math.sin(a0) + 0.4, Math.cos(a0)]);
        lashR.push([base, add(base, mul(out, L0))]);
      }
      const strip = lashR.map(([b0, t0]) => ({ c: b0, U: norm(sub(t0, b0)), V: [0, 0, 1], ru: 0, rv: 0, pts: [b0, t0] }));
      const lv = [], lt = [];
      strip.forEach((s, i) => { const u = i / (strip.length - 1); lv.push({ p: s.pts[0], n: [0, 1, 0.3], uv: [u, 0], w: [[headB, 1]], tag: 'lashU' + sd }, { p: s.pts[1], n: [0, 1, 0.3], uv: [u, 1], w: [[headB, 1]], tag: 'lashU' + sd }); if (i) { const a1 = (i - 1) * 2, b1 = i * 2; lt.push([a1, b1, b1 + 1], [a1, b1 + 1, a1 + 1]); } });
      m.raw('lash', lv, lt);
    }
    // eyebrows: strands lying on the brow, thick and rising at the inner end, thinning and falling to the tail
    if (hi) {
      const N = Math.round(110 * (sp.face?.browThick ?? 1)), arch = sp.face?.browArch ?? 1, thick = sp.face?.browThick ?? 1;
      let seed = 7 + sp.name.length * 13 + (sx > 0 ? 0 : 101);
      const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
      for (let s = 0; s < N; s++) {
        const u = Math.pow(rnd(), 0.85), xb = 0.0105 + 0.0445 * u;
        const yc = 0.0175 + (0.0075 * Math.sin(Math.min(1, u * 1.15) * PI * 0.82) * arch) - 0.0035 * u * u;
        const band = (0.0058 - 0.0034 * u) * thick, yb = yc + (rnd() - 0.5) * band;
        const ang = lerp(1.25, 0.05, Math.min(1, u * 1.6)) - 0.35 * ss(0.6, 1, u) + (rnd() - 0.5) * 0.25; // from the horizontal, toward the tail
        const len = (0.0062 + 0.003 * rnd()) * (u < 0.15 ? 0.8 : 1);
        const lift = 0.0005 + 0.0012 * rnd(), s0 = surfAt(sx * xb, yb, L, lift * 0.5), s1 = surfAt(sx * (xb + Math.cos(ang) * len), yb + Math.sin(ang) * len, L, lift + 0.0006);
        const a = W(s0.p), b = W(s1.p), up = norm(cross(norm(sub(b, a)), s0.n));
        m.cone('brow', a, b, (0.00085 + 0.0003 * rnd()) * L.HS, { ratio: 0.55, U: up, V: s0.n, bone: headB, n: 4, rings: 2, tag: 'brow' + sd, pow: 1 });
      }
    }
  }

  // ── the mouth: a dark interior, teeth on the head and the jaw ──
  const lipZ = surfAt(0, L.mouthY, L).p[2];
  m.ellipsoid('cavity', W([0, L.mouthY - 0.002, lipZ - 0.03]), L.mouthW * 1.05 * L.HS, 0.016 * L.HS, 0.02 * L.HS, { bone: headB, steps: hi ? 7 : 4, n: hi ? 16 : 8, tag: 'mouth' });
  const teeth = (y0, h, bone, up) => {
    const rr = [];
    for (const [dy, k] of [[0, 0.8], [0.25, 1], [0.75, 1], [1, 0.82]]) {
      const yy = y0 + (up ? 1 : -1) * h * dy;
      rr.push({ c: W([0, yy, lipZ - 0.0045 - 0.024]), U: [1, 0, 0], V: [0, 0, 1], ru: 0.0225 * L.HS * k * (L.mouthW / 0.0245), rv: 0.024 * L.HS * k, e: 2.2, mod: (an) => 1 + 0.035 * Math.pow(Math.abs(Math.sin((an - PI / 2) * 9)), 0.4) - 0.035 });
    }
    m.loft('teeth', rr, { n: hi ? 30 : 12, arc: [PI / 2 - 1.15, PI / 2 + 1.15], bone, tag: 'mouth' });
  };
  teeth(L.mouthY + 0.0005, 0.0095, headB, true);
  teeth(L.mouthY - 0.0015, 0.0085, jawB, false);

  // ── ears: a cupped shell with a rolled rim (helix), a lobe, set behind the jaw hinge ──
  for (const sx of [1, -1]) {
    const S = section(-0.016, L), ec = W([sx * S.w * 0.93, -0.016, S.cz - 0.016]);
    const tilt = 0.18, R = (v) => [v[0], v[1] * Math.cos(tilt) - v[2] * Math.sin(tilt), v[1] * Math.sin(tilt) + v[2] * Math.cos(tilt)];
    m.ellipsoid('skin', ec, 0.0075 * L.HS, 0.029 * L.HS, 0.018 * L.HS, { bone: headB, steps: hi ? 8 : 4, n: hi ? 14 : 6, tag: 'ear' });
    const rim = []; for (let i = 0; i <= 14; i++) { const a = -1.0 + (i / 14) * 3.8; rim.push(add(ec, R([sx * 0.0085 * L.HS, Math.sin(a) * 0.031 * L.HS, -Math.cos(a) * 0.019 * L.HS - 0.002 * L.HS]))); }
    for (let i = 0; i < 14; i++) m.capsule('skin', rim[i], rim[i + 1], 0.0042 * L.HS, 0.0042 * L.HS, { bone: headB, n: hi ? 7 : 4, caps: 1, rings: 1, tag: 'ear' });
    m.ellipsoid('skin', add(ec, [sx * 0.004 * L.HS, -0.031 * L.HS, 0.004 * L.HS]), 0.0055 * L.HS, 0.0085 * L.HS, 0.0075 * L.HS, { bone: headB, steps: 5, n: hi ? 10 : 6, tag: 'ear' }); // lobe
    m.ellipsoid('skin', add(ec, [sx * 0.002 * L.HS, 0.004 * L.HS, 0.016 * L.HS]), 0.005 * L.HS, 0.007 * L.HS, 0.004 * L.HS, { bone: headB, steps: 4, n: 8, tag: 'ear' }); // tragus
  }

  // hair and hats follow the bare skull: a ring at t (-1 the chin, 1 the crown) like the old loft, scaled by k plus add
  const ring = (t, k = 1, addR = 0) => {
    const y = clamp(L.hc[1] + L.HY * t, L.yE - 0.127 * L.HS * L.Fl, L.yE + CROWN * L.HS * L.Fl);
    const yu = (y - L.yE) / (L.HS * L.Fl), S = section(clamp(yu, -0.127, CROWN), L);
    return { c: [0, y, L.zoff + S.cz * L.HS], U: [1, 0, 0], V: [0, 0, 1], ru: Math.max(0.002, S.w * L.HS * L.Fw * k + addR), rv: Math.max(0.002, S.d * L.HS * k + addR), e: S.e, mod: () => 1 };
  };
  const surf = (x, y, out = 0) => W(surfAt(x / (L.HS * L.Fw), (y - L.yE) / (L.HS * L.Fl), L, out / L.HS).p);
  return { ring, surf, layout: L };
}

/** area weighted smooth normals; shared: pairs of vertex ids that are one point (a seam) */
function smoothNormals(verts, tris, shared = []) {
  for (const v of verts) v.n = [0, 0, 0];
  for (const [a, b, c] of tris) {
    const f = cross(sub(verts[b].p, verts[a].p), sub(verts[c].p, verts[a].p));
    for (const v of [a, b, c]) { verts[v].n[0] += f[0]; verts[v].n[1] += f[1]; verts[v].n[2] += f[2]; }
  }
  for (const [a, b] of shared) { const s = add(verts[a].n, verts[b].n); verts[a].n = s.slice(); verts[b].n = s.slice(); }
  for (const v of verts) v.n = norm(v.n);
}

// ── expressions as morph targets ──────────────────────────────────────────
export const FACE_TARGETS = ['blink', 'browUp', 'browDown', 'browPinch', 'squint', 'smile', 'snarl', 'shout', 'wince', 'ko', 'jawOpen'];
/** rotate p about the x axis through c by a */
const rotX = (p, c, a) => { const y = p[1] - c[1], z = p[2] - c[2], cs = Math.cos(a), sn = Math.sin(a); return [p[0], c[1] + y * cs - z * sn, c[2] + y * sn + z * cs]; };
/**
 * the displacement (unit metres) of a head vertex for target `name`; p is the vertex in unit coordinates, tag its part
 * (skinU / skinD: the face above / below the mouth line, lidU|lidD|lashU + L|R, brow + L|R, ...)
 */
export function faceTarget(name, p, tag, L) {
  const [x, y, z] = p, ax = Math.abs(x), sgn = x >= 0 ? 1 : -1, mw = L.mouthW;
  const side = tag && /[LR]$/.test(tag) ? (tag.endsWith('L') ? 1 : -1) : sgn;
  const ec = eyeCentre(side, L);
  const lid = tag && tag.startsWith('lidU'), lash = tag && tag.startsWith('lashU'), low = tag && tag.startsWith('lidD'), brow = tag && tag.startsWith('brow');
  const lower = tag === 'skinD';
  const near = (cx, cy, rx, ry) => G(Math.hypot((ax - cx) / rx, (y - cy) / ry), 1);
  let d = [0, 0, 0];
  const addv = (v, k = 1) => { d[0] += v[0] * k; d[1] += v[1] * k; d[2] += v[2] * k; };
  const lidTurn = (a) => { if (lid || lash) addv(sub(rotX(p, ec, a), p)); };
  const lowRaise = (a) => { if (low) addv(sub(rotX(p, ec, -a), p)); };
  // forehead and brow region motion (the skin over the brow carries the strands with it)
  const browField = (dy, dz, pinch) => {
    const k = (brow ? 1 : near(L.eyeX, 0.02, 0.034, 0.016) * (y > 0.004 ? 1 : 0.35)) * (lid || lash || low ? 0.35 : 1);
    const inner = ss(0.05, 0.012, ax);
    addv([-sgn * 0.0028 * pinch * inner, dy + (-0.0032 * pinch * inner), dz], k);
    if (!brow && dy > 0) addv([0, dy * 0.6 * ss(0.02, 0.05, y) * ss(0.11, 0.06, y) * G(x, 0.06), 0]); // the forehead lifts too
  };
  switch (name) {
    case 'blink': lidTurn(1.0); lowRaise(0.12); addv([0, -0.0012 * near(L.eyeX, 0.011, 0.02, 0.006), 0]); break;
    case 'squint': lowRaise(0.32); lidTurn(0.28); addv([0, 0.0018 * near(L.eyeX + 0.006, -0.02, 0.022, 0.012), 0.001 * near(L.eyeX + 0.006, -0.02, 0.022, 0.012)]); break;
    case 'browUp': browField(0.0048, 0.0004, 0); lidTurn(-0.1); break;
    case 'browDown': browField(-0.0042, 0.0012, 0); lidTurn(0.12); break;
    case 'browPinch': browField(0, 0.0006, 1); if (!brow && !lid && !low) addv([0, 0, -0.0012 * Math.pow(Math.abs(Math.sin(x * 260)), 3) * G(y - 0.012, 0.01) * G(x, 0.011)]); break;
    case 'smile': {
      const c = near(mw, L.mouthY + 0.001, 0.016, 0.011), ck = near(0.038, -0.03, 0.02, 0.016);
      addv([sgn * 0.0042 * c, 0.0045 * c, -0.0022 * c]);
      addv([sgn * 0.0012 * ck, 0.0028 * ck, 0.0032 * ck]);
      if (!lid && !brow) addv([0, 0.0016 * near(L.eyeX + 0.003, -0.012, 0.02, 0.008), 0.0006 * near(L.eyeX + 0.003, -0.012, 0.02, 0.008)]);
      lowRaise(0.18);
      if (!lower) addv([0, 0.0008 * near(0, L.mouthY + 0.004, mw, 0.006), -0.0006 * near(0, L.mouthY + 0.004, mw, 0.006)]);
      break;
    }
    case 'snarl': {
      const ul = lower ? 0 : near(0.011, L.mouthY + 0.004, 0.014, 0.01);
      addv([sgn * 0.0006 * ul, 0.0048 * ul, 0.0018 * ul]);
      const nw = near(0.016, -0.046, 0.012, 0.012); addv([sgn * 0.0006 * nw, 0.0024 * nw, 0.0005 * nw]);
      const br = near(0.004, -0.01, 0.012, 0.012); addv([0, 0.0012 * br, -0.0007 * br * Math.abs(Math.sin(y * 700))]);
      const ck = near(0.034, -0.032, 0.018, 0.014); addv([0, 0.0018 * ck, 0.0018 * ck]);
      lowRaise(0.22); browField(-0.0025, 0.0008, 0.6);
      break;
    }
    case 'shout': {
      const c = near(mw, L.mouthY, 0.018, 0.016);
      addv([sgn * 0.0055 * c, -0.0012 * c, -0.0025 * c]);
      if (!lower) { const ul = near(0, L.mouthY + 0.003, mw * 0.95, 0.009); addv([0, 0.0042 * ul, 0.0008 * ul]); }
      else { const ll = near(0, L.mouthY - 0.004, mw * 0.95, 0.012); addv([0, -0.0035 * ll, 0.0006 * ll]); }
      const nw = near(0.016, -0.046, 0.01, 0.01); addv([sgn * 0.0014 * nw, 0.0008 * nw, 0]);
      const ck = near(0.034, -0.034, 0.018, 0.014); addv([0, 0.0014 * ck, 0.0012 * ck]);
      browField(-0.003, 0.001, 0.8); lowRaise(0.14);
      break;
    }
    case 'wince': {
      lidTurn(0.82); lowRaise(0.34);
      const ck = near(0.036, -0.026, 0.02, 0.015); addv([0, 0.0032 * ck, 0.0025 * ck]);
      const c = near(mw, L.mouthY, 0.018, 0.014); addv([sgn * 0.0045 * c, -0.0006 * c, -0.0018 * c]);
      if (!lower) { const ul = near(0.006, L.mouthY + 0.003, mw, 0.008); addv([0, 0.0028 * ul, 0.0006 * ul]); }
      else { const ll = near(0, L.mouthY - 0.004, mw, 0.01); addv([0, -0.0016 * ll, 0]); }
      const nw = near(0.012, -0.04, 0.014, 0.014); addv([0, 0.0018 * nw, 0.0004 * nw]);
      browField(-0.0036, 0.0014, 1);
      break;
    }
    case 'ko': {
      lidTurn(0.92); lowRaise(0.05); browField(0.0016, 0, 0);
      if (lower) { const ll = near(0, L.mouthY - 0.004, mw * 1.1, 0.012); addv([0, -0.0028 * ll, 0.0008 * ll]); }
      const c = near(mw, L.mouthY, 0.012, 0.01); addv([sgn * -0.0008 * c, -0.0018 * c, 0]);
      break;
    }
    case 'jawOpen': { // shapes the lips while the Jaw bone opens: corners in, the lips part a little more
      const c = near(mw, L.mouthY, 0.014, 0.012); addv([-sgn * 0.0028 * c, 0, 0.0008 * c]);
      if (!lower) { const ul = near(0, L.mouthY + 0.002, mw, 0.007); addv([0, 0.0016 * ul, 0.0004 * ul]); }
      break;
    }
  }
  return d;
}

/**
 * Morph targets for the head mesh groups (face, brow, lash) of a SkinMesh: { name: { group: [dpos..., ], nrm: [...] } },
 * with normal deltas from the deformed surface. Vertex positions are in world (rest) space; L from buildHead.
 */
export function faceTargets(m, L, groups = ['face', 'brow', 'lash']) {
  const out = {};
  for (const name of FACE_TARGETS) {
    out[name] = {};
    for (const gname of groups) {
      const g = m.groups.get(gname);
      if (!g || !g.idx.length) continue;
      const nv = g.pos.length / 3, dp = new Float32Array(nv * 3);
      for (let v = 0; v < nv; v++) {
        const tag = g.tag[v];
        if (!tag || tag === 'ear' || tag === 'mouth' || tag === 'eye') continue;
        const pw = [g.pos[v * 3], g.pos[v * 3 + 1], g.pos[v * 3 + 2]], pu = L.toUnit(pw);
        const du = faceTarget(name, pu, tag, L);
        dp[v * 3] = du[0] * L.HS * L.Fw; dp[v * 3 + 1] = du[1] * L.HS * L.Fl; dp[v * 3 + 2] = du[2] * L.HS;
      }
      // normal deltas: the same face-accumulated normals on the rest and the deformed positions, welded where the rest
      // normals agree (seams), never across the mouth slit
      const nrest = accNormals(g.pos, g.idx, g.nrm), pos2 = g.pos.map((x, i) => x + dp[i]), ndef = accNormals(pos2, g.idx, g.nrm);
      const dn = new Float32Array(nv * 3);
      for (let i = 0; i < nv * 3; i++) dn[i] = ndef[i] - nrest[i];
      let any = false; for (let i = 0; i < dp.length; i++) if (Math.abs(dp[i]) > 1e-7) { any = true; break; }
      out[name][gname] = { pos: dp, nrm: dn, any };
    }
  }
  return out;
}
function accNormals(pos, idx, nrm0) {
  const nv = pos.length / 3, N = new Float64Array(nv * 3);
  for (let t = 0; t < idx.length; t += 3) {
    const a = idx[t], b = idx[t + 1], c = idx[t + 2];
    const ux = pos[b * 3] - pos[a * 3], uy = pos[b * 3 + 1] - pos[a * 3 + 1], uz = pos[b * 3 + 2] - pos[a * 3 + 2];
    const vx = pos[c * 3] - pos[a * 3], vy = pos[c * 3 + 1] - pos[a * 3 + 1], vz = pos[c * 3 + 2] - pos[a * 3 + 2];
    const fx = uy * vz - uz * vy, fy = uz * vx - ux * vz, fz = ux * vy - uy * vx;
    for (const v of [a, b, c]) { N[v * 3] += fx; N[v * 3 + 1] += fy; N[v * 3 + 2] += fz; }
  }
  // weld: vertices at one position whose given normals agree share the sum
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

/** an eye texture (front projected): a white sclera with faint vessels toward the corners, a dark limbal ring, an iris
 * with radial fibres, a crypt pattern and a lighter collarette, a round pupil. The wet highlight comes from the
 * material's clear coat (a painted one would not follow the lights). Returns a bake() callback for make-textures. */
export function eyePixel(iris, seed = 1) {
  return (u, v, o) => {
    const x = (u - 0.5) * 2.2, y = (v - 0.5) * 2.2, r = Math.hypot(x, y), a = Math.atan2(y, x);
    o.r = 0.6; o.g = 0.57; o.b = 0.54; o.ro = 0.12; o.h = 0; o.ao = 1 - 0.45 * ss(0.5, 1.0, r);
    const rim = ss(0.45, 1.0, r); o.r -= 0.16 * rim; o.g -= 0.18 * rim; o.b -= 0.16 * rim; // the eyeball darkens toward the lids (they shadow it)
    const vein = Math.pow(Math.abs(Math.sin(a * 17 + seed + 3 * Math.sin(r * 9 + a * 3))), 40) * ss(0.6, 0.95, r);
    o.r -= 0.02 * vein; o.g -= 0.12 * vein; o.b -= 0.1 * vein;
    const ir = 0.45;
    if (r < ir * 1.08) {
      const limb = ss(ir * 0.82, ir * 1.04, r), fib = 0.72 + 0.28 * Math.sin(a * 57 + seed) * Math.sin(a * 11 + r * 30);
      const crypt = 0.85 + 0.15 * Math.sin(a * 23 + seed * 2) * Math.sin(r * 55), coll = 1 + 0.35 * G(r - ir * 0.52, ir * 0.09);
      const k = (0.62 + 0.45 * (1 - r / ir)) * fib * crypt * coll * (1 - 0.75 * limb);
      const m = ss(ir * 1.08, ir * 1.0, r);
      o.r = lerp(o.r, iris[0] * k, m); o.g = lerp(o.g, iris[1] * k, m); o.b = lerp(o.b, iris[2] * k, m); o.h = -0.15 * m;
      const pr = ir * 0.38, pm = ss(pr * 1.06, pr * 0.94, r);
      o.r = lerp(o.r, 0.012, pm); o.g = lerp(o.g, 0.012, pm); o.b = lerp(o.b, 0.015, pm);
    }
  };
}
