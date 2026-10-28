// Generates the four fighters as skinned glb files (one bone layout, per-fighter proportions and outfits) into
// assets/models/. Run after make-textures.mjs:
//   node tools/make-characters.mjs [name ...]
// Facing is +Z in the model, +X is the fighter's left. The game poses every bone itself from the simulation (chars.c),
// so the files carry no clips: only the rest skeleton, the skinned meshes and their materials.
// Bones the game drives with springs (HairA, HairB, CoatL, CoatR, Belly) and the finger bones (Fingers1/2, Thumb per hand,
// curled into a fist or opened by the pose) are part of the same skeleton.
// The skeleton matches the joint layout the simulation's hitboxes were tuned against (the old chars.c REST table):
// arm and leg joint positions, shoulder width and hip width per fighter are the same numbers.
import { mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb } from './gltf-writer.mjs';
import { SkinMesh, add, sub, mul, norm, cross, dot } from './charmesh.mjs';
import { faceSkin, bake } from './make-textures.mjs';
import { buildHead, faceBones, FACE_BONES, eyePixel, faceLayout, faceSkinFrame, headSection, faceTargets, FACE_TARGETS, lipMasks } from './facemesh.mjs';
import { writeSet } from './texlib.mjs';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'models');
mkdirSync(OUT, { recursive: true });

// ── the skeleton ──
// each hand: four fingers in two bones (the knuckle, and the middle joint carrying the end) and a thumb in two
const FINGERS = ['Index', 'Middle', 'Ring', 'Pinky'];
// [across the palm (z), knuckle below the wrist, first length, second length, radius]
const FING = [[0.027, -0.090, 0.040, 0.046, 0.0098], [0.009, -0.094, 0.045, 0.050, 0.0102], [-0.009, -0.092, 0.042, 0.047, 0.0096], [-0.026, -0.086, 0.033, 0.037, 0.0086]];
const FINGER_BONES = [], FINGER_PARENT = {};
for (const sd of ['L', 'R']) { for (const f of FINGERS) { FINGER_BONES.push(f + '1' + sd, f + '2' + sd); FINGER_PARENT[f + '1' + sd] = 'Hand' + sd; FINGER_PARENT[f + '2' + sd] = f + '1' + sd; } FINGER_BONES.push('Thumb' + sd, 'Thumb2' + sd); FINGER_PARENT['Thumb' + sd] = 'Hand' + sd; FINGER_PARENT['Thumb2' + sd] = 'Thumb' + sd; }
const BONE_NAMES = ['Hips', 'Spine', 'Chest', 'Neck', 'Head', 'UpperArmL', 'LowerArmL', 'HandL', 'UpperArmR', 'LowerArmR', 'HandR',
  'UpperLegL', 'LowerLegL', 'FootL', 'UpperLegR', 'LowerLegR', 'FootR', 'Belly', 'HairA', 'HairB', 'CoatL', 'CoatR',
  ...FINGER_BONES, ...FACE_BONES];
const PARENT = { Hips: -1, Spine: 'Hips', Chest: 'Spine', Neck: 'Chest', Head: 'Neck', UpperArmL: 'Chest', LowerArmL: 'UpperArmL', HandL: 'LowerArmL',
  UpperArmR: 'Chest', LowerArmR: 'UpperArmR', HandR: 'LowerArmR', UpperLegL: 'Hips', LowerLegL: 'UpperLegL', FootL: 'LowerLegL',
  UpperLegR: 'Hips', LowerLegR: 'UpperLegR', FootR: 'LowerLegR', Belly: 'Spine', HairA: 'Head', HairB: 'HairA', CoatL: 'Hips', CoatR: 'Hips',
  ...FINGER_PARENT,
  Jaw: 'Head', LidL: 'Head', LidR: 'Head', BrowL: 'Head', BrowR: 'Head', EyeL: 'Head', EyeR: 'Head' };
const BI = Object.fromEntries(BONE_NAMES.map((n, i) => [n, i]));

// joint heights (metres, before the fighter's height scale): the table the hitboxes were drawn against
const RY = { Hips: 0.95, Spine: 1.05, Chest: 1.28, Neck: 1.50, Head: 1.57, Sh: 1.43, El: 1.13, Wr: 0.86, Hip: 0.93, Kn: 0.50, An: 0.07 };
/** world positions of every bone, and translations relative to the parent */
function skeleton(sp) {
  const sc = sp.scale, sw = 0.2 * sp.shoulder, hw = 0.1 * (0.85 + 0.3 * sp.bulk), HS = sp.head ?? 1;
  const hand = sp.hand ?? 1;
  const hair = sp.hairRoot ?? [0, 0.17, -0.1];
  const world = {
    Hips: [0, RY.Hips * sc, 0], Spine: [0, RY.Spine * sc, 0], Chest: [0, RY.Chest * sc, 0], Neck: [0, RY.Neck * sc, 0], Head: [0, RY.Head * sc, 0],
    UpperArmL: [sw, RY.Sh * sc, 0], LowerArmL: [sw, RY.El * sc, 0], HandL: [sw, RY.Wr * sc, 0],
    UpperArmR: [-sw, RY.Sh * sc, 0], LowerArmR: [-sw, RY.El * sc, 0], HandR: [-sw, RY.Wr * sc, 0],
    UpperLegL: [hw, RY.Hip * sc, 0], LowerLegL: [hw, RY.Kn * sc, 0], FootL: [hw, RY.An * sc, 0],
    UpperLegR: [-hw, RY.Hip * sc, 0], LowerLegR: [-hw, RY.Kn * sc, 0], FootR: [-hw, RY.An * sc, 0],
  };
  world.Belly = add(world.Spine, [0, 0, 0.1]);
  world.HairA = add(world.Head, [hair[0], hair[1] * HS, hair[2] * HS]);
  world.HairB = add(world.HairA, sp.hairB ?? [0, -0.15, -0.03]);
  world.CoatL = add(world.Hips, [0.11, -0.04, -0.1]);
  world.CoatR = add(world.Hips, [-0.11, -0.04, -0.1]);
  for (const sd of ['L', 'R']) { // knuckles 9.5 cm below the wrist, the middle finger joint 4.5 cm further; the thumb root on the palm's front
    const sx = sd === 'L' ? 1 : -1, w = world['Hand' + sd];
    FINGERS.forEach((f, i) => { const [fz, ky, l1] = FING[i]; world[f + '1' + sd] = add(w, [-sx * 0.005 * hand, ky * hand, fz * hand]); world[f + '2' + sd] = add(world[f + '1' + sd], [0, -l1 * hand, 0.001 * hand]); });
    world['Thumb' + sd] = add(w, [-sx * 0.012 * hand, -0.028 * hand, 0.03 * hand]);
    world['Thumb2' + sd] = add(world['Thumb' + sd], [-sx * 0.012 * hand, -0.03 * hand, 0.022 * hand]);
  }
  faceBones(world, sp);
  const T = {};
  for (const n of BONE_NAMES) T[n] = PARENT[n] === -1 ? world[n] : sub(world[n], world[PARENT[n]]);
  return { T, world };
}
const lerp = (a, b, t) => a + (b - a) * t;
const EASE = { io: (u) => u * u * (3 - 2 * u) };
const PI = Math.PI;

