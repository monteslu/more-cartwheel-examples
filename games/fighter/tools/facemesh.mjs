// A modelled head and face for skinned characters (used by make-characters.mjs; written to be copied by other games).
//
// What it builds, all on a SkinMesh (charmesh.mjs):
//   - a skull and face loft: brow ridge, eye sockets, temples, cheekbones, a muzzle over the teeth, nasolabial folds,
//     jaw angle and chin per face shape; the lower face (jaw, lower lip, chin) is weighted to a Jaw bone so the mouth opens
//   - eyeballs (sclera, iris, pupil and a wet highlight in their own texture), upper lids on LidL/LidR bones (they blink and
//     narrow), fixed lower lids, a dark lash line on the lid edge
//   - brows as hair strips on BrowL/BrowR bones (they lower and pinch for anger and pain, lift for a shout)
//   - a nose: bridge, tip, the wings of the nostrils and the nostril openings
//   - lips: an upper lip with a bow on the head, a fuller lower lip on the jaw, a dark mouth and two rows of teeth that show
//     when the jaw drops (gritted teeth, a shout, a knockout)
//   - ears: a cupped shell with a rim (helix) and a lobe
// Coordinates: y up, +z the face's front, +x the character's left; sizes scale with HS (head scale).
// faceBones(world, sp) adds the bone positions; buildHead(m, ctx) adds the geometry. The face texture (make-textures
// face() with features: false) paints skin only: pores, colour variation, stubble, scars, age lines, makeup.
import { add, sub, mul, norm } from './charmesh.mjs';