// ── the body ────────────────────────────────────────────────────────────
const hexRaw = (h) => [((h >> 16) & 255) / 255, ((h >> 8) & 255) / 255, (h & 255) / 255];
const hex = (h) => hexRaw(h).map((c) => Math.pow(c, 2.2));
const TEX = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'textures');
const ss = (a, b, x) => { const t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const gauss = (x, w) => Math.exp(-(x * x) / (w * w));
const G = gauss;
const wrapA = (a) => { a = ((a + PI) % (PI * 2) + PI * 2) % (PI * 2) - PI; return a; }; // -PI..PI
const FRONT = PI / 2, BACK = -PI / 2; // loft angles: +Z is the front
/** a soft fold pattern: narrow ridges at the given heights, stronger on one side (angle) */
const folds = (y, a, list) => { let m = 0; for (const [fy, amp, w, side, sw] of list) m += amp * gauss(y - fy, w) * (side == null ? 1 : 0.35 + 0.65 * gauss(wrapA(a - side), sw ?? 1.2)); return m; };
const noise1 = (x, s) => { const i = Math.floor(x), f = x - i, h = (n) => { let v = Math.imul(n ^ s, 0x27d4eb2d); v ^= v >>> 15; v = Math.imul(v, 0x85ebca6b); v ^= v >>> 13; return (v >>> 0) / 4294967296; }; const u = f * f * (3 - 2 * f); return lerp(h(i), h(i + 1), u); };

function build(sp) {
  const m = new SkinMesh();
  if (sp.lod) m.lod = sp.lod;
  const { world } = skeleton(sp);
  const B = (n) => world[n];
  const X = [1, 0, 0], Y = [0, 1, 0], Z = [0, 0, 1];
  const W = sp.width ?? 1, AR = sp.arm ?? 1, LG = sp.leg ?? 1, HS = sp.head ?? 1, HN = sp.hand ?? 1.1, belly = sp.belly ?? 0, chest = sp.chest ?? 1, bust = sp.bust ?? 0, abs = sp.abs ?? 0;
  const sw = B("UpperArmL")[0], hw = B("UpperLegL")[0];
  const yH = B('Hips')[1], yS = B('Spine')[1], yC = B('Chest')[1], yN = B('Neck')[1], yHd = B('Head')[1];
  const ySh = B('UpperArmL')[1], yEl = B('LowerArmL')[1], yWr = B('HandL')[1];
  const yHip = B('UpperLegL')[1], yKn = B('LowerLegL')[1], yAn = B('FootL')[1];

  // ── skin weights by position ──
  const torsoW = (P, a) => {
    const y = P[1], ax = Math.abs(P[0]), sd = P[0] >= 0 ? 'L' : 'R';
    let w = [];
    const tHS = ss(yH + 0.02, yS + 0.04, y), tSC = ss(yS + 0.04, yC + 0.02, y), tCN = ss(yN - 0.03, yN + 0.04, y);
    w.push([BI.Hips, 1 - tHS], [BI.Spine, tHS * (1 - tSC)], [BI.Chest, tSC * (1 - tCN)], [BI.Neck, tCN]);
    // the belly moves with its spring bone
    if (belly > 0) { const kb = 0.85 * gauss(y - (yS - 0.02), 0.12) * Math.pow(Math.max(0, Math.sin(a)), 2); w = w.map(([b, x]) => [b, x * (1 - kb)]); w.push([BI.Belly, kb]); }
    // shoulders follow the arm a little; the seat follows the thighs a little
    const ka = 0.55 * ss(sw - 0.08, sw + 0.02, ax) * ss(ySh - 0.12, ySh - 0.02, y) * (1 - ss(ySh + 0.05, ySh + 0.1, y) * 0.5);
    if (ka > 0) { w = w.map(([b, x]) => [b, x * (1 - ka)]); w.push([BI['UpperArm' + sd], ka]); }
    const kl = 0.45 * ss(yHip + 0.05, yHip - 0.08, y) * ss(0.02, 0.1, ax);
    if (kl > 0) { w = w.map(([b, x]) => [b, x * (1 - kl)]); w.push([BI['UpperLeg' + sd], kl]); }
    return w;
  };
  const armW = (P) => {
    const y = P[1], sd = P[0] >= 0 ? 'L' : 'R';
    const a = ss(ySh + 0.07, ySh - 0.05, y), b = ss(yEl + 0.04, yEl - 0.04, y), c = ss(yWr + 0.03, yWr - 0.02, y);
    return [[BI.Chest, 1 - a], [BI['UpperArm' + sd], a * (1 - b)], [BI['LowerArm' + sd], a * b * (1 - c)], [BI['Hand' + sd], a * b * c]];
  };
  const legW = (P) => {
    const y = P[1], sd = P[0] >= 0 ? 'L' : 'R';
    const a = ss(yHip + 0.07, yHip - 0.08, y), b = ss(yKn + 0.05, yKn - 0.05, y), c = ss(yAn + 0.06, yAn + 0.0, y);
    return [[BI.Hips, 1 - a], [BI['UpperLeg' + sd], a * (1 - b)], [BI['LowerLeg' + sd], a * b * (1 - c)], [BI['Foot' + sd], a * b * c]];
  };
  const headW = (P) => { const t = ss(yHd - 0.02, yHd + 0.05, P[1]); return [[BI.Neck, 1 - t], [BI.Head, t]]; };
  const tailW = (P) => { // coat skirts: hips at the waist, the spring bones toward the hem, a share of the thigh
    const y = P[1], sd = P[0] >= 0 ? 'L' : 'R', t = ss(yH - 0.02, yKn + 0.05, y), back = P[2] < 0 ? 1 : 0.3;
    return [[BI.Hips, 1 - t], [BI['Coat' + sd], t * 0.75 * back], [BI['UpperLeg' + sd], t * (1 - 0.75 * back)]];
  };

  // ── the torso: rings from the crotch to the neck; pecs, glutes, belly, shoulder mass by angle ──
  const bw = 0.78 + 0.22 * W, bd = 0.85 + 0.15 * W, sws = sw / 0.2;
  // [y, half width, half depth, z offset, superellipse]
  const PROF = [[yHip - 0.08, 0.04, 0.045, 0, 2], [yHip - 0.05, 0.13, 0.095, 0, 2.2], [yHip, 0.172, 0.115, -0.005, 2.4], [yHip + 0.06, 0.18, 0.122, -0.008, 2.5],
    [yH + 0.07, 0.168, 0.112, -0.004, 2.5], [yS + 0.03, 0.152, 0.104, 0, 2.5], [yS + 0.11, 0.165, 0.112, 0.004, 2.6], [yC + 0.02, 0.19, 0.124, 0.008, 2.7],
    [yC + 0.09, 0.2, 0.13, 0.008, 2.8], [ySh - 0.05, 0.205, 0.124, 0.002, 3.0], [ySh, 0.185, 0.11, -0.004, 3.2], [ySh + 0.04, 0.145, 0.094, -0.01, 3.0],
    [yN + 0.0, 0.085, 0.068, -0.012, 2.4], [yN + 0.035, 0.064, 0.062, -0.01, 2.2]];
  for (let i = 1; i < PROF.length; i++) if (PROF[i][0] < PROF[i - 1][0] + 0.008) PROF[i][0] = PROF[i - 1][0] + 0.008; // the fighters' joints sit closer than the profile assumes
  const hipK = sp.hips ?? 1; for (const p of PROF) if (p[0] < yS) { p[1] *= hipK; }
  const prof = (y) => { // interpolate the profile at y
    let i = 0; while (i < PROF.length - 2 && PROF[i + 1][0] < y) i++;
    const A = PROF[i], Bq = PROF[i + 1], t = EASE.io(Math.max(0, Math.min(1, (y - A[0]) / (Bq[0] - A[0]))));
    return A.map((v, k) => lerp(v, Bq[k], t));
  };
  const torsoMod = (y, extra = 0, foldList = null) => (a) => {
    let k = 1 + extra;
    const s = Math.sin(a), c = Math.cos(a);
    const front = Math.max(0, s), back = Math.max(0, -s);
    { const yp = yC + 0.07, pv = y < yp ? gauss(y - yp, 0.028) : gauss(y - yp, 0.075); k += 0.085 * chest * pv * (gauss(wrapA(a - (FRONT - 0.5)), 0.42) + gauss(wrapA(a - (FRONT + 0.5)), 0.42)); } // pecs: a shelf with a crisp lower edge
    k -= 0.018 * (chest - 0.8) * gauss(wrapA(a - FRONT), 0.07) * gauss(y - (yC + 0.06), 0.07); // the sternum line between them
    k += 0.08 * gauss(y - (yHip + 0.03), 0.06) * (gauss(wrapA(a - (BACK - 0.5)), 0.5) + gauss(wrapA(a - (BACK + 0.5)), 0.5)); // seat
    k += belly * 0.3 * gauss(y - (yS - 0.01), 0.1) * Math.pow(front, 2.5); // belly
    k += 0.05 * gauss(y - (yC + 0.03), 0.07) * Math.pow(Math.abs(c), 3); // lats
    k -= 0.025 * gauss(y - yC, 0.15) * gauss(wrapA(a - BACK), 0.18); // the spine groove
    if (bust) k += bust * 0.16 * gauss(y - (yC + 0.035), 0.055) * (gauss(wrapA(a - (FRONT - 0.42)), 0.32) + gauss(wrapA(a - (FRONT + 0.42)), 0.32));
    if (abs) { const f = gauss(wrapA(a - FRONT), 0.42), band = y > yH - 0.02 && y < yC - 0.03 ? Math.pow(Math.abs(Math.sin((y - yH) / (yC - yH) * PI * 3.2)), 0.6) : 0; k += abs * 0.018 * f * band - abs * 0.012 * gauss(wrapA(a - FRONT), 0.05) * ss(yH, yS, y) * ss(yC, yC - 0.05, y); }
    if (foldList) k += folds(y, a, foldList);
    return k;
  };
  // below the ribs the width follows the build; from the chest up it follows the shoulders, so arms never sink into it
  const ringAt = (y, k = 1, kz = 1, add = 0, mod = null) => { const p = prof(y); const u = ss(yS + 0.05, yC + 0.06, y); const wk = lerp(bw, sws * (0.92 + 0.08 * chest), u); return { c: [0, y, p[3]], U: X, V: Z, ru: p[1] * wk * k + add, rv: p[2] * bd * (1 + (chest - 1) * 0.6 * u) * kz + add, e: p[4], mod }; };
  const ys = (y0, y1, step = 0.025) => { const n = Math.max(1, Math.round((y1 - y0) / step)), r = []; for (let i = 0; i <= n; i++) r.push(y0 + ((y1 - y0) * i) / n); return r; };

  const top = sp.top ?? 'tee', bottom = sp.bottom ?? 'jeans';
  const waist = yH + 0.06; // where trousers end and the top begins
  const shirtMat = sp.shirtMat ?? 'shirt';
  const tuck = sp.tuck ?? false;
  // trousers over the pelvis
  const pantsFolds = [[waist - 0.03, 0.02, 0.012, FRONT, 1.0], [yHip - 0.02, 0.025, 0.015, FRONT, 0.6]];
  m.loft(bottom === 'none' ? 'skin' : 'pants', ys(PROF[0][0], waist).map((y) => ringAt(y, 1.04, 1.05, 0.006, torsoMod(y, 0, pantsFolds))), { n: 32, weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
  // the upper body: a shirt from the waist up (bare skin for none), with a hem that flares and waist folds
  const shirtTop = top === 'tank' ? ySh - 0.02 : yN + 0.0;
  const hemY = tuck ? waist : waist - 0.06;
  const shirtFolds = [[hemY + 0.06, 0.018, 0.012, FRONT, 1.2], [hemY + 0.12, 0.014, 0.012, FRONT + 0.6, 0.6], [hemY + 0.1, 0.012, 0.012, FRONT - 0.7, 0.6], [yC - 0.02, 0.01, 0.015, null]];
  if (top === 'none') m.loft('skin', ys(waist - 0.01, yN + 0.035).map((y) => ringAt(y, 1, 1, 0, torsoMod(y))), { n: 32, weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
  else {
    const rings = ys(hemY, shirtTop).map((y) => ringAt(y, 1.05, 1.06, 0.01, torsoMod(y, 0, shirtFolds)));
    rings.unshift({ ...ringAt(hemY, 1.08, 1.1, 0.012, torsoMod(hemY)), c: [0, hemY - 0.005, prof(hemY)[3]] });
    m.loft(shirtMat, rings, { n: 32, weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
    if (top === 'tank') { // straps over the shoulders and skin above
      m.loft('skin', ys(shirtTop - 0.02, yN + 0.035).map((y) => ringAt(y, 1, 1, 0, torsoMod(y))), { n: 32, weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
      for (const ac of [0.82, -0.82, PI - 0.82, -PI + 0.82]) // straps: front and back, left and right
        m.loft(shirtMat, ys(shirtTop - 0.03, ySh + 0.05, 0.012).map((y) => ringAt(y, 1, 1, 0.007, torsoMod(y))), { n: 3, arc: [ac - 0.11, ac + 0.11], weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    } else { // a collar
      m.loft(shirtMat, [ringAt(yN - 0.005, 1.0, 1.0, 0.012), ringAt(yN + 0.025, 1.0, 1.0, 0.016), ringAt(yN + 0.04, 0.96, 0.96, 0.012)], { n: 24, weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    }
  }
  if (sp.obi) { // a wide cloth belt wrapped twice, a square knot at the front and two hanging ends
    const yb = waist - 0.035;
    m.loft('belt', [ringAt(yb - 0.035, 1.16, 1.2, 0.03), ringAt(yb - 0.03, 1.19, 1.23, 0.034), ringAt(yb + 0.03, 1.19, 1.23, 0.034), ringAt(yb + 0.035, 1.16, 1.2, 0.03)].map((r) => ({ ...r, mod: (a) => 1 + 0.012 * Math.sin(a * 9) })), { n: 36, weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    const zf = prof(yb)[2] * bd * 1.23 + 0.04 + prof(yb)[3];
    m.ellipsoid('belt', [0, yb, zf], 0.04, 0.036, 0.022, { bone: BI.Hips, steps: 5, n: 12, e: 2.6 });
    for (const sd of [1, -1]) m.capsule('belt', [sd * 0.02, yb - 0.02, zf], [sd * 0.07, yb - 0.24, zf + 0.025], 0.03, 0.026, { weights: (t) => [[BI.Hips, 1 - 0.6 * t], [BI['UpperLeg' + (sd > 0 ? 'L' : 'R')], 0.6 * t]], n: 10, caps: 2, rings: 5, ratio: 0.22, U: [1, 0, 0], V: [0, 0, 1] });
  }
  if (sp.harness) { // two leather straps crossing the chest and back with a metal ring where they meet
    const pts = (from, to, front) => { const r = []; for (let i = 0; i <= 14; i++) { const t = i / 14, y = lerp(from[0], to[0], t), an = lerp(from[1], to[1], t) + (front ? 0 : PI); const R = ringAt(y, 1, 1, 0.012, torsoMod(y)); const e = R.e; const cs = Math.cos(an), sn = Math.sin(an); const sx = Math.sign(cs) * Math.pow(Math.abs(cs), 2 / e), sz = Math.sign(sn) * Math.pow(Math.abs(sn), 2 / e); const k = R.mod(an); r.push([R.ru * sx * k, y, R.c[2] + R.rv * sz * k]); } return r; };
    for (const front of [true, false]) for (const sd of [1, -1]) {
      const p = pts([ySh - 0.01, PI / 2 + sd * 0.62], [waist + 0.01, PI / 2 - sd * 1.0], front);
      for (let i = 0; i + 1 < p.length; i++) { const dir = norm(sub(p[i + 1], p[i])); const nrm = norm([p[i][0], 0, p[i][2]]); const U = norm([dir[1] * nrm[2] - dir[2] * nrm[1], dir[2] * nrm[0] - dir[0] * nrm[2], dir[0] * nrm[1] - dir[1] * nrm[0]]); m.capsule('harness', p[i], p[i + 1], 0.024, 0.024, { weights: (t, P, a) => torsoW(P, Math.atan2(P[2], P[0])), n: 6, caps: 1, rings: 2, ratio: 0.22, U, V: nrm }); }
    }
    const yc = lerp(ySh + 0.03, waist, 0.5), R0 = ringAt(yc, 1, 1, 0.024, torsoMod(yc));
    m.ellipsoid('metal', [0, yc, R0.c[2] + R0.rv * R0.mod(PI / 2)], 0.034, 0.034, 0.012, { bone: BI.Spine, steps: 5, n: 14 });
  }
  if (sp.belt) {
    m.loft('belt', [ringAt(waist - 0.035, 1.07, 1.08, 0.016), ringAt(waist - 0.03, 1.09, 1.1, 0.02), ringAt(waist + 0.005, 1.09, 1.1, 0.02), ringAt(waist + 0.01, 1.07, 1.08, 0.016)], { n: 32, weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    const p = prof(waist - 0.012);
    if (sp.champ) { m.ellipsoid('metal', [0, waist - 0.012, p[2] * bd * 1.1 + 0.03 + p[3]], 0.13, 0.085, 0.016, { bone: BI.Hips, steps: 6, n: 20, e: 2.6 }); m.ellipsoid('buckle', [0, waist - 0.012, p[2] * bd * 1.1 + 0.043 + p[3]], 0.07, 0.045, 0.01, { bone: BI.Hips, steps: 5, n: 16, e: 2.2 }); for (const sd of [1, -1]) m.ellipsoid('metal', [sd * 0.17, waist - 0.012, p[2] * bd * 0.95 + 0.02 + p[3]], 0.04, 0.05, 0.012, { bone: BI.Hips, steps: 5, n: 12, e: 2.6 }); }
    else m.ellipsoid('buckle', [0, waist - 0.012, p[2] * bd * 1.1 + 0.022 + p[3]], 0.032, 0.024, 0.008, { bone: BI.Hips, steps: 4, n: 10, e: 3 });
  }
  // open garments over the top: jackets, vests, coats (double sided, the front left open), armour plates
  const outer = sp.outer;
  if (outer) {
    const open = sp.openFront ?? 0.38, kind = sp.outerKind ?? 'jacket';
    const y0 = kind === 'gi' ? waist - 0.17 : kind === 'crop' ? yS + 0.07 : kind === 'vest' || kind === 'jacket' || kind === 'hivis' ? waist - 0.07 : waist - 0.02;
    const y1 = ySh + 0.04;
    const of = [[y0 + 0.08, 0.02, 0.014, FRONT, 0.8], [yC - 0.05, 0.016, 0.02, FRONT - 0.9, 0.5], [yC - 0.02, 0.016, 0.02, FRONT + 0.9, 0.5], [y0 + 0.2, 0.012, 0.015, BACK, 1]];
    if (kind === 'gi') { // a wrapped jacket: closed at the belt, opening into a V up to the neck, skirt over the hips, a thick lapel band along the opening
      const gf = [...of, [waist + 0.03, 0.03, 0.012, null], [waist - 0.03, 0.025, 0.014, null], [yC + 0.02, 0.02, 0.02, FRONT + 1.4, 0.4], [yC + 0.02, 0.02, 0.02, FRONT - 1.4, 0.4]];
      const openAt = (y) => lerp(0.05, open, ss(waist, yC + 0.1, y)) + 0.1 * ss(yC + 0.05, yN, y);
      const rr = ys(y0, ySh + 0.04).map((y) => { const yy = Math.max(y, PROF[1][0] + 0.02), skirt = ss(waist - 0.02, y0, y); const r = ringAt(yy, 1.12 + 0.12 * skirt, 1.15 + 0.12 * skirt, 0.026 + 0.012 * ss(yC, y0, y), torsoMod(yy, 0, gf)); r.c = [0, y, r.c[2]]; r.arc = [FRONT + openAt(y), FRONT - openAt(y) + PI * 2]; if (y < waist) { const lr = 0.078 * LG * (0.8 + 0.2 * W) * 1.1 + 0.045; r.ru = Math.max(r.ru, (hw + lr) * (1 + 0.12 * skirt)); r.rv = Math.max(r.rv, lr * 1.25 + 0.01); } return r; });
      rr.push({ ...ringAt(yN + 0.005, 1.06, 1.06, 0.024), c: [0, yN + 0.005, prof(yN)[3] - 0.012], arc: [FRONT + openAt(yN) + 0.1, FRONT - openAt(yN) - 0.1 + PI * 2] });
      m.loft('outer', rr, { n: 40, weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
      for (const side of [1, -1]) { // the lapels: a strip on each edge of the opening, standing a little proud
        const lr = rr.map((r) => ({ ...r, ru: r.ru + 0.006, rv: r.rv + 0.006, arc: side > 0 ? [r.arc[0] - 0.02, r.arc[0] + 0.24] : [r.arc[1] - 0.24, r.arc[1] + 0.02] })).filter((r) => r.c[1] > waist - 0.03);
        m.loft('outer', lr, { n: 5, weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
      }
    } else {
    const rings = ys(y0, y1).map((y) => ringAt(y, 1.1, 1.13, 0.024 + 0.01 * ss(yC, y0, y), torsoMod(y, 0, of)));
    rings.push({ ...ringAt(yN + 0.01, 1.02, 1.02, 0.02), c: [0, yN + 0.01, prof(yN)[3] - 0.01] });
    rings.push({ ...ringAt(yN + 0.055, 1.25, 1.25, 0.03), c: [0, yN + 0.055, prof(yN)[3] - 0.015] }); // the collar stands and rolls out
    m.loft('outer', rings, { n: 36, arc: [FRONT + open, FRONT - open + PI * 2], weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
    if (kind === 'crop') for (const sd of [1, -1]) m.loft('trim', ys(y0 - 0.005, ySh - 0.02, 0.03).map((y) => ringAt(y, 1.1, 1.13, 0.027 + 0.01 * ss(yC, y0, y), torsoMod(y, 0, of))), { n: 2, arc: sd > 0 ? [FRONT + open, FRONT + open + 0.07] : [FRONT - open - 0.07 + PI * 2, FRONT - open + PI * 2], weights: (k, P, a) => torsoW(P, a), tile: 0.3 }); // glowing piping on the edges
    }
    if (kind === 'hivis') for (const y of [yS + 0.06, yC + 0.06]) m.loft('reflect', [ringAt(y - 0.018, 1.1, 1.13, 0.028), ringAt(y + 0.018, 1.1, 1.13, 0.028)].map((r) => ({ ...r, mod: torsoMod(r.c[1]) })), { n: 36, arc: [FRONT + open, FRONT - open + PI * 2], weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    if (kind === 'longcoat' || kind === 'tailcoat') { // skirts below the waist (tails are split at the back)
      const yb = kind === 'tailcoat' ? yKn + 0.02 : yKn - 0.1;
      const rr = ys(yb, waist - 0.01, 0.03).reverse().map((y) => { const t = ss(waist, yb, y); const r = ringAt(Math.max(y, PROF[1][0]), 1.12 + 0.18 * t, 1.18 + 0.2 * t, 0.03 + 0.035 * t, torsoMod(Math.max(y, PROF[1][0]), 0, [[y, 0, 1, null]])); r.c = [0, y, r.c[2] - 0.01 * t]; r.ru = Math.max(r.ru, prof(yHip)[1] * bw * 1.12 + 0.03 + 0.06 * t); r.rv = Math.max(r.rv, prof(yHip)[2] * bd * 1.15 + 0.03 + 0.05 * t); r.mod = (a) => 1 + 0.03 * Math.sin(a * 7 + y * 9) * t; return r; });
      if (kind === 'tailcoat') { m.loft('outer', rr, { n: 12, arc: [BACK + 0.06, BACK + 1.6], weights: (k, P) => tailW(P), tile: 0.5 }); m.loft('outer', rr, { n: 12, arc: [BACK - 1.6, BACK - 0.06], weights: (k, P) => tailW(P), tile: 0.5 }); }
      else { m.loft('outer', rr, { n: 18, arc: [FRONT + open + 0.15, FRONT + PI - 0.03], weights: (k, P) => tailW(P), tile: 0.5 }); m.loft('outer', rr, { n: 18, arc: [FRONT + PI + 0.03, FRONT + 2 * PI - open - 0.15], weights: (k, P) => tailW(P), tile: 0.5 }); }
    }
  }
  if (sp.armor) { // a chest plate and a back plate
    const rr = ys(yS + 0.02, ySh - 0.02).map((y) => ringAt(y, 1.15, 1.2, 0.03, torsoMod(y)));
    m.loft('metal', rr, { n: 20, arc: [FRONT - 1.25, FRONT + 1.25], weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
    m.loft('metal', rr, { n: 20, arc: [BACK - 1.1, BACK + 1.1], weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
  }
  if (sp.hood) { // a hood lying on the upper back
    const hc = [0, yN - 0.03, -prof(yN)[2] * bd - 0.04];
    m.ellipsoid('shirt', hc, 0.15 * bw, 0.07, 0.075, { weights: (k, P, a) => torsoW(P, a), steps: 7, n: 16, mod: (a) => 1 + 0.06 * Math.sin(a * 5) });
    for (const sx of [0.045, -0.045]) m.capsule('band', [sx, yN - 0.02, prof(yN)[2] * bd + 0.03], [sx, yC - 0.06, prof(yC)[2] * bd + 0.045], 0.006, 0.006, { weights: (t, P, a) => torsoW(P, a), n: 6, caps: 2 });
  }

  // ── neck ──
  const nr = 0.064 * (0.85 + 0.15 * W) * (sp.neck ?? 1);
  // up into the head, kept inside the jaw and the back of the skull (a thick neck must not swallow the face)
  const LH = faceLayout(world.Head, sp), yNT = LH.yE - 0.075 * LH.HS * LH.Fl;
  const neckR = (y) => { const S = headSection(LH, y), k = ss(LH.yE - 0.135 * LH.HS, LH.yE - 0.1 * LH.HS, y); return [Math.min(nr * (1 + 0.25 * ss(yN + 0.03, yN - 0.02, y)), lerp(1, 0.9 * S.w / nr, k) * nr), Math.min(nr * 0.95, lerp(nr * 0.95, 0.72 * S.d, k)), lerp(-0.008, S.cz - 0.2 * S.d, k)]; };
  m.loft('skin', ys(yN - 0.02, yNT, 0.015).map((y) => ({ c: [0, y, neckR(y)[2]], U: X, V: Z, ru: neckR(y)[0], rv: neckR(y)[1], e: 2.2, mod: (a) => 1 + 0.06 * gauss(wrapA(a - FRONT), 0.4) * gauss(y - (yN + 0.06), 0.03) })), { n: 18, weights: (k, P) => (P[1] < yN + 0.02 ? torsoW(P, 0) : headW(P)), tile: 0.3 });

  // ── head: a modelled face (facemesh.mjs): sockets, eyeballs, lids, brows, nose, lips, teeth, ears; the jaw, lids and brows move ──
  const head = buildHead({ m, BI, world, sp });
  const { hc, R, RZ, HY } = head.layout, jawW = head.layout.jawW;
  const headRing = (t, k = 1, add = 0) => { const r = head.ring(t, k, add); const mod0 = r.mod; r.mod = (a) => 1 + (mod0(a) - 1) * 0.35; return r; }; // hair and hats follow the skull, not the face detail
  if (sp.shades) { const L2 = head.layout, yS0 = L2.yE + (sp.shadesUp ? 0.068 : 0.002) * L2.HS, S = headSection(L2, yS0); m.ellipsoid(sp.colors.glow ? 'glow' : 'shades', [0, yS0, S.cz + S.d * 0.84], S.w * 1.12, 0.017 * L2.HS, 0.016 * L2.HS, { bone: BI.Head, steps: 6, n: 16, e: 3 }); } // pushed up on the forehead, the eyes show
  if (sp.moustache) for (const sd of [1, -1]) m.capsule('hair', [sd * 0.008, hc[1] - 0.058 * HS, hc[2] + RZ * 0.98], [sd * 0.06 * HS, hc[1] - 0.07 * HS, hc[2] + RZ * 0.8], 0.012 * HS, 0.005 * HS, { bone: BI.Head, n: 8, caps: 2 });
  if (sp.beard) { const r = []; for (let i = 0; i <= 6; i++) { const t = -1.06 + 0.5 * (i / 6); const hr = headRing(t, 1.07, 0.006); hr.mod = (a) => 1 + 0.05 * noise1(a * 6 + i, 3); r.push(hr); } m.loft('hair', r, { n: 24, arc: [FRONT - 1.7, FRONT + 1.7], bone: BI.Head, tile: 0.2 }); }
  if (sp.goatee) { const L2 = head.layout, gb = head.surf(0, L2.yE - 0.1 * L2.HS, -0.004); m.cone('hair', gb, add(gb, [0, -0.032 * L2.HS, 0.006]), 0.022 * L2.HS, { bone: BI.Jaw, n: 10, ratio: 0.7 }); }
  if (sp.mask) { const r = []; for (let i = 0; i <= 6; i++) { const t = -1.05 + 0.95 * (i / 6); r.push(headRing(t, 1.06, 0.004)); } r.forEach((x, i) => { x.mod = (a) => 1 + 0.02 * Math.sin(a * 9 + i); }); m.loft('mask', r, { n: 28, bone: BI.Head, tile: 0.2 }); }

  // hair and hats
  const hairW = (P) => { // the hair root moves with the head; long hair hands over to the spring bones below it
    const ya = B('HairA')[1], yb = B('HairB')[1];
    if (P[1] > ya) return [[BI.Head, 1]];
    const t = ss(ya, yb, P[1]);
    return [[BI.Head, (1 - t) * ss(ya - 0.12, ya, P[1])], [BI.HairA, (1 - t) * (1 - ss(ya - 0.12, ya, P[1])) + 0.001], [BI.HairB, t]];
  };
  const shell = (t0, t1, k, add, steps, o = {}) => { const r = []; for (let i = 0; i <= steps; i++) { const t = t0 + (t1 - t0) * (i / steps); const hr = headRing(Math.min(t, 0.999), k, add); if (o.mod) hr.mod = o.mod(t, i); r.push(hr); } return r; };
  const tuft = (seed, amp) => (t, i) => (a) => 1 + amp * (noise1(a * 7 + seed, seed) - 0.5) * ss(-0.6, 0.6, t);
  const hairCut = sp.hair ?? 'crop';
  const scalp = (k = 1.06, add = 0.006, seed = 1) => {
    m.loft('hair', [...shell(0.28, 1.0, k, add, 9, { mod: tuft(seed, 0.1) })], { n: 28, bone: BI.Head, tile: 0.25 }); // crown down to the hairline
    m.loft('hair', shell(-0.4, 0.3, k, add, 6, { mod: tuft(seed + 3, 0.08) }), { n: 18, arc: [BACK - 1.8, BACK + 1.8], bone: BI.Head, tile: 0.25 }); // the sides and back
  };
  // swept clumps over the scalp break up the outline (a smooth shell reads as a helmet)
  const clumps = (rows, len, rad, sweep, seed) => {
    for (let r = 0; r < rows; r++) for (let i = 0, nI = 5 + r * 2; i < nI; i++) {
      const ph = 0.12 + (r / rows) * 1.35, th = -PI * 0.9 + 1.8 * PI * ((i + 0.5) / nI) + (noise1(i * 3 + r * 17, seed) - 0.5) * 0.3;
      const nrm = norm([Math.sin(ph) * Math.sin(th), Math.cos(ph), Math.sin(ph) * Math.cos(th)]);
      if (nrm[2] > 0.55 && nrm[1] < 0.75) continue; // keep the forehead clear
      const base = add(hc, [nrm[0] * R * 1.02, nrm[1] * HY * 1.02, nrm[2] * RZ * 1.02]);
      const tdir = norm(add(mul(nrm, 0.2), [nrm[0] * 0.3, -0.25 - 0.5 * sweep, -0.85 * sweep - 0.2]));
      const side = norm([tdir[1] * nrm[2] - tdir[2] * nrm[1], tdir[2] * nrm[0] - tdir[0] * nrm[2], tdir[0] * nrm[1] - tdir[1] * nrm[0]]);
      m.cone('hair', base, add(base, mul(tdir, len * (0.75 + 0.5 * noise1(i + r * 7, seed + 1)))), rad, { ratio: 0.42, U: side, V: nrm, bone: BI.Head, n: 7, rings: 4, bend: mul(nrm, -0.02) });
    }
  };
  // ── hair cards (full detail fighters): ribbons of the alpha-tested strand texture along curves that grow from the
  // scalp, lit with the outward normal of the hair volume (so the cards shade like a mass of hair, not flat planes);
  // crossed pairs for standing tufts. The anisotropic highlight runs along each card's v (KHR_materials_anisotropy).
  const cardStyle = !sp.lod && (hairCut === 'spiky' || hairCut === 'mohawk' || hairCut === 'ponytail');
  let rs = sp.name.length * 977 + 13;
  const rnd = () => { rs = (Math.imul(rs, 1664525) + 1013904223) >>> 0; return rs / 4294967296; };
  const hcC = [0, hc[1] - 0.02 * HS, hc[2]];
  const ell = (p) => Math.hypot(p[0] / R, (p[1] - hc[1]) / HY, (p[2] - hc[2]) / RZ); // 1 on the skull ellipsoid
  /** a point on the scalp at ring height t and loft angle a (PI/2 the front), and its outward direction */
  const scalpPt = (t, a, out = 0) => { const r = headRing(t); const cs = Math.cos(a), sn = Math.sin(a); const p = [r.ru * Math.sign(cs) * Math.pow(Math.abs(cs), 2 / r.e), r.c[1], r.c[2] + r.rv * Math.sign(sn) * Math.pow(Math.abs(sn), 2 / r.e)]; const n = norm(sub(p, hcC)); return [add(p, mul(n, out)), n]; };
  /** grow a curve: from p along dir, steps of len/seg, bending by bend(i, p, d) -> new dir; keep it off the skull by off */
  const grow = (p, dir, len, seg, bend, off = 0) => { const pts = [p]; let d = norm(dir); for (let i = 0; i < seg; i++) { d = norm(bend(i, pts[pts.length - 1], d)); let q = add(pts[pts.length - 1], mul(d, len / seg)); if (off > 0) { const e = ell(q); if (e < 1 + off) { const n = norm(sub(q, hcC)); q = add(q, mul(n, (1 + off - e) * R)); } } pts.push(q); } return pts; };
  /** one card along pts: width w0 at the root to w1 at the tip; twist: 0 flat to the scalp, PI/2 standing; u0 the texture strip */
  const card = (pts, w0, w1, twist, weights, u0 = rnd() * 0.6, uw = 0.4) => {
    const verts = [], tris = [], K = pts.length;
    for (let i = 0; i < K; i++) {
      const s = i / (K - 1), tg = norm(sub(pts[Math.min(K - 1, i + 1)], pts[Math.max(0, i - 1)])), out = norm(sub(pts[i], hcC));
      const side0 = norm(cross(tg, out)), up0 = norm(cross(side0, tg));
      const side = add(mul(side0, Math.cos(twist)), mul(up0, Math.sin(twist))), w = lerp(w0, w1, Math.pow(s, 0.8)) * 0.5;
      const n = norm(add(out, mul(tg, -0.15)));
      const tn = norm(sub(side, mul(n, dot(side, n)))); // the tangent (u across the card) for the anisotropic highlight: no derivative frame
      for (const k of [-1, 1]) verts.push({ p: add(pts[i], mul(side, k * w)), n, t: [...tn, 1], uv: [u0 + (k > 0 ? uw : 0), s], w: weights(s, pts[i]), tag: 'hair' });
      if (i) { const a = (i - 1) * 2, b = i * 2; tris.push([a, b, b + 1], [a, b + 1, a + 1]); }
    }
    m.raw('hairCard', verts, tris);
  };
  const onHead = () => [[BI.Head, 1]];
  if (cardStyle) {
    const shellAt = (t0, t1, steps, arc) => { const rr = []; for (let i = 0; i <= steps; i++) { const t = t0 + (t1 - t0) * (i / steps), e = ss(0, 0.25, i / steps); rr.push(headRing(Math.min(t, 0.999), 1 + 0.006 * e, 0.0006 + 0.0012 * e)); } m.loft(hairCut === 'mohawk' ? 'stubble' : 'hair', rr, { n: 32, bone: BI.Head, tile: 0.25, ...(arc ? { arc } : {}) }); };
    if (hairCut === 'mohawk') { /* the shaved sides are painted (face texture) */ }
    else { shellAt(0.56, 1, 9); shellAt(-0.42, 0.58, 8, [BACK - 1.75, BACK + 1.75]); }
  }
  if (cardStyle && hairCut === 'spiky') { // short spiky tufts standing up and back, a fringe swept up off the forehead
    for (let i = 0; i < 150; i++) {
      const t = lerp(0.55, 0.97, Math.sqrt(rnd())), a = PI / 2 + (rnd() * 2 - 1) * PI * (t > 0.9 ? 1 : 0.92);
      const [p, n] = scalpPt(t, a, -0.002);
      const back = Math.cos(a - PI / 2) < 0 ? 1 : 0.4, len = (0.055 + 0.05 * rnd()) * HS * (1 - 0.25 * back * (1 - t));
      const dir = add(mul(n, 1.0), [0, 0.55, -0.45 * back - 0.1]);
      const pts = grow(p, dir, len, 4, (k, q, d) => add(d, [0, -0.04 * k, -0.06 * k]), 0.04);
      const tw = rnd() * PI;
      card(pts, 0.026 * HS, 0.003 * HS, tw, onHead); card(pts, 0.022 * HS, 0.003 * HS, tw + PI / 2, onHead);
    }
    for (let i = 0; i < 0; i++) { // (short cards lying down the sides caught the low sun as pale blocks: the shell and the painted scalp carry the sides)
      const t = lerp(-0.25, 0.62, rnd()), a = BACK + (rnd() * 2 - 1) * 1.75;
      const [p, n] = scalpPt(t, a, 0.001);
      const pts = grow(p, add(mul(n, 0.15), [0, -1, -0.2]), (0.035 + 0.025 * rnd()) * HS, 4, (k, q, d) => d, 0.012 + 0.01 * rnd());
      card(pts, 0.024 * HS, 0.006 * HS, 0, onHead);
    }
  }
  if (cardStyle && hairCut === 'mohawk') { // a crest of tall stiff tufts along the midline, splayed a little, tips raked back, over a solid core
    for (let i = 0; i < 7; i++) {
      const u = i / 6, ang = lerp(0.55, -1.75, u), dir = norm([0, Math.cos(ang) * 0.9 + 0.1, Math.sin(ang) + 0.15]);
      const base = add(hc, [0, Math.cos(ang) * HY * 0.9, Math.sin(ang) * RZ * 0.9]);
      const h = (0.1 + 0.05 * Math.sin(u * PI)) * HS * (sp.mohawk ?? 1);
      m.cone('hair', base, add(base, mul(dir, h)), 0.026 * HS, { ratio: 0.4, U: [0, -dir[2], dir[1]], V: X, weights: () => (u > 0.6 ? [[BI.Head, 0.6], [BI.HairA, 0.4]] : [[BI.Head, 1]]), n: 8, bend: [0, -0.01, -0.035] });
    }
    for (let i = 0; i < 70; i++) {
      const u = i / 69, ang = lerp(0.62, -1.7, u), base = add(hc, [(rnd() - 0.5) * 0.022 * HS, Math.cos(ang) * HY * 0.97, Math.sin(ang) * RZ * 0.97]);
      const h = (0.11 + 0.07 * Math.sin(u * PI) + 0.03 * rnd()) * HS * (sp.mohawk ?? 1), n = norm(sub(base, hcC));
      const dir = add(n, [(rnd() - 0.5) * 0.25, 0.25, -0.15]);
      const pts = grow(base, dir, h, 5, (k, q, d) => add(d, [0, -0.02 * k, -0.07 * k]));
      const w = (u > 0.55 ? [[BI.Head, 0.6], [BI.HairA, 0.4]] : [[BI.Head, 1]]), tw = rnd() * PI;
      card(pts, 0.03 * HS, 0.004 * HS, PI / 2 + (rnd() - 0.5) * 0.4, () => w); card(pts, 0.022 * HS, 0.004 * HS, tw, () => w);
    }
  }
  if (cardStyle && hairCut === 'ponytail') { // hair swept back over the scalp to a tie at the crown, the tail on its spring bones
    const tie = add(hc, [0, HY * 0.5, -RZ * 0.9]);
    m.capsule('band', add(tie, [0, 0.008, 0.012]), add(tie, [0, -0.006, -0.01]), 0.02 * HS, 0.02 * HS, { bone: BI.Head, n: 12, caps: 2 });
    for (let i = 0; i < 190; i++) { // lying cards from the hairline toward the tie, layered
      const t = lerp(0.5, 0.98, Math.pow(rnd(), 0.7)), a = PI / 2 + (rnd() * 2 - 1) * PI * 0.97;
      const [p] = scalpPt(t, a, 0.0012 + 0.003 * rnd());
      const to = sub(tie, p), L0 = Math.hypot(...to);
      if (L0 < 0.03) continue;
      const pts = grow(p, to, L0 * 0.98, 6, (k, q, d) => add(d, mul(norm(sub(tie, q)), 0.35)), 0.012 + 0.012 * rnd());
      card(pts, 0.034 * HS, 0.03 * HS, 0, onHead);
    }
    // the tail: a solid core (reads at fight distance) wrapped in a bundle of long cards, swinging on HairA and HairB
    { const a = add(tie, [0, -0.004, -0.012]), b = add(a, [0, -0.12, -0.07]), c = add(b, [0, -0.15, -0.01]);
      m.capsule('hair', a, b, 0.022 * HS, 0.03 * HS, { weights: (t) => [[BI.Head, 1 - t], [BI.HairA, t]], n: 12, caps: 3, ratio: 0.8, mod: (an) => 1 + 0.08 * Math.sin(an * 6) });
      m.capsule('hair', b, c, 0.03 * HS, 0.008 * HS, { weights: (t) => [[BI.HairA, 1 - t], [BI.HairB, t]], n: 12, caps: 3, ratio: 0.8, mod: (an) => 1 + 0.08 * Math.sin(an * 6) }); }
    const tw = (s) => (s < 0.4 ? [[BI.Head, 1 - s / 0.4], [BI.HairA, s / 0.4]] : [[BI.HairA, 1 - (s - 0.4) / 0.6], [BI.HairB, (s - 0.4) / 0.6]]);
    for (let i = 0; i < 46; i++) {
      const ang = rnd() * PI * 2, rr = 0.012 * Math.sqrt(rnd()), p = add(tie, [Math.cos(ang) * rr, Math.sin(ang) * rr * 0.6, -0.008]);
      const len = (0.27 + 0.08 * rnd()) * HS, spread = [Math.cos(ang) * 0.35, 0.1, Math.sin(ang) * 0.15 - 0.6];
      const pts = grow(p, add([0, 0.25, -1], spread), len, 9, (k, q, d) => add(d, [Math.cos(ang) * 0.04 * (k < 4 ? 1 : -1.2), -0.32, 0.02]));
      card(pts, 0.04 * HS, 0.008 * HS, ang, (s) => tw(s));
    }
    for (const sx of [] ) for (let i = 0; i < 4; i++) { // (no loose strands: they read as streaks on the cheeks)
      const [p] = scalpPt(0.52, PI / 2 + sx * (1.0 + 0.1 * i), 0.004);
      const pts = grow(p, [sx * 0.25, -1, 0.35], (0.1 + 0.03 * i) * HS, 6, (k, q, d) => add(d, [sx * 0.02, -0.05, -0.02]), 0.04);
      card(pts, 0.012 * HS, 0.003 * HS, 0.3 * sx, onHead);
    }
  }
  if (!cardStyle && (hairCut === 'crop' || hairCut === 'long' || hairCut === 'ponytail' || hairCut === 'spiky')) { scalp(1.05, 0.004, sp.name.length); clumps(hairCut === 'crop' ? 4 : 3, 0.085 * HS, 0.03 * HS, hairCut === 'ponytail' ? 0.9 : 0.6, sp.name.length); }
  if (!cardStyle && hairCut === 'spiky') {
    for (let i = 0; i < 13; i++) {
      const th = (i / 13) * PI * 2, ph = 0.35 + 0.45 * ((i * 7) % 5) / 5, dir = norm([Math.cos(th) * Math.sin(ph), Math.cos(ph) + 0.25, Math.sin(th) * Math.sin(ph) - 0.35]);
      const base = add(hc, [dir[0] * R * 0.85, dir[1] * HY * 0.8, dir[2] * RZ * 0.85]);
      m.cone('hair', base, add(base, mul(dir, 0.085 * HS)), 0.035 * HS, { weights: () => [[BI.Head, 1]], n: 7, bend: [0, -0.01, -0.02] });
    }
  }
  if (!cardStyle && hairCut === 'mohawk') {
    m.loft('stubble', shell(0.3, 1.0, 1.015, 0.002, 7), { n: 28, bone: BI.Head, tile: 0.25 }); m.loft('stubble', shell(-0.5, 0.32, 1.015, 0.002, 6), { n: 18, arc: [BACK - 2.05, BACK + 2.05], bone: BI.Head, tile: 0.25 });
    for (let i = 0; i < 7; i++) {
      const u = i / 6, ang = lerp(0.55, -1.75, u), dir = norm([0, Math.cos(ang) * 0.9 + 0.1, Math.sin(ang) + 0.15]);
      const base = add(hc, [0, Math.cos(ang) * HY * 0.86, Math.sin(ang) * RZ * 0.86]);
      const h = (0.12 + 0.06 * Math.sin(u * PI)) * HS * (sp.mohawk ?? 1);
      m.cone('hair', base, add(base, mul(dir, h)), 0.034 * HS, { ratio: 0.45, U: [0, -dir[2], dir[1]], V: X, weights: () => (u > 0.6 ? [[BI.Head, 0.6], [BI.HairA, 0.4]] : [[BI.Head, 1]]), n: 8, bend: [0, -0.01, -0.035] });
    }
  }
  if (hairCut === 'long') { // a fall of hair down the back to the shoulder blades, with clumps
    const ya = hc[1] + 0.02, yb = yC + 0.06;
    const rr = ys(yb, ya, 0.03).reverse().map((y, i) => {
      const t = ss(ya, yb, y), hr = headRing(Math.max(-0.75, (y - hc[1]) / HY), 1.08, 0.01);
      hr.c = [0, y, hc[2] - 0.035 - 0.05 * t]; hr.ru = Math.max(hr.ru, R * (0.95 + 0.25 * t)); hr.rv = Math.max(hr.rv * 0.9, RZ * 0.7);
      hr.mod = (a) => 1 + 0.07 * Math.sin(a * 11 + i * 0.3) * t; return hr;
    });
    m.loft('hair', rr, { n: 26, arc: [BACK - 1.55, BACK + 1.55], weights: (k, P) => hairW(P), tile: 0.3 });
  }
  if (!cardStyle && hairCut === 'ponytail') {
    const a = add(hc, [0, HY * 0.55, -RZ * 0.85]), b = add(a, [0, -0.13, -0.06]), c = add(b, [0, -0.16, 0.0]);
    m.capsule('band', add(a, [0, 0, 0.01]), add(a, [0, -0.012, -0.012]), 0.026 * HS, 0.026 * HS, { bone: BI.Head, n: 12, caps: 2 });
    m.capsule('hair', a, b, 0.034 * HS, 0.042 * HS, { weights: (t) => [[BI.Head, 1 - t], [BI.HairA, t]], n: 12, caps: 3, ratio: 0.8, mod: (an) => 1 + 0.08 * Math.sin(an * 6) });
    m.capsule('hair', b, c, 0.042 * HS, 0.012 * HS, { weights: (t) => [[BI.HairA, 1 - t], [BI.HairB, t]], n: 12, caps: 3, ratio: 0.8, mod: (an) => 1 + 0.08 * Math.sin(an * 6) });
  }
  if (hairCut === 'beanie') {
    m.loft('hat', shell(0.22, 1.0, 1.1, 0.012, 10, { mod: (t) => (a) => 1 + 0.012 * Math.sin(a * 24) }), { n: 32, bone: BI.Head, tile: 0.25 });
    m.loft('hat', [headRing(0.18, 1.12, 0.016), headRing(0.22, 1.14, 0.02), headRing(0.36, 1.13, 0.018), headRing(0.41, 1.1, 0.012)], { n: 32, bone: BI.Head, tile: 0.25 });
  }
  if (hairCut === 'cap' || hairCut === 'capback') {
    scalp(1.05, 0.004, 5);
    m.loft('hat', shell(0.25, 1.0, 1.1, 0.012, 9), { n: 32, bone: BI.Head, tile: 0.25 });
    const fwd = hairCut === 'cap' ? 1 : -1, y = hc[1] + HY * 0.3;
    m.ellipsoid('hat', [0, y, hc[2] + fwd * RZ * 1.18], 0.09 * HS, 0.008, 0.075 * HS, { bone: BI.Head, steps: 4, n: 18, e: 2.4 });
  }
  if (hairCut === 'tophat') {
    scalp(1.05, 0.004, 7);
    const y0 = hc[1] + HY * 0.45;
    m.loft('hat', [[0, 0.002, 0.002], [0, 1.0, 1.0], [0.02, 1.0, 1.0], [0.2, 0.95, 0.95], [0.23, 1.0, 1.0], [0.235, 0.9, 0.9], [0.236, 0.002, 0.002]].map(([dy, k, kk]) => ({ c: [0, y0 + dy * HS, hc[2] - 0.01], U: X, V: Z, ru: R * 0.92 * k, rv: RZ * 0.92 * kk, e: 2.2 })), { n: 28, bone: BI.Head, tile: 0.3 });
    m.loft('band', [{ c: [0, y0 + 0.025 * HS, hc[2] - 0.01], U: X, V: Z, ru: R * 0.935, rv: RZ * 0.935 }, { c: [0, y0 + 0.055 * HS, hc[2] - 0.01], U: X, V: Z, ru: R * 0.93, rv: RZ * 0.93 }], { n: 28, bone: BI.Head, tile: 0.3 });
    m.ellipsoid('hat', [0, y0 + 0.004, hc[2] - 0.01], R * 1.55, 0.008, RZ * 1.4, { bone: BI.Head, steps: 4, n: 28, e: 2.2 });
  }
  if (hairCut === 'hardhat' || hairCut === 'helmet') {
    if (hairCut === 'hardhat') scalp(1.04, 0.003, 9);
    m.loft('helmet', shell(hairCut === 'helmet' ? -0.25 : 0.1, 1.0, hairCut === 'helmet' ? 1.2 : 1.17, 0.014, 10, { mod: (t) => (a) => 1 + 0.035 * gauss(wrapA(a - FRONT), 0.18) * ss(0.2, 0.7, t) }), { n: 32, bone: BI.Head, tile: 0.3 });
    if (hairCut === 'hardhat') m.ellipsoid('helmet', [0, hc[1] + HY * 0.12, hc[2] + 0.01], R * 1.42, 0.01, RZ * 1.38, { bone: BI.Head, steps: 4, n: 28, e: 2.3, mod: (a) => 1 + 0.25 * Math.pow(Math.max(0, Math.sin(a)), 3) });
    if (hairCut === 'helmet') { // a visor bar and cheek guards
      m.loft('helmet', [headRing(-0.05, 1.24, 0.02), headRing(0.08, 1.26, 0.022)], { n: 24, arc: [FRONT - 1.3, FRONT + 1.3], bone: BI.Head, tile: 0.3 });
      m.loft('glow', [headRing(0.08, 1.235, 0.019), headRing(0.2, 1.22, 0.018)], { n: 24, arc: [FRONT - 1.0, FRONT + 1.0], bone: BI.Head, tile: 0.3 });
      for (const sd of [1, -1]) m.ellipsoid('helmet', [sd * R * 0.98, hc[1] - 0.05 * HS, hc[2] + 0.03], 0.03 * HS, 0.06 * HS, 0.07 * HS, { bone: BI.Head, steps: 5, n: 12 });
    }
  }
  if (sp.headband) m.loft('band', [-0.02, 0.02].map((dy) => headRing(0.42 + dy / HY, 1.09, 0.008)), { n: 32, bone: BI.Head, tile: 0.3 });
  if (sp.headband) { const a = add(hc, [0, HY * 0.42, -RZ * 1.05]); m.capsule('band', a, add(a, [0.03, -0.14, -0.05]), 0.012, 0.008, { weights: (t) => [[BI.Head, 1 - t], [BI.HairA, t]], n: 6, caps: 2, ratio: 0.4 }); m.capsule('band', a, add(a, [-0.04, -0.12, -0.04]), 0.012, 0.008, { weights: (t) => [[BI.Head, 1 - t], [BI.HairA, t]], n: 6, caps: 2, ratio: 0.4 }); }

  // ── arms: deltoid, biceps and triceps, elbow, forearm, wrist; sleeves are a layer over them ──
  const armR = 0.05 * AR * (0.8 + 0.2 * W);
  const sleeve = sp.sleeves ?? (top === 'longsleeve' || top === 'hoodie' ? 'long' : top === 'tee' ? 'short' : 'none');
  const outerSleeve = outer && (sp.outerKind ?? 'jacket') !== 'vest' && sp.outerKind !== 'hivis';
  const armRad = (y) => { // [radius side to side, radius front to back]
    const top0 = ySh + 0.05, d = (top0 - y);
    const k = lerp(0.25, 1.22, ss(0, 0.045, d)) // the shoulder cap
      + 0.12 * gauss(y - (ySh - 0.04), 0.04) // deltoid
      + lerp(0, -0.2, ss(ySh - 0.02, yEl + 0.02, y)) // down to the elbow
      + 0.13 * gauss(y - (yEl - 0.07), 0.05) // forearm
      - 0.3 * ss(yEl - 0.07, yWr + 0.01, y); // the wrist
    return [armR * k * 1.05, armR * k];
  };
  const armMod = (y) => (a) => 1 + 0.1 * gauss(y - (ySh - 0.13), 0.05) * gauss(wrapA(a - FRONT), 0.6) + 0.06 * gauss(y - (ySh - 0.12), 0.06) * gauss(wrapA(a - BACK), 0.8);
  for (const sx of [1, -1]) {
    const ax = sx * sw;
    const ringA = (y, k = 1, add = 0, fl = null) => { const [ru, rv] = armRad(y); return { c: [ax + sx * 0.008 * gauss(y - ySh, 0.05), y, 0.004], U: X, V: Z, ru: ru * k + add, rv: rv * k + add, e: 2.15, mod: fl ? (a) => armMod(y)(a) + folds(y, a, fl) : armMod(y) }; };
    const yTop = ySh + 0.05, yWristEnd = yWr - 0.005;
    const sleeveEnd = sleeve === 'three' ? yEl - 0.09 : outerSleeve || sleeve === 'long' ? yWr + 0.03 : sleeve === 'short' ? ySh - 0.15 : yTop;
    const sFolds = [[yEl + 0.04, 0.05, 0.012, BACK, 0.9], [yEl - 0.0, 0.045, 0.012, FRONT, 0.6], [yEl - 0.035, 0.04, 0.012, BACK, 0.9], [yEl + 0.08, 0.03, 0.014, null], [yWr + 0.06, 0.035, 0.012, null]];
    const skinY = ys(yWristEnd, Math.min(yTop, sleeveEnd + 0.02), 0.02).reverse();
    if (skinY.length > 1) m.loft('skin', skinY.map((y) => ringA(y)), { n: 20, weights: (k, P) => armW(P), tile: 0.3 });
    if (sleeveEnd < yTop) {
      const mat = outerSleeve ? 'outer' : shirtMat, th = outerSleeve ? 0.024 : 0.012;
      const yy = ys(sleeveEnd, yTop, 0.018).reverse();
      const rr = yy.map((y) => ringA(y, 1.06, th, sFolds));
      rr.push({ ...ringA(sleeveEnd, 1.08, th + 0.006), c: [ax, sleeveEnd - 0.005, 0.004] }); // the cuff
      m.loft(mat, rr, { n: 22, weights: (k, P) => armW(P), tile: 0.4 });
      if (outerSleeve && sleeve !== 'long') { /* the jacket hides the shirt sleeve */ }
    }
    { // the deltoid: a rounded cap over the shoulder joint so the arm grows out of the torso instead of a flat ledge
      const dm = top === 'none' || top === 'tank' ? (outerSleeve ? 'outer' : 'skin') : outerSleeve ? 'outer' : shirtMat, th = dm === 'skin' ? 0 : dm === 'outer' ? 0.004 : 0.004;
      m.ellipsoid(dm, [ax + sx * 0.012, ySh + 0.012, 0.004], armR * (dm === 'skin' ? 1.08 : 0.98) + th, 0.06 + th, armR * (dm === 'skin' ? 1.06 : 0.96) + th, { weights: (k, P) => { const t = ss(ySh + 0.07, ySh - 0.03, P[1]) * ss(sw - 0.06, sw + 0.03, Math.abs(P[0])); return [[BI.Chest, 1 - 0.7 * t], [BI['UpperArm' + (sx > 0 ? 'L' : 'R')], 0.7 * t]]; }, steps: 7, n: 16, e: 2.2 });
    }
    if (sp.pads) m.ellipsoid(sp.padMat ?? 'pad', [ax + sx * 0.035, ySh + 0.04, 0], 0.085 * (0.8 + 0.2 * W), 0.05, 0.095 * (0.8 + 0.2 * W), { weights: () => [[BI.Chest, 0.4], [BI['UpperArm' + (sx > 0 ? 'L' : 'R')], 0.6]], steps: 6, n: 16, e: 2.4 });
    if (sp.spikes) for (let i = 0; i < 3; i++) { const base = [ax + sx * (0.05 + 0.02 * i), ySh + 0.08 - 0.012 * i, -0.04 + 0.04 * i]; m.cone('buckle', base, add(base, [sx * 0.03, 0.05, 0]), 0.012, { weights: () => [[BI['UpperArm' + (sx > 0 ? 'L' : 'R')], 1]], n: 6 }); }
    if (sp.bracers) m.loft('harness', ys(yWr + 0.02, yEl - 0.04, 0.02).reverse().map((y) => ringA(y, 1.03, 0.01)), { n: 18, weights: (k, P) => armW(P), tile: 0.3 });
    if (sp.wristband) m.loft('band', [ringA(yWr + 0.05, 1.12, 0.008), ringA(yWr + 0.015, 1.12, 0.008)], { n: 16, weights: (k, P) => armW(P), tile: 0.3 });
    // the hand: a palm, four two-part fingers on the Fingers and Fingers2 bones and a thumb on its own bone, modelled open
    // (the game curls them into a fist); gloves cover the palm and the first finger joints, wraps tape the palm and knuckles
    const sd = sx > 0 ? 'L' : 'R', s = HN, hy = yWr, H = BI['Hand' + sd], F1 = BI['Middle1' + sd], TH = BI['Thumb' + sd], TH2 = BI['Thumb2' + sd];
    const hx = ax - sx * 0.002 * s, palmMat = sp.gloves ? 'glove' : sp.wraps ? 'wrap' : 'skin', tipMat = sp.gloves && !sp.fingerless ? 'glove' : 'skin';
    const yK = hy - 0.092 * s;
    const palm = ys(yK + 0.004 * s, hy + 0.012, 0.01).reverse().map((y) => { const t = ss(hy, yK, y); return { c: [hx, y, 0.003 * s], U: X, V: Z, ru: (0.017 + 0.003 * gauss(t - 0.55, 0.3)) * s, rv: (0.036 + 0.008 * t) * s, e: 3.0, mod: (an) => 1 + 0.12 * gauss(t - 0.35, 0.25) * gauss(wrapA(an - (sx > 0 ? PI : 0)), 0.6) }; });
    palm.push({ c: [hx, yK - 0.002 * s, 0.003 * s], U: X, V: Z, ru: 0.012 * s, rv: 0.034 * s, e: 3 });
    m.loft(palmMat, palm, { n: 20, weights: (k, P) => (P[1] > hy - 0.008 ? [[H, 0.65], [BI['LowerArm' + sd], 0.35]] : P[1] < yK + 0.012 ? [[H, 0.7], [F1, 0.3]] : [[H, 1]]), tile: 0.15 });
    if (sp.wraps) m.loft('wrap', ys(hy - 0.01, hy + 0.06, 0.012).reverse().map((y) => { const r = ringA(Math.min(y, hy + 0.05), 1.05, 0.006); r.c = [ax, y, 0.004]; return r; }), { n: 16, weights: (k, P) => armW(P), tile: 0.15 });
    // thenar pad under the thumb
    m.ellipsoid(palmMat, [hx - sx * 0.008 * s, hy - 0.035 * s, 0.024 * s], 0.011 * s, 0.028 * s, 0.014 * s, { bone: H, steps: 5, n: 10 });
    FING.forEach(([fz, ky, l1, l2, r], fi) => { // each finger on its own two bones; knuckles stand proud on the back of the hand
      const B1 = BI[FINGERS[fi] + 1 + sd], B2 = BI[FINGERS[fi] + 2 + sd];
      const k0 = [hx - sx * 0.003 * s, hy + ky * s, fz * s], tip = add(k0, [0, -(l1 + l2) * s, 0.002 * s]), tj = l1 / (l1 + l2);
      const wf = (t) => { const w2 = ss(tj - 0.06, tj + 0.06, t), wh = 0.6 * ss(0.12, 0.0, t); return [[H, wh], [B1, (1 - w2) * (1 - wh)], [B2, w2 * (1 - wh)]]; };
      const mid = add(k0, [0, -l1 * s, 0.001 * s]);
      const joint = (t) => (an) => 1 + 0.1 * G(t - 0.0, 0.08) * Math.max(0, Math.cos(an) * -sx) + 0.07 * G(t - 1, 0.12); // a knuckle bump on the back
      m.capsule(palmMat === 'skin' ? 'skin' : palmMat, k0, mid, r * s * 1.12, r * s, { weights: (t) => wf(t * tj), n: 10, caps: 3, rings: 4, ratio: 0.88, mod: joint(0) });
      m.capsule(tipMat, mid, tip, r * s * 1.02, r * s * 0.8, { weights: (t) => wf(tj + t * (1 - tj)), n: 10, caps: 3, rings: 4, ratio: 0.85 });
      m.ellipsoid(palmMat === 'skin' ? 'skin' : palmMat, add(k0, [sx * 0.006 * s, 0.004 * s, 0]), 0.0075 * s, 0.009 * s, 0.0085 * s, { weights: () => [[H, 0.5], [B1, 0.5]], steps: 5, n: 10 });
      if (tipMat === 'skin') m.ellipsoid('nail', add(tip, [sx * 0.0062 * s, 0.009 * s, 0]), 0.0018 * s, 0.0085 * s, r * s * 0.62, { bone: B2, steps: 4, n: 8 });
    });
    const t0 = [hx - sx * 0.012 * s, hy - 0.028 * s, 0.03 * s], t1 = add(t0, [-sx * 0.012 * s, -0.03 * s, 0.022 * s]), t2 = add(t1, [-sx * 0.006 * s, -0.026 * s, 0.012 * s]);
    m.capsule(palmMat, t0, t1, 0.0125 * s, 0.0105 * s, { weights: (t) => [[H, 0.5 * (1 - t)], [TH, 1 - 0.5 * (1 - t)]], n: 10, caps: 3, rings: 3 }); // thumb
    m.capsule(tipMat, t1, t2, 0.0105 * s, 0.0085 * s, { weights: (t) => [[TH, 1 - ss(0, 0.3, t)], [TH2, ss(0, 0.3, t)]], n: 10, caps: 3, rings: 3 });
  }

  // ── legs: thigh, knee, calf, ankle; trousers are a layer over them ──
  const legR = 0.078 * LG * (0.8 + 0.2 * W);
  const legRad = (y) => {
    const k = 1.08 + 0.1 * gauss(y - (yHip - 0.08), 0.08) - 0.38 * ss(yHip - 0.05, yKn + 0.05, y) // thigh to knee
      + 0.14 * gauss(y - (yKn - 0.12), 0.07) // calf
      - 0.34 * ss(yKn - 0.14, yAn + 0.08, y); // ankle
    return [legR * k, legR * k * 1.04];
  };
  const legMod = (y, sx) => (a) => 1 + 0.07 * gauss(y - (yHip - 0.16), 0.08) * gauss(wrapA(a - (FRONT - sx * 0.4)), 0.7) + 0.08 * gauss(y - (yKn - 0.11), 0.06) * gauss(wrapA(a - BACK), 0.8) + 0.04 * gauss(y - (yHip - 0.12), 0.1) * gauss(wrapA(a - (sx > 0 ? 0 : PI)), 0.6);
  const pantsEnd = bottom === 'shorts' ? yKn + 0.1 : bottom === 'none' ? yHip + 0.1 : sp.boots ? yAn + 0.2 : yAn + 0.075;
  const loose = bottom === 'gi' ? 0.04 : bottom === 'cargo' ? 0.02 : bottom === 'tight' ? 0.004 : bottom === 'jeans' ? 0.014 : 0.02;
  for (const sx of [1, -1]) {
    const lx = sx * hw;
    const ringL = (y, k = 1, add = 0, fl = null) => { const [ru, rv] = legRad(y); return { c: [lx, y, 0.0], U: X, V: Z, ru: ru * k + add, rv: rv * k + add, e: 2.1, mod: fl ? (a) => legMod(y, sx)(a) + folds(y, a, fl) : legMod(y, sx) }; };
    const yTop = yHip + 0.07;
    const pFolds = [[yKn + 0.05, 0.05, 0.014, BACK, 0.8], [yKn - 0.01, 0.04, 0.012, BACK, 0.8], [yKn + 0.02, 0.035, 0.014, FRONT, 0.5], [yHip - 0.06, 0.03, 0.015, FRONT + sx * 0.5, 0.5],
      [pantsEnd + 0.05, 0.05, 0.012, null], [pantsEnd + 0.1, 0.04, 0.012, null], [pantsEnd + 0.16, 0.03, 0.015, FRONT, 1]];
    if (bottom === 'cargo') pFolds.push([yHip - 0.25, 0.06, 0.045, sx > 0 ? 0 : PI, 0.45]); // side pockets
    if (pantsEnd > yAn + 0.03) m.loft('skin', ys(yAn + 0.02, pantsEnd + 0.02, 0.025).reverse().map((y) => ringL(y)), { n: 20, weights: (k, P) => legW(P), tile: 0.3 });
    const rr = ys(pantsEnd, yTop, 0.022).reverse().map((y) => ringL(y, 1.05, loose + 0.01 * ss(yKn, yAn, y) * (bottom === 'jeans' ? 1 : 0), bottom === 'none' ? null : pFolds));
    rr.push({ ...ringL(pantsEnd, 1.08, loose + 0.006), c: [lx, pantsEnd - 0.006, 0] });
    m.loft(bottom === 'none' ? 'skin' : 'pants', rr, { n: 24, weights: (k, P) => legW(P), tile: 0.5 });
    if (bottom === 'joggers' || bottom === 'tight') m.loft('band', ys(yHip - 0.35, yHip - 0.02, 0.03).reverse().map((y) => ringL(y, 1.05, loose + 0.003)), { n: 4, arc: sx > 0 ? [-0.12, 0.12] : [PI - 0.12, PI + 0.12], weights: (k, P) => legW(P), tile: 0.3 }); // a side stripe
    if (sp.kneepads) m.ellipsoid('pad', [lx, yKn + 0.01, legR * 0.85], 0.05, 0.06, 0.03, { weights: () => [[BI['LowerLeg' + (sx > 0 ? 'L' : 'R')], 0.5], [BI['UpperLeg' + (sx > 0 ? 'L' : 'R')], 0.5]], steps: 5, n: 14 });
    // footwear: a loft heel to toe with a thick sole, boots with a shaft
    const f = B('Foot' + (sx > 0 ? 'L' : 'R')), F = BI['Foot' + (sx > 0 ? 'L' : 'R')], fs = sp.foot ?? 1.12, wsh = (0.82 + 0.18 * W) * fs;
    const sh = [[-0.1, 0.034, 0.03], [-0.09, 0.05, 0.06], [-0.04, 0.055, 0.072], [0.03, 0.057, 0.062], [0.1, 0.058, 0.052], [0.16, 0.052, 0.042], [0.2, 0.038, 0.032], [0.225, 0.016, 0.018]];
    m.loft('shoe', sh.map(([z, w, h]) => ({ c: [f[0], 0.022 + h * 0.5, f[2] + z * fs], U: X, V: Y, ru: w * wsh, rv: h * 0.55 * fs, e: 2.6 })), { n: 18, weights: (k, P) => (P[1] > yAn + 0.0 ? [[F, 0.6], [BI['LowerLeg' + (sx > 0 ? 'L' : 'R')], 0.4]] : [[F, 1]]), tile: 0.3 });
    m.loft('sole', [[-0.104, 0.05], [-0.06, 0.06], [0.12, 0.064], [0.2, 0.046], [0.232, 0.02]].map(([z, w]) => ({ c: [f[0], 0.014, f[2] + z * fs], U: X, V: Y, ru: w * wsh * 1.06, rv: 0.016 * fs, e: 3.2 })), { n: 14, bone: F, tile: 0.3 });
    if (sp.boots) m.loft('shoe', ys(yAn - 0.01, yAn + 0.22, 0.03).reverse().map((y) => ringL(y, 1.1, 0.016 + loose * 0.3)).concat([{ ...ringL(yAn - 0.02, 1.15, 0.02), c: [lx, yAn - 0.02, 0.01] }]), { n: 18, weights: (k, P) => legW(P), tile: 0.3 });
  }
  return { m, world, L: head.layout };
}

/** the face texture each character wears (painted in the head's own mapping, facemesh faceSkinFrame) and its eyes */
function writeFace(sp) {
  const skin = hexRaw(sp.colors.skin), L = faceLayout(skeleton(sp).world.Head, sp);
  const hairCol = sp.hair && sp.hair !== 'none' ? hexRaw(sp.colors.roots ?? sp.colors.hair) : null;
  writeSet(TEX, 'face_' + sp.name, faceSkin({ w: sp.lod ? 256 : 2048, h: sp.lod ? 128 : 1024, skin, L, frame: faceSkinFrame(L), lipMasks, stubble: sp.stubble, scar: sp.scar,
    war: sp.war ? hexRaw(sp.war) : null, seed: sp.name.length, age: sp.face?.age ?? 0, lip: sp.lip ? hexRaw(sp.lip) : null, hair: hairCol, hairline: sp.hairline ?? (sp.hair === 'mohawk' ? 'mohawk' : sp.hair === 'none' ? null : 'full'),
    brow: hexRaw(sp.colors.brow ?? sp.colors.roots ?? sp.colors.hair), browArch: sp.face?.browArch ?? 1, browThick: sp.face?.browThick ?? 1,
    makeup: sp.face?.makeup ? { shadow: hexRaw(sp.face.makeup.shadow), blush: hexRaw(sp.face.makeup.blush) } : null }), 3.0);
  writeSet(TEX, 'eye_' + sp.name, bake(sp.lod ? 32 : 256, eyePixel(sp.eyes ? hexRaw(sp.eyes) : [0.3, 0.18, 0.08], sp.name.length)), 0.5);
}
// The material table: every garment slot names its fabric, and each fabric has one class, texture set, roughness and its
// glTF material extensions (KHR_materials_sheen / specular / clearcoat / ior / anisotropy), so the file carries the
// shading and the game needs no per-class conversion. Ordinary cloth (cotton, canvas, denim, knit, wool) is matte with a
// soft grazing sheen and little specular; only leather, vinyl, rubber, metal and plastic trims get gloss; skin sits at a
// moderate roughness with skin shading (extras.skin_shading, read by the engine's glTF loading) and a faint sheen.
// rough is the glTF roughness factor: it multiplies the texture's own roughness (cloth sets about 0.88, leather about
// 0.5, skin about 0.58), so the effective values are: cloth 0.85-0.95, leather 0.45-0.6, vinyl about 0.3, skin about 0.55.
export const FABRICS = {
  cotton: { cls: 'cloth', set: 'fab_cloth', rough: 1, normal: 0.45 },
  canvas: { cls: 'cloth', set: 'fab_gi', rough: 1, normal: 0.6 },
  denim: { cls: 'cloth', set: 'fab_denim', rough: 1, normal: 0.5 },
  knit: { cls: 'cloth', set: 'fab_knit', rough: 1, normal: 0.5 },
  wool: { cls: 'cloth', set: 'fab_knit', rough: 1, normal: 0.4 },
  leather: { cls: 'leather', set: 'fab_leather', rough: 1, normal: 0.6 },
  vinyl: { cls: 'leather', set: 'fab_leather', rough: 0.6, normal: 0.3 },
  rubber: { cls: 'rubber', set: 'fab_cloth', rough: 0.75, normal: 0.3 },
  metal: { cls: 'metal', set: 'paint_metal', rough: 0.4, metal: 1 },
  plastic: { cls: 'gloss', set: 'fab_leather', rough: 0.3, metal: 0 },
  skin: { cls: 'skin', set: 'fab_skin', rough: 0.95, normal: 0.7 },
  eye: { cls: 'eye', set: null, rough: 1, metal: 0 },
  cavity: { cls: 'rubber', set: null, rough: 1, metal: 0 },
  enamel: { cls: 'enamel', set: null, rough: 0.3, metal: 0 },
  hair: { cls: 'hair', set: 'fab_hair', rough: 1, normal: 1.2 },
  brows: { cls: 'fur', set: 'fab_hair', rough: 1, normal: 0.6 },
  hairCard: { cls: 'hair', set: 'hair_card', rough: 1, normal: 0.4 },
};
const FAB_OF = { cloth: 'cotton', gi: 'canvas', denim: 'denim', knit: 'knit', leather: 'leather', vinyl: 'vinyl', wool: 'wool' };
/** the glTF extensions (and extras) for a material class and its base colour */
function shading(cls, tint, sp) {
  const t = tint ?? [1, 1, 1], ext = {};
  if (cls === 'cloth') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((c) => 0.01 + 0.25 * c), sheenRoughnessFactor: 0.9 }; ext.KHR_materials_specular = { specularFactor: 0.12 }; }
  if (cls === 'skin') {
    const sk = hexRaw(sp.colors.skin), dark = 1 - (sk[0] + sk[1] + sk[2]) / 3;
    ext.KHR_materials_sheen = { sheenColorFactor: [0.05 + 0.18 * t[0], 0.02 + 0.08 * t[1], 0.015 + 0.05 * t[2]], sheenRoughnessFactor: 0.65 };
    ext.KHR_materials_specular = { specularFactor: 0.5 }; ext.KHR_materials_ior = { ior: 1.4 };
    return { ext, extras: { skin_shading: { scatter: [lerp(1, 0.9, dark), lerp(0.36, 0.3, dark), lerp(0.24, 0.2, dark)], wrap: 0.5, translucency: lerp(0.38, 0.22, dark) } } };
  }
  if (cls === 'hair') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((c) => 0.02 + 0.15 * c), sheenRoughnessFactor: 0.6 }; ext.KHR_materials_specular = { specularFactor: 0.15 }; ext.KHR_materials_anisotropy = { anisotropyStrength: 0.3, anisotropyRotation: PI / 2 }; } // a strong anisotropic lobe on many crossed cards blows out against a low sun
  // brows and lashes: no anisotropy (their strips have no clean texture direction: a derivative tangent frame there goes
  // degenerate, and one bad value blooms over the whole face)
  if (cls === 'fur') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((c) => 0.02 + 0.15 * c), sheenRoughnessFactor: 0.6 }; ext.KHR_materials_specular = { specularFactor: 0.15 }; }
  if (cls === 'leather') ext.KHR_materials_clearcoat = { clearcoatFactor: 0.35, clearcoatRoughnessFactor: 0.38 };
  if (cls === 'eye') { ext.KHR_materials_clearcoat = { clearcoatFactor: 0.7, clearcoatRoughnessFactor: 0.09 }; /* a mirror coat put the sun into one blinding point that bloomed over the face */ ext.KHR_materials_ior = { ior: 1.376 }; }
  if (cls === 'enamel') ext.KHR_materials_specular = { specularFactor: 0.6 };
  if (cls === 'rubber') ext.KHR_materials_specular = { specularFactor: 0.3 };
  return { ext };
}
function materials(glb, sp) {
  const c = Object.fromEntries(Object.entries(sp.colors).map(([k, v]) => [k, Array.isArray(v) ? v : hex(v)]));
  // garment slot -> fabric, explicit per slot (a spec may override with shirtFab, pantsFab, outerFab, hatFab)
  const slot = {
    shirt: FAB_OF[sp.shirtFab ?? 'cloth'], pants: FAB_OF[sp.pantsFab ?? 'denim'], outer: FAB_OF[sp.outerFab ?? 'leather'], hat: FAB_OF[sp.hatFab ?? 'cloth'],
    band: 'cotton', wrap: 'cotton', mask: 'cotton', shoe: 'leather', sole: 'rubber', belt: sp.obi ? 'canvas' : 'leather', glove: 'leather', pad: 'vinyl', harness: 'leather',
    buckle: 'metal', metal: 'metal', helmet: sp.hair === 'hardhat' ? 'plastic' : 'metal', shades: 'plastic',
  };
  const P = (n, fab, o = {}) => { const F = FABRICS[fab], sh = shading(F.cls, o.tint, sp); return [n, glb.pbr(F.cls + '_' + n, TEX, o.set === undefined ? F.set : o.set, { rough: F.rough, normal: F.normal, metal: F.metal ?? 0, ext: sh.ext, extras: sh.extras, ...o })]; };
  const G = (n, tint, o = {}) => P(n, slot[n], { tint, ...o });
  return Object.fromEntries([
    P('skin', 'skin', { tint: c.skin }),
    P('face', 'skin', { set: 'face_' + sp.name, clamp: true, normal: 1.0 }),
    P('eye_' + sp.name, 'eye', { set: 'eye_' + sp.name, clamp: true, normal: 0.15 }),
    P('nail', 'plastic', { set: null, tint: c.skin.map((x) => x * 0.92 + 0.05), rough: 0.35 }),
    P('cavity', 'cavity', { tint: [0.045, 0.012, 0.014] }),
    P('teeth', 'enamel', { tint: [0.8, 0.76, 0.66] }), P('lash', 'brows', { tint: c.hair.map((x) => x * 0.25), doubleSided: true }), P('brow', 'brows', { tint: (c.brow ?? c.roots ?? c.hair).map((x) => x * 0.75) }),
    G('shirt', c.shirt), G('pants', c.pants), G('outer', c.outer ?? c.accent, { doubleSided: true }),
    G('shoe', c.shoes), G('sole', [0.035, 0.035, 0.035]), G('belt', c.belt || [0.03, 0.025, 0.02]), G('buckle', [0.85, 0.75, 0.45]),
    G('glove', c.glove ?? c.accent),
    P('hair', 'hair', { tint: c.hair, doubleSided: true }), P('stubble', 'hair', { tint: (c.roots ?? c.hair).map((x, i) => x * 0.35 + c.skin[i] * 0.45) }),
    P('hairCard', 'hairCard', { tint: c.hair, doubleSided: true, alphaMode: 'MASK', alphaCutoff: 0.32 }),
    G('hat', c.hat ?? c.accent), G('band', c.band ?? c.accent), G('wrap', c.wrap ?? [0.9, 0.88, 0.84], { normal: 0.8 }),
    G('harness', c.harness ?? [0.08, 0.06, 0.05]), G('mask', c.mask ?? c.accent), G('pad', c.pad ?? c.accent),
    G('helmet', c.helmet ?? c.hair), G('metal', c.metal ?? [0.5, 0.52, 0.55]), G('shades', [0.015, 0.015, 0.02]),
    P('trim', 'plastic', { set: null, tint: c.accent, emissive: (c.glow ?? [0, 0, 0]).map((x) => x * 0.6) }),
    P('reflect', 'plastic', { set: 'fab_cloth', tint: [0.7, 0.72, 0.68], rough: 0.5, emissive: [0.22, 0.24, 0.2] }),
    P('glow', 'plastic', { set: null, tint: [0.1, 0.4, 0.5], rough: 0.2, emissive: c.glow ?? [0.2, 1.4, 1.8] }),
  ]);
}

const HEAD_GROUPS = ['face', 'brow', 'lash'];
function emit(sp) {
  const glb = new Glb();
  writeFace(sp);
  const mats = materials(glb, sp);
  const { m: g, world, L } = build(sp);
  const { T } = skeleton(sp);
  // two skinned mesh nodes: Head (the face, brows and lashes, with the expression morph targets) and Body (the rest);
  // a morphed mesh draws on its own and reads every target per vertex, so only the head carries them
  const targets = sp.lod ? null : faceTargets(g, L, HEAD_GROUPS);
  const names = targets ? FACE_TARGETS.filter((n) => HEAD_GROUPS.some((h) => targets[n][h]?.any)) : [];
  const head = [], body = [];
  for (const [gg, mi] of g.prims(mats)) {
    const gname = [...g.groups].find(([, x]) => x === gg)[0];
    if (targets && HEAD_GROUPS.includes(gname)) { gg.targets = names.map((n) => targets[n][gname]); head.push([gg, mi]); } else body.push([gg, mi]);
  }
  const jointNodes = BONE_NAMES.map((n) => glb.node({ name: n, translation: T[n] }));
  BONE_NAMES.forEach((n, i) => { if (PARENT[n] !== -1) (glb.nodes[jointNodes[BI[PARENT[n]]]].children ||= []).push(jointNodes[i]); });
  const ibm = new Float32Array(BONE_NAMES.length * 16);
  BONE_NAMES.forEach((n, i) => { const w = world[n]; ibm.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -w[0], -w[1], -w[2], 1], i * 16); });
  glb.skins.push({ name: 'Armature', inverseBindMatrices: glb.acc(ibm, 'MAT4', 5126, 0), joints: jointNodes, skeleton: jointNodes[0] });
  const meshNodes = [glb.node({ name: 'Body', mesh: glb.mesh(sp.name + '_body', body), skin: 0 })];
  if (head.length) meshNodes.push(glb.node({ name: 'Face', mesh: glb.mesh(sp.name + '_head', head, null, names), skin: 0 }));
  glb.sceneNodes.push(...meshNodes, jointNodes[0]);
  glb.write(join(OUT, sp.name + '.glb'));
  console.log(sp.name, 'tris', g.tris(), 'targets', names.join(','));
}


// The four fighters. scale, bulk and shoulder are the numbers the hitboxes were tuned with (height, hip width, shoulder
// width); everything else is the look. Silhouette first: a gi with a headband and spiky hair, a huge bearded grappler
// in trunks and a harness, a lean runner in a cropped jacket with a mohawk, a long coat with a ponytail.
export const SPECS = [
  { name: 'bolt', face: { brow: 1.2, cheek: 1.1, browThick: 1.15, age: 0.25, lips: 0.95 }, eyes: 0x3a2412, scale: 1.0, bulk: 1.0, shoulder: 1.0, width: 1.08, chest: 1.08, arm: 1.12, leg: 1.06, head: 0.94, hand: 1.08, jaw: 1.15, abs: 1,
    hair: 'spiky', headband: true, wraps: true, stubble: true, top: 'none', outer: true, outerKind: 'gi', outerFab: 'gi', openFront: 0.2, sleeves: 'three',
    bottom: 'gi', pantsFab: 'gi', obi: true, shoes: 'slipper',
    colors: { skin: 0xc68c62, outer: 0xe8e2d2, pants: 0xe2dccc, shoes: 0x2a1a14, hair: 0x1c1410, accent: 0xb3201a, band: 0xc0221a, belt: 0x141214, wrap: 0xe8e4da } },
  { name: 'grit', face: { brow: 1.6, cheek: 1.2, width: 1.06, browThick: 1.3, browArch: 0.7, age: 0.8, lips: 0.9, chin: 1.25, eyeSize: 0.92, mouthW: 1.1 }, eyes: 0x4a5a6a, scale: 1.06, bulk: 1.38, shoulder: 1.25, width: 1.6, chest: 1.2, belly: 0.75, arm: 1.55, leg: 1.3, head: 1.0, hand: 1.3, jaw: 1.45, neck: 1.5, abs: 0.4,
    hair: 'none', goatee: true, stubble: true, scar: true, frown: true, top: 'none', harness: true, belt: true, champ: true, bottom: 'shorts', pantsFab: 'leather', kneepads: true, boots: true,
    bracers: true, wristband: true, gloves: true, fingerless: true,
    colors: { skin: 0xa8714c, pants: 0x2a2c34, shoes: 0x1a1612, hair: 0x2a1a10, accent: 0xe08a10, belt: 0x1e1814, metal: 0xc8a050, glove: 0x302822, pad: 0xd07a10, band: 0xe08a10, harness: 0x2a1e16 } },
  { name: 'vex', face: { brow: 0.9, cheek: 1.25, length: 1.04, browThick: 0.8, lips: 1.15, eyeSize: 1.05 }, eyes: 0x2a1a0e, scale: 0.94, bulk: 0.82, shoulder: 0.9, width: 0.86, chest: 0.95, arm: 0.95, leg: 1.0, head: 0.93, hand: 1.0, jaw: 0.9, nose: 0.9, abs: 0.6,
    hair: 'mohawk', mohawk: 1.35, shades: true, shadesUp: true, top: 'tank', shirtFab: 'knit', outer: true, outerKind: 'crop', outerFab: 'leather', openFront: 0.5, sleeves: 'long',
    bottom: 'joggers', pantsFab: 'cloth', gloves: true, fingerless: true, belt: true, war: 0x20c0e0,
    colors: { skin: 0x9a6a48, shirt: 0x12141a, pants: 0x16181e, shoes: 0x1a1c22, hair: 0x30d8f0, roots: 0x101014, accent: 0x18c8e8, outer: 0x101216, glove: 0x18181c, band: 0x18c8e8, belt: 0x0e0e10, glow: [0.3, 2.2, 2.8] } },
  { name: 'mara', face: { brow: 0.75, cheek: 1.2, width: 0.96, browThick: 0.7, browArch: 1.3, lips: 1.25, eyeSize: 1.08, chin: 0.85, mouthW: 0.95, makeup: { shadow: 0x5a4a3a, blush: 0xd07070 } }, scale: 0.97, bulk: 0.88, shoulder: 0.92, width: 0.86, chest: 0.92, bust: 1, hips: 1.08, arm: 0.88, leg: 1.0, head: 0.93, hand: 0.95, jaw: 0.72, nose: 0.8,
    hair: 'ponytail', hairRoot: [0, 0.2, -0.1], hairB: [0, -0.13, -0.06], lip: 0xa03a48, top: 'tank', shirtFab: 'cloth', outer: true, outerKind: 'longcoat', outerFab: 'leather', openFront: 0.42,
    bottom: 'tight', pantsFab: 'leather', boots: true, gloves: true, belt: true, pads: true, padMat: 'metal', epaulette: true,
    colors: { skin: 0xe6b894, shirt: 0xd8b040, pants: 0x1c1e22, shoes: 0x2a1c14, hair: 0x7a2a14, accent: 0xe0b030, outer: 0x1f4a34, glove: 0x2a1c14, belt: 0x241810, metal: 0xd8b860, band: 0xe0b030 }, eyes: 0x2a6a3a },
];
// The crowd: low detail people (a third of the points) on the same skeleton, posed by the game (stage.c) to watch and cheer.
const C = { lod: 0.4, scale: 1.0, bulk: 1.0, shoulder: 1.0 };
export const CROWD = [
  { ...C, name: 'crowd0', stubble: true, hair: 'beanie', hatFab: 'knit', top: 'hoodie', hood: true, bottom: 'jeans', width: 1.1, belly: 0.3, colors: { skin: 0xc0906a, shirt: 0x2a3a5a, pants: 0x34384a, shoes: 0x1c1c1c, hair: 0x1a1410, accent: 0x2a2a2a, hat: 0x8a2a1a, band: 0xd8d8d8 } },
  { ...C, name: 'crowd1', scale: 0.96, shoulder: 0.92, bulk: 0.9, hair: 'ponytail', hairRoot: [0, 0.2, -0.1], hairB: [0, -0.13, -0.06], bust: 0.8, hips: 1.06, jaw: 0.75, top: 'tee', outer: true, outerKind: 'jacket', outerFab: 'denim', openFront: 0.4, bottom: 'tight', pantsFab: 'cloth', lip: 0xa04050, colors: { skin: 0xe8c0a0, shirt: 0xe8e0d0, pants: 0x1a1a22, shoes: 0xe0e0e0, hair: 0x3a2010, accent: 0x5070a0, outer: 0x5a7aa8 } },
  { ...C, name: 'crowd2', scale: 1.03, bulk: 1.1, shoulder: 1.08, hair: 'capback', top: 'tee', bottom: 'cargo', pantsFab: 'cloth', width: 1.2, colors: { skin: 0x8a5838, shirt: 0xd8b020, pants: 0x55583a, shoes: 0x202020, hair: 0x101010, accent: 0x202020, hat: 0x1a1a1a } },
  { ...C, name: 'crowd3', hair: 'crop', top: 'longsleeve', outer: true, outerKind: 'jacket', outerFab: 'leather', openFront: 0.35, bottom: 'jeans', beard: true, colors: { skin: 0xd8a882, shirt: 0x2a2a2a, pants: 0x2c4a80, shoes: 0x3b2618, hair: 0x5a3a20, accent: 0x202020, outer: 0x3a2418 } },
  { ...C, name: 'crowd4', scale: 0.95, shoulder: 0.9, bulk: 0.92, hair: 'long', hairRoot: [0, 0.08, -0.12], hairB: [0, -0.16, -0.02], bust: 0.9, hips: 1.08, jaw: 0.72, top: 'tank', bottom: 'jeans', lip: 0x902838, colors: { skin: 0x9a6a48, shirt: 0xc02858, pants: 0x202838, shoes: 0x101010, hair: 0x101010, accent: 0xc02858 } },
  { ...C, name: 'crowd5', scale: 1.05, bulk: 1.15, shoulder: 1.1, hair: 'mohawk', mohawk: 0.9, top: 'none', outer: true, outerKind: 'vest', outerFab: 'leather', openFront: 0.55, bottom: 'tight', pantsFab: 'cloth', boots: true, colors: { skin: 0xe0b890, shirt: 0x2a2a30, pants: 0x3a2a4a, shoes: 0x101010, hair: 0xd03060, accent: 0xa0a0b0, outer: 0x18181c } },
  { ...C, name: 'crowd6', hair: 'crop', top: 'none', outer: true, outerKind: 'gi', outerFab: 'gi', openFront: 0.1, sleeves: 'three', bottom: 'gi', pantsFab: 'gi', obi: true, colors: { skin: 0xd09a70, outer: 0xe8e4d8, pants: 0xe4dfd2, shoes: 0x2a1a14, hair: 0x1a1410, accent: 0x202020, belt: 0x2a5a2a } },
  { ...C, name: 'crowd7', scale: 0.95, shoulder: 0.9, bulk: 0.9, hair: 'ponytail', hairRoot: [0, 0.2, -0.1], hairB: [0, -0.13, -0.06], bust: 0.7, hips: 1.05, jaw: 0.75, top: 'none', outer: true, outerKind: 'gi', outerFab: 'gi', openFront: 0.08, sleeves: 'three', bottom: 'gi', pantsFab: 'gi', obi: true, colors: { skin: 0xf0c8a8, outer: 0xe8e4d8, pants: 0xe4dfd2, shoes: 0x2a1a14, hair: 0x101010, accent: 0x202020, belt: 0x8a5a20 } },
  { ...C, name: 'crowd8', scale: 1.02, hair: 'none', beard: true, top: 'none', outer: true, outerKind: 'gi', outerFab: 'gi', openFront: 0.1, sleeves: 'long', bottom: 'gi', pantsFab: 'gi', obi: true, colors: { skin: 0xb07a52, outer: 0x2a2a34, pants: 0x2a2a34, shoes: 0x1a1a14, hair: 0xd0d0d0, accent: 0x202020, belt: 0x101010 } },
];
const only = process.argv.slice(2);
for (const s of [...SPECS, ...CROWD]) if ((!only.length && !s.lod) || only.includes(s.name) || (only.includes('crowd') && s.lod)) emit(s);