const PI = Math.PI;
const gauss = (x, w) => Math.exp(-(x * x) / (w * w));
const ss = (a, b, x) => { const t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const lerp = (a, b, t) => a + (b - a) * t;
const wrapA = (a) => ((a + PI) % (PI * 2) + PI * 2) % (PI * 2) - PI;
const FRONT = PI / 2, BACK = -PI / 2;

/** the head centre and the face layout for a spec (shared by the bones and the mesh) */
export function faceLayout(headBone, sp) {
  const HS = sp.head ?? 1;
  const hc = [0, headBone[1] + 0.125 * HS, 0.008 * HS];
  const F = sp.face ?? {};
  return {
    HS, hc, R: 0.118 * HS * (F.width ?? 1), RZ: 0.128 * HS, HY: 0.142 * HS * (F.length ?? 1),
    eyeX: 0.034 * HS * (F.eyeSpread ?? 1), eyeY: 0.01 * HS, eyeR: 0.0118 * HS * (F.eyeSize ?? 1),
    mouthY: -0.083 * HS * (F.length ?? 1), mouthW: 0.026 * HS * (F.mouthW ?? 1), noseK: sp.nose ?? 1,
    jawW: sp.jaw ?? 1, brow: F.brow ?? 1, cheek: F.cheek ?? 1, lips: F.lips ?? 1, chin: F.chin ?? 1, age: F.age ?? 0,
  };
}

/** bone world positions: Jaw (the hinge in front of the ears), LidL/R and BrowL/R (at each eye) */
export function faceBones(world, sp) {
  const L = faceLayout(world.Head, sp), { hc, HS } = L;
  world.Jaw = add(hc, [0, -0.035 * HS, -0.01 * HS]);
  for (const [sd, sx] of [['L', 1], ['R', -1]]) {
    const ez = hc[2] + L.RZ * 0.86 - L.eyeR * 0.45;
    world['Lid' + sd] = [sx * L.eyeX, hc[1] + L.eyeY, ez];
    world['Brow' + sd] = [sx * L.eyeX, hc[1] + L.eyeY + 0.026 * HS, ez + 0.01 * HS];
  }
}
export const FACE_BONES = ['Jaw', 'LidL', 'LidR', 'BrowL', 'BrowR'];

/**
 * ctx: { m (SkinMesh), BI (bone index by name), world, sp, faceUV (p -> [u, v]) , lod }
 * Materials it uses: face (skin with the face texture), skin, eye_<name>, lip, cavity, teeth, lash, hair
 */
export function buildHead(ctx) {
  const { m, BI, world, sp } = ctx;
  const L = faceLayout(world.Head, sp), { hc, HS, R, RZ, HY } = L;
  const X = [1, 0, 0], Y = [0, 1, 0], Z = [0, 0, 1];
  const jawB = BI.Jaw, headB = BI.Head;
  // ── the skull and face surface ──
  const tE = L.eyeY / HY, tM = L.mouthY / HY, aE = Math.asin(Math.min(0.9, L.eyeX / R));
  const ring = (t, k = 1, add = 0) => {
    const s = Math.sqrt(Math.max(0, 1 - t * t));
    const jaw = t < -0.15 ? lerp(1, 0.7 + 0.22 * L.jawW, ss(-0.15, -0.95, t)) : 1;
    const zoff = t < -0.25 ? 0.022 * HS * ss(-0.25, -0.9, t) * L.chin : 0;
    return {
      c: [0, hc[1] + HY * t, hc[2] + zoff], U: X, V: Z, ru: Math.max(0.002, R * s * jaw * k + add), rv: Math.max(0.002, RZ * s * (1 - 0.1 * ss(-0.1, -0.4, t)) * k + add),
      e: 2.25 + (0.25 + 0.6 * L.jawW) * ss(-0.2, -0.5, t), // smooth: a step here shows as a crease across the face
      mod: (a) => {
        const f = wrapA(a - FRONT);
        let v = 1;
        v += 0.05 * L.cheek * gauss(t - (tE - 0.22), 0.12) * (gauss(f - 0.85, 0.33) + gauss(f + 0.85, 0.33)); // cheekbones
        v += 0.06 * gauss(t - 0.15, 0.25) * gauss(wrapA(a - BACK), 1.0); // the back of the skull
        v -= 0.05 * gauss(t - tE, 0.075) * (gauss(f - aE, 0.13) + gauss(f + aE, 0.13)); // eye sockets
        v += 0.035 * L.brow * gauss(t - (tE + 0.17), 0.055) * gauss(f, 0.62) * (1 - 0.6 * gauss(f, 0.12)); // brow ridge, a dip over the nose
        v -= 0.03 * gauss(t - (tE + 0.2), 0.14) * (gauss(f - 1.2, 0.25) + gauss(f + 1.2, 0.25)); // temples
        v += 0.022 * gauss(t - (tM + 0.04), 0.16) * gauss(f, 0.38); // the muzzle over the teeth
        v -= 0.012 * gauss(t - (tM + 0.1), 0.12) * (gauss(f - 0.42, 0.07) + gauss(f + 0.42, 0.07)); // nasolabial folds
        v -= 0.014 * gauss(t - (tM - 0.12), 0.05) * gauss(f, 0.3); // the groove under the lower lip
        return v;
      },
    };
  };
  /** the head surface point at (x, y) on the front, and its outward normal (bisection on the ring's angle) */
  const surf = (x, y, out = 0) => {
    const t = (y - hc[1]) / HY, r = ring(t);
    const P = (a) => { const e = r.e, cs = Math.cos(a), sn = Math.sin(a), k = r.mod(a); return [r.ru * Math.sign(cs) * Math.pow(Math.abs(cs), 2 / e) * k, r.c[1], r.c[2] + r.rv * Math.sign(sn) * Math.pow(Math.abs(sn), 2 / e) * k]; };
    let lo = FRONT - 1.4, hi = FRONT + 1.4; // x decreases as a grows from FRONT - 1.4 to FRONT + 1.4
    for (let i = 0; i < 40; i++) { const mid = (lo + hi) / 2; if (P(mid)[0] > x) lo = mid; else hi = mid; }
    const a = (lo + hi) / 2, p = P(a), q = P(a + 0.01);
    const tang = sub(q, p), up = [0, 1, 0], n = norm([tang[1] * up[2] - tang[2] * up[1], tang[2] * up[0] - tang[0] * up[2], tang[0] * up[1] - tang[1] * up[0]]);
    const nn = n[2] < 0 ? mul(n, -1) : n;
    return add(p, mul(nn, out));
  };
  // weights of the face surface: the lower front goes with the jaw
  const faceW = (P) => {
    const t = (P[1] - hc[1]) / HY, front = ss(hc[2] - 0.03 * HS, hc[2] + 0.06 * HS, P[2]);
    const j = ss(tM + 0.02, tM - 0.14, t) * front + ss(-0.55, -0.85, t) * 0.7;
    return [[headB, 1 - Math.min(1, j)], [jawB, Math.min(1, j)]];
  };
  const steps = 34, hr = [];
  for (let k = 0; k <= steps; k++) hr.push(ring(-1 + (2 * k) / steps));
  m.loft('face', hr, { n: 44, weights: (k, P) => faceW(P), uvFn: (p) => ctx.faceUV(p, hc, HS) });
  ctx.ring = ring; ctx.layout = L;

  // ── eyes, lids, lashes, brows ──
  for (const [sd, sx] of [['L', 1], ['R', -1]]) {
    const ex = sx * L.eyeX, ey = hc[1] + L.eyeY, s0 = surf(ex, ey), ez = s0[2] - L.eyeR * 0.5, er = L.eyeR;
    const ec = [ex, ey, ez], lid = BI['Lid' + sd], brow = BI['Brow' + sd];
    m.ellipsoid('eye_' + sp.name, ec, er, er, er, { axis: [0, 0, 1], U: X, V: Y, steps: 10, n: 16, bone: headB, uvFn: (p) => [0.5 + (p[0] - ex) / (2.2 * er), 0.5 - (p[1] - ey) / (2.2 * er)] });
    // upper lid: a shell over the top of the eyeball, its edge just over the iris; the bone turns it down to blink
    const lidR = er * 1.1, rings = [];
    for (let i = 0; i <= 8; i++) { const u = -1 + (2 * i) / 8, w = Math.sqrt(Math.max(0.02, 1 - u * u * 0.85)); rings.push({ c: [ex + u * er * 1.25, ey, ez], U: Z, V: Y, ru: lidR * w, rv: lidR * w, arc: [0.32 + 0.25 * u * u, 2.3] }); }
    m.loft('face', rings, { n: 8, arc: [0.3, 2.3], bone: lid, uvFn: (p) => ctx.faceUV(p, hc, HS) });
    const lash = []; for (let i = 0; i <= 8; i++) { const u = -1 + (2 * i) / 8, w = Math.sqrt(Math.max(0.02, 1 - u * u * 0.85)), a = 0.32 + 0.25 * u * u; lash.push([ex + u * er * 1.25, ey + Math.sin(a) * lidR * w, ez + Math.cos(a) * lidR * w]); }
    for (let i = 0; i < 8; i++) m.capsule('lash', lash[i], lash[i + 1], 0.0016 * HS, 0.0016 * HS, { bone: lid, n: 4, caps: 1, rings: 1 });
    // lower lid
    const low = []; for (let i = 0; i <= 8; i++) { const u = -1 + (2 * i) / 8, w = Math.sqrt(Math.max(0.02, 1 - u * u * 0.85)); low.push({ c: [ex + u * er * 1.2, ey, ez], U: Z, V: Y, ru: er * 1.06 * w, rv: er * 1.06 * w }); }
    m.loft('face', low, { n: 6, arc: [-2.1, -0.42 - 0.1], bone: headB, uvFn: (p) => ctx.faceUV(p, hc, HS) });
    // brow: a tapered strip along the brow ridge, thick at the inner end
    const bpts = []; for (let i = 0; i <= 6; i++) { const u = i / 6, x = sx * (0.011 + 0.052 * u) * HS, y = ey + (0.022 + 0.012 * Math.sin(u * PI * 0.85) - 0.004 * u) * HS * (sp.face?.browArch ?? 1); bpts.push(surf(x, y, 0.002 * HS)); }
    for (let i = 0; i < 6; i++) { const u = i / 6, dir = norm(sub(bpts[i + 1], bpts[i])); m.capsule('brow', bpts[i], bpts[i + 1], (0.0062 - 0.0035 * u) * HS * (sp.face?.browThick ?? 1), (0.0062 - 0.0035 * (u + 1 / 6)) * HS * (sp.face?.browThick ?? 1), { bone: brow, n: 6, caps: 2, rings: 1, ratio: 0.45, U: [0, 1, 0], V: norm([dir[1], -dir[0], 0.6]) }); }
  }

  // ── nose ──
  const nk = L.noseK, nose = (x, y, o = 0) => surf(x, y, o);
  const bridgeTop = nose(0, hc[1] + L.eyeY + 0.008 * HS), tip = add(nose(0, hc[1] - 0.045 * HS), [0, 0, 0.012 * HS * nk]);
  m.capsule('face', add(bridgeTop, [0, 0, -0.004 * HS]), add(tip, [0, 0.006 * HS, -0.008 * HS]), 0.0072 * HS * nk, 0.0095 * HS * nk, { bone: headB, n: 10, caps: 3, rings: 4, ratio: 1.1, U: X, V: Z, uvFn: (p) => ctx.faceUV(p, hc, HS) });
  m.ellipsoid('face', tip, 0.0118 * HS * nk, 0.0098 * HS * nk, 0.0098 * HS, { bone: headB, steps: 6, n: 12, uvFn: (p) => ctx.faceUV(p, hc, HS) });
  for (const sx of [1, -1]) {
    const wing = add(nose(sx * 0.015 * HS * nk, hc[1] - 0.05 * HS), [0, 0, 0.005 * HS]);
    m.ellipsoid('face', wing, 0.0085 * HS * nk, 0.0075 * HS, 0.0095 * HS, { bone: headB, steps: 5, n: 10, uvFn: (p) => ctx.faceUV(p, hc, HS) });
    m.ellipsoid('cavity', add(wing, [-sx * 0.007 * HS * nk, -0.0055 * HS, 0.004 * HS]), 0.0042 * HS, 0.0022 * HS, 0.005 * HS, { bone: headB, steps: 4, n: 8 });
  }

  // ── mouth: dark inside, teeth, lips ──
  const my = hc[1] + L.mouthY, mw = L.mouthW, mz = surf(0, my)[2];
  m.ellipsoid('cavity', [0, my - 0.004 * HS, mz - 0.016 * HS], mw * 0.95, 0.014 * HS, 0.012 * HS, { bone: headB, steps: 5, n: 12 });
  m.capsule('teeth', [-mw * 0.8, my + 0.0035 * HS, mz - 0.006 * HS], [mw * 0.8, my + 0.0035 * HS, mz - 0.006 * HS], 0.0045 * HS, 0.0045 * HS, { bone: headB, n: 8, caps: 2, rings: 6, ratio: 0.6, U: [0, 1, 0], V: [0, 0, 1], mod: (a) => 1 + 0.15 * Math.abs(Math.sin(a * 0)) });
  m.capsule('teeth', [-mw * 0.72, my - 0.006 * HS, mz - 0.008 * HS], [mw * 0.72, my - 0.006 * HS, mz - 0.008 * HS], 0.004 * HS, 0.004 * HS, { bone: jawB, n: 8, caps: 2, rings: 6, ratio: 0.6, U: [0, 1, 0], V: [0, 0, 1] });
  const lip = (y0, lo, bone, full) => {
    const pts = []; for (let i = 0; i <= 8; i++) { const u = -1 + (2 * i) / 8, x = u * mw * 1.05, bow = lo ? 0 : 0.0028 * HS * gauss(Math.abs(u) - 0.25, 0.18); const y = y0 + (lo ? -1 : 1) * 0.0005 * HS + bow - 0.002 * HS * u * u; pts.push(add(surf(x, y), [0, 0, (0.003 - 0.004 * u * u) * HS])); }
    for (let i = 0; i < 8; i++) { const u = Math.abs(-1 + (2 * i + 1) / 8), r = full * (1 - 0.55 * u * u) * HS; m.capsule('lip', pts[i], pts[i + 1], Math.max(0.0015 * HS, r), Math.max(0.0015 * HS, full * (1 - 0.55 * Math.min(1, (u + 0.25) ** 2)) * HS), { bone, n: 8, caps: 2, rings: 1, ratio: 0.85, U: [0, 1, 0], V: [0, 0, 1] }); }
  };
  lip(my + 0.0045 * HS, false, headB, 0.0042 * L.lips);
  lip(my - 0.005 * HS, true, jawB, 0.0052 * L.lips);
  // chin on the jaw
  m.ellipsoid('face', add(surf(0, hc[1] - 0.112 * HS), [0, 0, -0.012 * HS]), 0.03 * HS * L.jawW, 0.022 * HS, 0.018 * HS * L.chin, { bone: jawB, steps: 6, n: 12, uvFn: (p) => ctx.faceUV(p, hc, HS) });

  // ── ears ──
  for (const sx of [1, -1]) {
    const ec = [sx * R * 0.97, hc[1] - 0.004 * HS, hc[2] - 0.016 * HS];
    m.ellipsoid('skin', ec, 0.009 * HS, 0.03 * HS, 0.02 * HS, { bone: headB, steps: 7, n: 12 });
    const rim = []; for (let i = 0; i <= 10; i++) { const a = -0.9 + (i / 10) * 3.6; rim.push([ec[0] + sx * 0.008 * HS, ec[1] + Math.sin(a) * 0.032 * HS, ec[2] - Math.cos(a) * 0.02 * HS - 0.002 * HS]); }
    for (let i = 0; i < 10; i++) m.capsule('skin', rim[i], rim[i + 1], 0.0045 * HS, 0.0045 * HS, { bone: headB, n: 6, caps: 1, rings: 1 });
    m.ellipsoid('skin', [ec[0] + sx * 0.004 * HS, ec[1] - 0.03 * HS, ec[2] + 0.004 * HS], 0.006 * HS, 0.009 * HS, 0.008 * HS, { bone: headB, steps: 5, n: 8 }); // lobe
  }
  return { ring, surf, layout: L };
}

/** an eye texture (front projected): sclera with soft veins and a darker rim, an iris with radial fibres, a pupil, a wet
 * highlight. Returns a bake() callback for make-textures. */
export function eyePixel(iris, seed = 1) {
  return (u, v, o) => {
    const x = (u - 0.5) * 2.2, y = (v - 0.5) * 2.2, r = Math.hypot(x, y), a = Math.atan2(y, x);
    o.r = 0.92; o.g = 0.89; o.b = 0.86; o.ro = 0.08; o.h = 0; o.ao = 1;
    const rim = ss(0.6, 1.0, r); o.r -= 0.2 * rim; o.g -= 0.24 * rim; o.b -= 0.22 * rim; // the sclera darkens toward the sides
    o.g -= 0.05 * rim * (0.5 + 0.5 * Math.sin(a * 23 + seed)); o.b -= 0.05 * rim * (0.5 + 0.5 * Math.sin(a * 31));
    const ir = 0.42;
    if (r < ir) {
      const fib = 0.75 + 0.25 * Math.sin(a * 60 + seed) * Math.sin(a * 13), ring = ss(ir * 0.7, ir, r);
      const k = (0.65 + 0.5 * (1 - r / ir)) * fib * (1 - 0.5 * ring);
      o.r = iris[0] * k; o.g = iris[1] * k; o.b = iris[2] * k; o.h = -0.2;
      if (r < ir * 0.42) { o.r = o.g = o.b = 0.015; }
    }
    if (Math.hypot(x + 0.14, y + 0.16) < 0.09) { o.r = o.g = o.b = 1; } // the wet highlight
  };
}
