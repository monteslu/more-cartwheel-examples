// Generates every fighter and enemy as a skinned glb (one bone layout, per-character proportions, the same clip
// library) into assets/models/. Run:
//   node tools/make-characters.mjs [name ...]
// Facing is +Z in the model, +X is the character's left. Rotation values in the poses are offsets from the rest pose
// (arms and legs hang along -Y): -x swings a limb forward, +z on the left side (+x) lifts it out sideways, +x on the
// spine leans forward, +x on a lower leg bends the knee, -x on a lower arm bends the elbow.
// Bones the clips never key (HairA, HairB, CoatL, CoatR, Belly) are driven at run time by the game's spring code.
import { mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb } from './gltf-writer.mjs';
import { SkinMesh, add, sub, mul, norm } from './charmesh.mjs';
import { writeSet } from './texlib.mjs';
import { faceParams, headModel, buildHead, shellGrid, paintFace, paintEye, FACE_BONES, EXPRESSIONS, FACE_TARGETS, faceTargets } from './face.mjs';
const HEAD_GROUPS = ['face', 'lash'];

const CARD_INWARD = process.env.BRAWLER_CARD_WINDING === 'inward';
const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'models');
mkdirSync(OUT, { recursive: true });

// ── the skeleton: names and parents are fixed; translations come from each character's proportions ──
const BONE_NAMES = ['Hips', 'Spine', 'Chest', 'Neck', 'Head', 'UpperArmL', 'LowerArmL', 'HandL', 'UpperArmR', 'LowerArmR', 'HandR',
  'UpperLegL', 'LowerLegL', 'FootL', 'UpperLegR', 'LowerLegR', 'FootR', 'Belly', 'HairA', 'HairB', 'CoatL', 'CoatR',
  // the face (expressions; the game blinks the lids) and the hands (two finger bones curl all four fingers, and the thumb)
  ...FACE_BONES, 'FingL1', 'FingL2', 'ThumbL', 'FingR1', 'FingR2', 'ThumbR'];
const PARENT = { Hips: -1, Spine: 'Hips', Chest: 'Spine', Neck: 'Chest', Head: 'Neck', UpperArmL: 'Chest', LowerArmL: 'UpperArmL', HandL: 'LowerArmL',
  UpperArmR: 'Chest', LowerArmR: 'UpperArmR', HandR: 'LowerArmR', UpperLegL: 'Hips', LowerLegL: 'UpperLegL', FootL: 'LowerLegL',
  UpperLegR: 'Hips', LowerLegR: 'UpperLegR', FootR: 'LowerLegR', Belly: 'Spine', HairA: 'Head', HairB: 'HairA', CoatL: 'Hips', CoatR: 'Hips',
  ...Object.fromEntries(FACE_BONES.map((n) => [n, 'Head'])), FingL1: 'HandL', FingL2: 'FingL1', ThumbL: 'HandL', FingR1: 'HandR', FingR2: 'FingR1', ThumbR: 'HandR' };
const HAND_BONES = ['FingL1', 'FingL2', 'ThumbL', 'FingR1', 'FingR2', 'ThumbR'];
const BI = Object.fromEntries(BONE_NAMES.map((n, i) => [n, i]));

/** per character bone translations (relative to the parent) */
function skeleton(sp) {
  const sw = sp.sw ?? 0.2, hw = sp.hw ?? 0.1, HS = sp.head ?? 1.15, al = sp.armLen ?? 1, ll = sp.legLen ?? 1;
  const hair = sp.hairRoot ?? [0, 0.17, -0.1];
  const T = {
    Hips: [0, 0.97 * ll + 0.03 * (1 - ll), 0], Spine: [0, 0.12, 0], Chest: [0, 0.2, 0], Neck: [0, 0.24, 0], Head: [0, 0.08, 0],
    UpperArmL: [sw, 0.17, 0], LowerArmL: [0, -0.28 * al, 0], HandL: [0, -0.26 * al, 0],
    UpperArmR: [-sw, 0.17, 0], LowerArmR: [0, -0.28 * al, 0], HandR: [0, -0.26 * al, 0],
    UpperLegL: [hw, -0.06, 0], LowerLegL: [0, -0.44 * ll, 0], FootL: [0, -0.43 * ll, 0],
    UpperLegR: [-hw, -0.06, 0], LowerLegR: [0, -0.44 * ll, 0], FootR: [0, -0.43 * ll, 0],
    Belly: [0, 0.0, 0.1], HairA: [hair[0], hair[1] * HS, hair[2] * HS], HairB: [0, -0.15, -0.03], CoatL: [0.11, -0.04, -0.1], CoatR: [-0.11, -0.04, -0.1],
  };
  if (sp.hairB) T.HairB = sp.hairB;
  const world = {};
  const body = BONE_NAMES.filter((n) => T[n]);
  for (const n of body) { const p = PARENT[n] === -1 ? [0, 0, 0] : world[PARENT[n]]; world[n] = add(p, T[n]); }
  // the face bones sit where the sculpted head puts its features; the hand bones at the knuckles and the thumb
  const M = headOf(sp, world);
  for (const n of FACE_BONES) world[n] = M.bones[n];
  for (const sx of [1, -1]) {
    const sd = sx > 0 ? 'L' : 'R', h = handGeom(sp, sx, world);
    world['Fing' + sd + '1'] = h.k1; world['Fing' + sd + '2'] = h.k2; world['Thumb' + sd] = h.tb;
  }
  for (const n of BONE_NAMES) if (!T[n]) T[n] = sub(world[n], world[PARENT[n]]);
  return { T, world, M };
}

/** the head: size, envelope rings (hats and hair are built on the same rings) and the sculpted face model */
const HEADS = new Map();
function headOf(sp, world) {
  if (HEADS.has(sp.name)) return HEADS.get(sp.name);
  const HS = sp.head ?? 1.15, yHd = world.Head[1], jawW = sp.jaw ?? 1;
  const hc = [0, yHd + 0.125 * HS, 0.008 * HS];
  const R = 0.11 * HS, RZ = 0.127 * HS, HY = 0.142 * HS;
  const ring = (t, k = 1, add2 = 0) => {
    const s = Math.sqrt(Math.max(0, 1 - t * t));
    const jaw = t < -0.15 ? lerp(1, 0.72 + 0.2 * jawW, ss(-0.15, -0.95, t)) : 1;
    const zoff = t < -0.25 ? 0.022 * HS * ss(-0.25, -0.9, t) : 0;
    return { c: [0, hc[1] + HY * t, hc[2] + zoff], U: [1, 0, 0], V: [0, 0, 1], ru: Math.max(0.002, R * s * jaw * k + add2), rv: Math.max(0.002, RZ * s * lerp(1, 0.9, ss(-0.1, -0.35, t)) * k + add2), e: lerp(2.3, 2.6 + 0.6 * jawW, ss(-0.15, -0.5, t)),
      mod: (a) => 1 + 0.06 * gauss(t - 0.15, 0.25) * gauss(wrapA(a - BACK), 1.0) }; // the back of the skull (every term smooth in t: the face is dense)
  };
  const M = headModel({ hc, R, RZ, HY, HS, ring }, faceParams(sp.face));
  HEADS.set(sp.name, M);
  return M;
}
/** hand layout: palm, knuckle line (first finger bone), middle joints (second finger bone), thumb root */
function handGeom(sp, sx, world) {
  const H = world['Hand' + (sx > 0 ? 'L' : 'R')], s = (sp.hand ?? 1.35) * (sp.gloves ? 1.2 : 1);
  const hx = H[0] + sx * 0.002, hy = H[1], ky = hy - 0.078 * s, kx = hx - sx * 0.004 * s;
  return { s, hx, hy, k1: [kx, ky, 0], k2: [kx, ky - 0.042 * s, 0], tb: [hx - sx * 0.016 * s, hy - 0.024 * s, 0.03 * s] };
}

// ── poses and clips ─────────────────────────────────────────────────────
const merge = (...ps) => {
  const out = {};
  for (const p of ps) for (const [k, v] of Object.entries(p)) out[k] = { ...(out[k] || {}), ...v };
  return out;
};
const lerp = (a, b, t) => a + (b - a) * t;
const lerpPose = (a, b, t) => {
  const out = {};
  for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) {
    const A = a[k] || {}, B = b[k] || {}, o = {};
    for (const f of ['r', 'p']) {
      if (!A[f] && !B[f]) continue;
      const x = A[f] || [0, 0, 0], y = B[f] || [0, 0, 0];
      o[f] = [lerp(x[0], y[0], t), lerp(x[1], y[1], t), lerp(x[2], y[2], t)];
    }
    out[k] = o;
  }
  return out;
};
const EASE = {
  io: (u) => u * u * (3 - 2 * u),
  out: (u) => 1 - Math.pow(1 - u, 3), // a snap: most of the move in the first frames
  in: (u) => u * u * u,
  back: (u) => { const c = 1.9; const v = u - 1; return 1 + (c + 1) * v * v * v + c * v * v; }, // overshoot then settle
  lin: (u) => u,
};
/** keyframes [[t, pose, ease], ...]: the ease shapes the move INTO that key */
const keyed = (keys) => (t) => {
  if (t <= keys[0][0]) return keys[0][1];
  for (let i = 1; i < keys.length; i++) {
    if (t <= keys[i][0]) {
      const [t0, a] = keys[i - 1], [t1, b, e] = keys[i];
      const u = (t - t0) / (t1 - t0);
      return lerpPose(a, b, (EASE[e] || EASE.io)(u));
    }
  }
  return keys[keys.length - 1][1];
};
const S = Math.sin, C = Math.cos, PI = Math.PI;
const REST = {};
// a boxer's guard: fists up by the chin, elbows in, knees soft, weight on the balls of the feet
const GUARD = {
  Hips: { p: [0, -0.07, 0], r: [0, 0.18, 0] }, Spine: { r: [0.12, -0.08, 0] }, Chest: { r: [0.06, -0.1, 0] }, Neck: { r: [-0.04, 0, 0] }, Head: { r: [-0.06, 0.05, 0] },
  UpperArmL: { r: [-0.55, -0.15, 0.32] }, LowerArmL: { r: [-2.15, 0, 0] }, HandL: { r: [0.2, 0, 0] },
  UpperArmR: { r: [-0.75, 0.15, -0.25] }, LowerArmR: { r: [-2.25, 0, 0] }, HandR: { r: [0.2, 0, 0] },
  UpperLegL: { r: [-0.38, -0.1, 0.1] }, LowerLegL: { r: [0.5, 0, 0] }, FootL: { r: [-0.1, 0.1, 0] },
  UpperLegR: { r: [0.22, -0.2, -0.12] }, LowerLegR: { r: [0.45, 0, 0] }, FootR: { r: [-0.25, 0.2, 0] },
};
const G = (o) => merge(GUARD, o);

/** clip definitions: name, length, loop, pose(t seconds) */
const CLIPS = [];
const clip = (name, len, loop, pose, only) => CLIPS.push({ name, len, loop, pose, only }); // only: the characters that carry it (default every one)

// idles: breathing and a weight shift; a neck roll; a bouncing boxer
clip('idle', 2.4, true, (t) => {
  const w = (t / 2.4) * PI * 2, b = S(w * 2), sh = S(w);
  return G({
    Hips: { p: [sh * 0.018, -0.07 + b * 0.008, 0], r: [0, 0.18, sh * 0.03] }, Spine: { r: [0.12 - b * 0.015, -0.08, -sh * 0.02] },
    Chest: { r: [0.06 - b * 0.03, -0.1, -sh * 0.015] }, Head: { r: [-0.06 + b * 0.02, 0.05 + S(w + 1) * 0.08, 0] },
    UpperArmL: { r: [-0.55 + b * 0.05, -0.15, 0.32] }, UpperArmR: { r: [-0.75 - b * 0.05, 0.15, -0.25] },
    LowerArmL: { r: [-2.15 - b * 0.06, 0, 0] }, LowerArmR: { r: [-2.25 + b * 0.06, 0, 0] },
    UpperLegL: { r: [-0.38, -0.1, 0.1 - sh * 0.03] }, UpperLegR: { r: [0.22, -0.2, -0.12 - sh * 0.03] },
  });
});
clip('idle2', 2.0, true, keyed([
  [0, GUARD],
  [0.4, G({ Chest: { r: [-0.12, -0.1, 0] }, Head: { r: [0.1, 0, 0.35] }, UpperArmL: { r: [-0.35, -0.15, 0.45] }, UpperArmR: { r: [-0.4, 0.15, -0.45] }, LowerArmL: { r: [-1.7, 0, 0] }, LowerArmR: { r: [-1.7, 0, 0] } })],
  [0.9, G({ Chest: { r: [-0.15, -0.1, 0] }, Head: { r: [0.1, 0, -0.35] }, UpperArmL: { r: [-0.35, -0.15, 0.45] }, UpperArmR: { r: [-0.4, 0.15, -0.45] }, LowerArmL: { r: [-1.7, 0, 0] }, LowerArmR: { r: [-1.7, 0, 0] } }), 'io'],
  [1.25, G({ Chest: { r: [-0.08, -0.1, 0] }, Head: { r: [-0.2, 0, 0] }, Spine: { r: [0.05, -0.08, 0] } }), 'io'],
  [2.0, GUARD, 'io']]));
clip('idle3', 0.8, true, (t) => {
  const w = (t / 0.8) * PI * 2, b = Math.abs(S(w)), s2 = S(w);
  return G({
    Hips: { p: [0, -0.07 + b * 0.03, 0], r: [0, 0.18, 0] }, Chest: { r: [0.06 + b * 0.04, -0.1 + s2 * 0.06, 0] }, Head: { r: [-0.06 - b * 0.03, 0.05, 0] },
    UpperArmL: { r: [-0.55 - b * 0.08, -0.15, 0.32] }, UpperArmR: { r: [-0.75 + b * 0.06, 0.15, -0.25] },
    LowerLegL: { r: [0.5 - b * 0.25, 0, 0] }, LowerLegR: { r: [0.45 - b * 0.25, 0, 0] }, FootL: { r: [-0.1 + b * 0.25, 0.1, 0] }, FootR: { r: [-0.25 + b * 0.25, 0.2, 0] },
  });
});

// locomotion: per leg phase f (0 heel strike, 0.5 toe off), stance and swing shaped separately
function legPose(f, run) {
  const st = f < (run ? 0.38 : 0.5);
  let hip, knee, foot;
  if (st) {
    const u = f / (run ? 0.38 : 0.5);
    hip = lerp(run ? -0.6 : -0.42, run ? 0.55 : 0.35, u);
    knee = (run ? 0.35 : 0.08) + (run ? 0.35 : 0.18) * S(u * PI);
    foot = u < 0.2 ? lerp(-0.3, 0, u / 0.2) : u > 0.75 ? lerp(0, run ? 0.6 : 0.45, (u - 0.75) / 0.25) : 0;
  } else {
    const u = (f - (run ? 0.38 : 0.5)) / (run ? 0.62 : 0.5);
    hip = lerp(run ? 0.55 : 0.35, run ? -0.75 : -0.42, EASE.io(u)) + (run ? -0.25 * S(u * PI) : 0);
    knee = (run ? 0.35 : 0.08) + (run ? 1.9 : 1.05) * Math.pow(S(Math.min(1, u * 1.25) * PI), 1.2) * (u < 0.8 ? 1 : 1 - (u - 0.8) / 0.4);
    foot = lerp(0.3, -0.2, u);
  }
  return { hip, knee, foot };
}
const walkPose = (t, period, run) => {
  const f = (t / period) % 1, L = legPose(f, run), R = legPose((f + 0.5) % 1, run);
  const ph = f * PI * 2, bob = C(ph * 2);
  return merge(run ? GUARD : REST, {
    Hips: { p: [0, (run ? -0.06 : -0.015) - bob * (run ? 0.03 : 0.018), 0], r: [0, S(ph) * (run ? 0.14 : 0.1), S(ph) * 0.04] },
    Spine: { r: [run ? 0.22 : 0.04, -S(ph) * 0.08, -S(ph) * 0.03] }, Chest: { r: [run ? 0.06 : 0, -S(ph) * (run ? 0.2 : 0.14), 0] },
    Head: { r: [run ? -0.15 : -0.02, S(ph) * 0.1, 0] },
    UpperLegL: { r: [L.hip, 0, 0.04] }, LowerLegL: { r: [L.knee, 0, 0] }, FootL: { r: [L.foot, 0, 0] },
    UpperLegR: { r: [R.hip, 0, -0.04] }, LowerLegR: { r: [R.knee, 0, 0] }, FootR: { r: [R.foot, 0, 0] },
    UpperArmL: { r: [S(ph) * (run ? 0.85 : 0.5) + (run ? -0.3 : 0), 0, run ? 0.2 : 0.14] }, LowerArmL: { r: [-(run ? 1.7 : 0.35) - Math.max(0, -S(ph)) * 0.4, 0, 0] },
    UpperArmR: { r: [-S(ph) * (run ? 0.85 : 0.5) + (run ? -0.3 : 0), 0, run ? -0.2 : -0.14] }, LowerArmR: { r: [-(run ? 1.7 : 0.35) - Math.max(0, S(ph)) * 0.4, 0, 0] },
  });
};
clip('walk', 1, true, (t) => {
  const p = walkPose(t, 1, false);
  // a little swagger: shoulders roll with the step, fists stay loose
  return merge(p, { HandL: { r: [0.15, 0, 0] }, HandR: { r: [0.15, 0, 0] } });
});
clip('run', 0.62, true, (t) => walkPose(t, 0.62, true));

// strikes: anticipation (wind up), a snap into the hit (ease out), overshoot, then a settle back to guard
const JAB_EXT = G({ Hips: { p: [0, -0.1, 0.12], r: [0, -0.25, 0] }, Spine: { r: [0.16, 0.3, 0] }, Chest: { r: [0.1, 0.42, 0] }, Head: { r: [0.0, -0.3, 0] },
  UpperArmR: { r: [-1.55, 0.05, -0.08] }, LowerArmR: { r: [-0.08, 0, 0] }, HandR: { r: [0, 0, 0] }, UpperArmL: { r: [-0.7, -0.15, 0.25] }, LowerArmL: { r: [-2.3, 0, 0] },
  UpperLegL: { r: [-0.7, -0.1, 0.12] }, LowerLegL: { r: [0.75, 0, 0] }, UpperLegR: { r: [0.4, -0.2, -0.12] }, LowerLegR: { r: [0.2, 0, 0] } });
clip('punch1', 0.42, false, keyed([
  [0, GUARD],
  [0.09, G({ Hips: { p: [0, -0.1, -0.04], r: [0, 0.3, 0] }, Spine: { r: [0.14, -0.25, 0] }, Chest: { r: [0.08, -0.32, 0] }, UpperArmR: { r: [-0.55, 0.3, -0.35] }, LowerArmR: { r: [-2.45, 0, 0] } })],
  [0.15, JAB_EXT, 'out'],
  [0.2, merge(JAB_EXT, { Chest: { r: [0.12, 0.5, 0] }, Head: { r: [0.04, -0.36, 0] } }), 'io'],
  [0.29, JAB_EXT, 'io'],
  [0.42, GUARD, 'io']]));
const CROSS_EXT = G({ Hips: { p: [0, -0.1, 0.14], r: [0, 0.55, 0] }, Spine: { r: [0.16, -0.35, 0] }, Chest: { r: [0.1, -0.5, 0] }, Head: { r: [0.0, 0.32, 0] },
  UpperArmL: { r: [-1.55, -0.05, 0.08] }, LowerArmL: { r: [-0.08, 0, 0] }, HandL: { r: [0, 0, 0] }, UpperArmR: { r: [-0.85, 0.2, -0.2] }, LowerArmR: { r: [-2.4, 0, 0] },
  UpperLegR: { r: [0.35, -0.3, -0.12] }, LowerLegR: { r: [0.3, 0, 0] }, FootR: { r: [0.45, 0.3, 0] } });
clip('punch2', 0.45, false, keyed([
  [0, GUARD],
  [0.1, G({ Hips: { p: [0, -0.11, -0.05], r: [0, 0.05, 0] }, Spine: { r: [0.14, 0.2, 0] }, Chest: { r: [0.08, 0.32, 0] }, UpperArmL: { r: [-0.45, -0.3, 0.4] }, LowerArmL: { r: [-2.45, 0, 0] } })],
  [0.17, CROSS_EXT, 'out'],
  [0.23, merge(CROSS_EXT, { Chest: { r: [0.14, -0.6, 0] }, Spine: { r: [0.2, -0.42, 0] } }), 'io'],
  [0.32, CROSS_EXT, 'io'],
  [0.45, GUARD, 'io']]));
const UPPER_TOP = G({ Hips: { p: [0, 0.06, 0.1], r: [0, 0.2, 0] }, Spine: { r: [-0.3, 0.25, 0] }, Chest: { r: [-0.3, 0.35, 0] }, Head: { r: [-0.35, -0.2, 0] },
  UpperArmR: { r: [-2.9, 0, -0.15] }, LowerArmR: { r: [-0.5, 0, 0] }, UpperArmL: { r: [-0.2, 0, 0.55] }, LowerArmL: { r: [-1.2, 0, 0] },
  UpperLegL: { r: [-0.15, 0, 0.1] }, LowerLegL: { r: [0.15, 0, 0] }, FootL: { r: [0.45, 0, 0] }, UpperLegR: { r: [0.25, 0, -0.1] }, LowerLegR: { r: [0.6, 0, 0] }, FootR: { r: [0.6, 0, 0] } });
clip('finisher', 0.7, false, keyed([
  [0, GUARD],
  [0.2, G({ Hips: { p: [0, -0.3, -0.04], r: [0, 0.35, 0] }, Spine: { r: [0.45, -0.3, 0] }, Chest: { r: [0.15, -0.3, 0] }, UpperArmR: { r: [0.35, 0, -0.2] }, LowerArmR: { r: [-1.0, 0, 0] },
    UpperLegL: { r: [-1.0, 0, 0.12] }, LowerLegL: { r: [1.35, 0, 0] }, UpperLegR: { r: [0.5, 0, -0.1] }, LowerLegR: { r: [1.1, 0, 0] } }), 'io'],
  [0.31, UPPER_TOP, 'out'],
  [0.38, merge(UPPER_TOP, { Chest: { r: [-0.4, 0.45, 0] }, Head: { r: [-0.45, -0.25, 0] } }), 'io'],
  [0.5, UPPER_TOP, 'io'],
  [0.7, GUARD, 'io']]));
const KICK_EXT = G({ Hips: { p: [0, -0.1, 0.05], r: [0, -0.2, 0] }, Spine: { r: [-0.25, 0, 0] }, Chest: { r: [-0.15, 0.1, 0] }, Head: { r: [0.25, 0, 0] },
  UpperLegR: { r: [-1.6, 0, -0.1] }, LowerLegR: { r: [0.1, 0, 0] }, FootR: { r: [0.5, 0, 0] }, UpperLegL: { r: [0.15, 0, 0.1] }, LowerLegL: { r: [0.35, 0, 0] },
  UpperArmL: { r: [-0.3, 0, 0.75] }, LowerArmL: { r: [-1.2, 0, 0] }, UpperArmR: { r: [0.4, 0, -0.5] }, LowerArmR: { r: [-1.4, 0, 0] } });
clip('kick', 0.55, false, keyed([
  [0, GUARD],
  [0.13, G({ Hips: { p: [0, -0.1, -0.05], r: [0, 0.0, 0] }, Spine: { r: [-0.1, 0, 0] }, UpperLegR: { r: [-1.45, 0, -0.1] }, LowerLegR: { r: [2.0, 0, 0] }, FootR: { r: [0.3, 0, 0] }, UpperLegL: { r: [0.1, 0, 0.1] }, LowerLegL: { r: [0.45, 0, 0] } })],
  [0.21, KICK_EXT, 'out'],
  [0.27, merge(KICK_EXT, { UpperLegR: { r: [-1.75, 0, -0.1] }, Spine: { r: [-0.32, 0, 0] } }), 'io'],
  [0.37, KICK_EXT, 'io'],
  [0.45, G({ UpperLegR: { r: [-1.2, 0, -0.1] }, LowerLegR: { r: [1.7, 0, 0] } }), 'io'],
  [0.55, GUARD, 'io']]));
const FLY = G({ Hips: { p: [0, 0, 0], r: [-0.2, 0, 0] }, Spine: { r: [-0.35, 0, 0] }, Chest: { r: [-0.2, 0, 0] }, Head: { r: [0.3, 0, 0] },
  UpperLegR: { r: [-1.6, 0, -0.1] }, LowerLegR: { r: [0.05, 0, 0] }, FootR: { r: [0.5, 0, 0] }, UpperLegL: { r: [-0.3, 0, 0.1] }, LowerLegL: { r: [1.9, 0, 0] },
  UpperArmL: { r: [-0.6, 0, 1.0] }, UpperArmR: { r: [0.3, 0, -1.0] }, LowerArmL: { r: [-0.8, 0, 0] }, LowerArmR: { r: [-0.8, 0, 0] } });
clip('jumpkick', 0.8, false, keyed([
  [0, G({ Hips: { p: [0, -0.18, 0] }, UpperLegL: { r: [-0.7, 0, 0.1] }, LowerLegL: { r: [1.2, 0, 0] }, UpperLegR: { r: [-0.7, 0, -0.1] }, LowerLegR: { r: [1.2, 0, 0] } })],
  [0.1, G({ UpperLegL: { r: [-1.2, 0, 0.1] }, LowerLegL: { r: [2.0, 0, 0] }, UpperLegR: { r: [-1.3, 0, -0.1] }, LowerLegR: { r: [2.0, 0, 0] }, Spine: { r: [0.3, 0, 0] } }), 'out'],
  [0.2, FLY, 'out'],
  [0.6, merge(FLY, { UpperLegR: { r: [-1.5, 0, -0.1] }, Spine: { r: [-0.3, 0, 0] } })],
  [0.8, G({ Hips: { p: [0, -0.15, 0] }, UpperLegL: { r: [-0.6, 0, 0.1] }, LowerLegL: { r: [1.0, 0, 0] } })]]));
clip('jump', 0.8, false, keyed([
  [0, G({ Hips: { p: [0, -0.22, 0] }, Spine: { r: [0.35, 0, 0] }, UpperLegL: { r: [-0.9, 0, 0.1] }, LowerLegL: { r: [1.5, 0, 0] }, UpperLegR: { r: [-0.9, 0, -0.1] }, LowerLegR: { r: [1.5, 0, 0] }, UpperArmL: { r: [0.5, 0, 0.3] }, UpperArmR: { r: [0.5, 0, -0.3] } })],
  [0.18, G({ Spine: { r: [-0.1, 0, 0] }, UpperArmL: { r: [-1.4, 0, 0.6] }, UpperArmR: { r: [-1.4, 0, -0.6] }, LowerArmL: { r: [-0.6, 0, 0] }, LowerArmR: { r: [-0.6, 0, 0] }, UpperLegL: { r: [-0.2, 0, 0.1] }, LowerLegL: { r: [0.3, 0, 0] }, UpperLegR: { r: [0.15, 0, -0.1] }, LowerLegR: { r: [0.6, 0, 0] }, FootL: { r: [0.5, 0, 0] }, FootR: { r: [0.5, 0, 0] } }), 'out'],
  [0.45, G({ UpperLegL: { r: [-1.0, 0, 0.1] }, LowerLegL: { r: [1.6, 0, 0] }, UpperLegR: { r: [-0.6, 0, -0.1] }, LowerLegR: { r: [1.3, 0, 0] }, UpperArmL: { r: [-0.4, 0, 0.9] }, UpperArmR: { r: [-0.4, 0, -0.9] } })],
  [0.8, G({ Hips: { p: [0, -0.2, 0] }, Spine: { r: [0.3, 0, 0] }, UpperLegL: { r: [-0.7, 0, 0.1] }, LowerLegL: { r: [1.2, 0, 0] }, UpperLegR: { r: [-0.5, 0, -0.1] }, LowerLegR: { r: [1.1, 0, 0] } })]]));

// grabs: the grabber holds the victim in front; knees; a throw over the shoulder; the victim struggles
const HOLD = G({ Hips: { p: [0, -0.12, 0] }, Spine: { r: [0.25, 0, 0] }, Chest: { r: [0.1, 0, 0] }, UpperArmL: { r: [-1.3, 0, 0.15] }, LowerArmL: { r: [-1.0, 0, 0] }, UpperArmR: { r: [-1.3, 0, -0.15] }, LowerArmR: { r: [-1.0, 0, 0] } });
clip('grab', 0.9, true, (t) => { const b = S((t / 0.9) * PI * 2); return merge(HOLD, { Chest: { r: [0.1 + b * 0.04, b * 0.06, 0] }, UpperArmL: { r: [-1.3 + b * 0.06, 0, 0.15] }, UpperArmR: { r: [-1.3 - b * 0.06, 0, -0.15] } }); });
clip('knee', 0.5, false, keyed([
  [0, HOLD],
  [0.08, merge(HOLD, { Hips: { p: [0, -0.1, -0.06] }, Spine: { r: [0.1, 0, 0] }, UpperLegR: { r: [0.3, 0, -0.1] }, LowerLegR: { r: [0.8, 0, 0] } })],
  [0.16, merge(HOLD, { Hips: { p: [0, -0.06, 0.06] }, Spine: { r: [0.45, 0, 0] }, UpperArmL: { r: [-1.0, 0, 0.15] }, UpperArmR: { r: [-1.0, 0, -0.15] }, UpperLegR: { r: [-1.75, 0, -0.1] }, LowerLegR: { r: [1.9, 0, 0] }, FootR: { r: [0.5, 0, 0] } }), 'out'],
  [0.26, merge(HOLD, { Spine: { r: [0.4, 0, 0] }, UpperLegR: { r: [-1.5, 0, -0.1] }, LowerLegR: { r: [1.9, 0, 0] } }), 'io'],
  [0.5, HOLD, 'io']]));
clip('throw', 0.7, false, keyed([
  [0, HOLD],
  [0.25, G({ Hips: { p: [0, -0.18, -0.05] }, Spine: { r: [-0.4, 0, 0] }, Chest: { r: [-0.3, 0, 0] }, Head: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-2.8, 0, 0.15] }, LowerArmL: { r: [-0.3, 0, 0] }, UpperArmR: { r: [-2.8, 0, -0.15] }, LowerArmR: { r: [-0.3, 0, 0] }, UpperLegL: { r: [-0.5, 0, 0.12] }, LowerLegL: { r: [0.9, 0, 0] }, UpperLegR: { r: [-0.5, 0, -0.12] }, LowerLegR: { r: [0.9, 0, 0] } })],
  [0.38, G({ Hips: { p: [0, -0.2, 0.12] }, Spine: { r: [0.75, 0, 0] }, Chest: { r: [0.3, 0, 0] }, Head: { r: [0.3, 0, 0] }, UpperArmL: { r: [-0.4, 0, 0.15] }, LowerArmL: { r: [-0.2, 0, 0] }, UpperArmR: { r: [-0.4, 0, -0.15] }, LowerArmR: { r: [-0.2, 0, 0] } }), 'out'],
  [0.5, G({ Hips: { p: [0, -0.2, 0.12] }, Spine: { r: [0.65, 0, 0] }, UpperArmL: { r: [-0.6, 0, 0.2] }, UpperArmR: { r: [-0.6, 0, -0.2] } }), 'io'],
  [0.7, GUARD, 'io']]));
clip('grabbed', 0.8, true, (t) => {
  const s = S((t / 0.8) * PI * 2), c = C((t / 0.8) * PI * 4);
  return merge(REST, { Hips: { p: [0, -0.12, 0] }, Spine: { r: [0.4, s * 0.1, 0] }, Chest: { r: [0.1, -s * 0.12, 0] }, Head: { r: [0.35 + c * 0.1, s * 0.2, 0] },
    UpperArmL: { r: [-0.6 + s * 0.5, 0, 0.5] }, UpperArmR: { r: [-0.6 - s * 0.5, 0, -0.5] }, LowerArmL: { r: [-0.9, 0, 0] }, LowerArmR: { r: [-0.9, 0, 0] },
    UpperLegL: { r: [-0.4 + s * 0.35, 0, 0.1] }, UpperLegR: { r: [-0.4 - s * 0.35, 0, -0.1] }, LowerLegL: { r: [0.7, 0, 0] }, LowerLegR: { r: [0.7, 0, 0] } });
});
clip('thrown', 0.7, true, (t) => {
  const s = S((t / 0.7) * PI * 2);
  return merge(REST, { Hips: { r: [-0.9, 0, s * 0.1] }, Spine: { r: [-0.3, 0, 0] }, Head: { r: [-0.4, 0, 0] }, UpperArmL: { r: [-2.4, 0, 0.6 + s * 0.3] }, UpperArmR: { r: [-2.4, 0, -0.6 - s * 0.3] }, LowerArmL: { r: [-0.4, 0, 0] }, LowerArmR: { r: [-0.4, 0, 0] },
    UpperLegL: { r: [-0.7 + s * 0.3, 0, 0.25] }, UpperLegR: { r: [-0.3 - s * 0.3, 0, -0.25] }, LowerLegL: { r: [1.0, 0, 0] }, LowerLegR: { r: [0.7, 0, 0] } });
});

// hurt: a head snap, a body blow that folds the target, a big stagger; each recoils, overshoots and settles
clip('hit', 0.32, false, keyed([
  [0, GUARD],
  [0.05, G({ Hips: { p: [0, -0.08, -0.08], r: [0, 0.1, 0] }, Spine: { r: [-0.25, 0, 0] }, Chest: { r: [-0.25, -0.2, 0] }, Neck: { r: [-0.25, 0, 0] }, Head: { r: [-0.55, 0.35, 0.15] }, UpperArmL: { r: [-0.3, 0, 0.6] }, UpperArmR: { r: [-0.3, 0, -0.6] }, LowerArmL: { r: [-0.9, 0, 0] }, LowerArmR: { r: [-0.9, 0, 0] } }), 'out'],
  [0.17, G({ Spine: { r: [0.2, 0, 0] }, Head: { r: [0.15, 0.1, 0] } }), 'io'],
  [0.32, GUARD, 'io']]));
clip('hit_gut', 0.36, false, keyed([
  [0, GUARD],
  [0.06, G({ Hips: { p: [0, -0.14, -0.1], r: [0, 0, 0] }, Spine: { r: [0.65, 0, 0] }, Chest: { r: [0.35, 0, 0] }, Head: { r: [-0.35, 0, 0] }, UpperArmL: { r: [-0.6, 0, 0.1] }, UpperArmR: { r: [-0.6, 0, -0.1] }, LowerArmL: { r: [-1.9, 0, 0] }, LowerArmR: { r: [-1.9, 0, 0] }, UpperLegL: { r: [-0.5, 0, 0.1] }, LowerLegL: { r: [0.8, 0, 0] }, UpperLegR: { r: [-0.1, 0, -0.1] }, LowerLegR: { r: [0.7, 0, 0] } }), 'out'],
  [0.2, G({ Spine: { r: [0.4, 0, 0] }, Chest: { r: [0.2, 0, 0] }, Head: { r: [-0.1, 0, 0] } }), 'io'],
  [0.36, GUARD, 'io']]));
clip('hit_hard', 0.42, false, keyed([
  [0, GUARD],
  [0.07, G({ Hips: { p: [0, -0.1, -0.18], r: [-0.25, 0.3, 0] }, Spine: { r: [-0.4, 0.35, 0] }, Chest: { r: [-0.3, 0.35, 0] }, Head: { r: [-0.65, 0.5, 0.2] }, UpperArmL: { r: [-0.2, 0, 1.1] }, UpperArmR: { r: [-0.5, 0, -0.8] }, LowerArmL: { r: [-0.4, 0, 0] }, LowerArmR: { r: [-0.6, 0, 0] }, UpperLegL: { r: [0.3, 0, 0.15] }, LowerLegL: { r: [0.3, 0, 0] } }), 'out'],
  [0.22, G({ Hips: { p: [0, -0.12, -0.05] }, Spine: { r: [0.3, -0.1, 0] }, Head: { r: [0.2, -0.1, 0] } }), 'io'],
  [0.42, GUARD, 'io']]));
const LYING = merge(REST, {
  Hips: { p: [0, -0.82, 0], r: [-1.5708, 0, 0] }, Head: { r: [0.2, 0.3, 0] }, Spine: { r: [-0.05, 0, 0] },
  UpperArmL: { r: [-0.4, 0, 0.7] }, UpperArmR: { r: [-0.3, 0, -0.8] }, LowerArmL: { r: [-0.6, 0, 0] }, LowerArmR: { r: [-0.4, 0, 0] },
  UpperLegL: { r: [0.05, 0, 0.2] }, UpperLegR: { r: [-0.4, 0, -0.25] }, LowerLegL: { r: [0.15, 0, 0] }, LowerLegR: { r: [0.9, 0, 0] }, FootL: { r: [0.4, 0, 0] }, FootR: { r: [0.2, 0, 0] },
});
clip('knockdown', 0.85, false, keyed([
  [0, GUARD],
  [0.12, merge(GUARD, { Hips: { p: [0, 0.05, -0.2], r: [-0.55, 0, 0] }, Spine: { r: [-0.35, 0, 0] }, Head: { r: [-0.6, 0, 0] }, UpperArmL: { r: [-1.6, 0, 0.6] }, UpperArmR: { r: [-1.6, 0, -0.6] }, LowerArmL: { r: [-0.3, 0, 0] }, LowerArmR: { r: [-0.3, 0, 0] }, UpperLegL: { r: [-0.6, 0, 0.2] }, LowerLegL: { r: [0.8, 0, 0] }, UpperLegR: { r: [-0.2, 0, -0.2] } }), 'out'],
  [0.42, merge(LYING, { Hips: { p: [0, -0.74, -0.5], r: [-1.85, 0, 0] }, UpperLegL: { r: [-0.9, 0, 0.2] }, UpperLegR: { r: [-0.7, 0, -0.2] }, UpperArmL: { r: [-0.9, 0, 0.9] }, UpperArmR: { r: [-0.9, 0, -0.9] } }), 'in'],
  [0.54, merge(LYING, { Hips: { p: [0, -0.66, -0.55], r: [-1.35, 0, 0] }, Head: { r: [0.5, 0.2, 0] }, UpperLegL: { r: [-0.5, 0, 0.2] }, UpperLegR: { r: [-0.6, 0, -0.2] } }), 'out'],
  [0.66, merge(LYING, { Hips: { p: [0, -0.8, -0.58], r: [-1.6, 0, 0] } }), 'in'],
  [0.85, LYING, 'io']]));
clip('down', 1.5, true, (t) => { const b = S((t / 1.5) * PI * 2); return merge(LYING, { Chest: { r: [b * 0.04, 0, 0] }, Head: { r: [0.2 + b * 0.05, 0.3, 0] }, LowerLegR: { r: [0.9 + b * 0.05, 0, 0] } }); });
clip('getup', 0.9, false, keyed([
  [0, LYING],
  [0.22, merge(LYING, { Hips: { p: [0, -0.72, -0.05], r: [-1.2, 0.6, 0.3] }, Spine: { r: [0.3, 0.3, 0] }, UpperArmR: { r: [-0.2, 0, -0.3] }, UpperLegR: { r: [-1.4, 0, -0.1] }, LowerLegR: { r: [2.0, 0, 0] } })],
  [0.45, merge(REST, { Hips: { p: [0, -0.5, -0.08], r: [-0.6, 0.2, 0] }, Spine: { r: [0.6, 0, 0] }, Head: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-0.9, 0, 0.3] }, UpperArmR: { r: [-0.5, 0, -0.3] }, LowerArmR: { r: [-0.3, 0, 0] }, UpperLegL: { r: [-1.7, 0, 0.2] }, LowerLegL: { r: [2.1, 0, 0] }, UpperLegR: { r: [0.1, 0, -0.1] }, LowerLegR: { r: [1.9, 0, 0] } }), 'io'],
  [0.7, G({ Hips: { p: [0, -0.16, 0] }, Spine: { r: [0.35, 0, 0] }, Head: { r: [-0.2, 0, 0] } }), 'io'],
  [0.9, GUARD, 'io']]));
clip('dead', 1, true, () => merge(LYING, { Head: { r: [0.3, 0.6, 0] } }));
// face down on the street (a ragdoll that settled on its front gets up with this one): push up, a knee under, stand
const PRONE = merge(REST, {
  Hips: { p: [0, -0.84, 0], r: [1.5708, 0, 0] }, Spine: { r: [0.05, 0, 0] }, Head: { r: [-0.45, 0.35, 0] },
  UpperArmL: { r: [-0.9, 0, 0.75] }, LowerArmL: { r: [-1.5, 0, 0] }, UpperArmR: { r: [-0.9, 0, -0.75] }, LowerArmR: { r: [-1.5, 0, 0] },
  UpperLegL: { r: [0.05, 0, 0.12] }, UpperLegR: { r: [0.0, 0, -0.15] }, LowerLegL: { r: [0.2, 0, 0] }, LowerLegR: { r: [0.35, 0, 0] }, FootL: { r: [0.5, 0, 0] }, FootR: { r: [0.5, 0, 0] },
});
clip('getup_prone', 0.9, false, keyed([
  [0, PRONE],
  [0.24, merge(PRONE, { Hips: { p: [0, -0.68, 0.04], r: [1.15, 0, 0] }, Spine: { r: [-0.35, 0, 0] }, Head: { r: [-0.5, 0, 0] }, UpperArmL: { r: [-1.25, 0, 0.25] }, LowerArmL: { r: [-0.15, 0, 0] }, UpperArmR: { r: [-1.25, 0, -0.25] }, LowerArmR: { r: [-0.15, 0, 0] } }), 'io'],
  [0.46, merge(REST, { Hips: { p: [0, -0.48, -0.06], r: [0.45, 0, 0] }, Spine: { r: [0.45, 0, 0] }, Head: { r: [-0.35, 0, 0] }, UpperArmL: { r: [-0.6, 0, 0.3] }, LowerArmL: { r: [-0.4, 0, 0] }, UpperArmR: { r: [-0.7, 0, -0.3] }, LowerArmR: { r: [-0.3, 0, 0] },
    UpperLegL: { r: [-1.6, 0, 0.15] }, LowerLegL: { r: [1.95, 0, 0] }, UpperLegR: { r: [-0.15, 0, -0.1] }, LowerLegR: { r: [1.95, 0, 0] }, FootR: { r: [0.6, 0, 0] } }), 'io'],
  [0.7, G({ Hips: { p: [0, -0.16, 0] }, Spine: { r: [0.35, 0, 0] }, Head: { r: [-0.2, 0, 0] } }), 'io'],
  [0.9, GUARD, 'io']]));

// weapons, pick-up, taunt, boss moves, block
const SWING_DOWN = G({ Hips: { p: [0, -0.2, 0.12] }, Spine: { r: [0.6, 0.2, 0] }, Chest: { r: [0.35, 0.25, 0] }, Head: { r: [-0.2, -0.1, 0] },
  UpperArmR: { r: [-1.1, 0, -0.1] }, LowerArmR: { r: [-0.1, 0, 0] }, UpperArmL: { r: [-1.0, 0, 0.15] }, LowerArmL: { r: [-0.2, 0, 0] },
  UpperLegL: { r: [-0.8, 0, 0.12] }, LowerLegL: { r: [0.9, 0, 0] }, UpperLegR: { r: [0.45, 0, -0.1] }, LowerLegR: { r: [0.3, 0, 0] } });
clip('swing', 0.62, false, keyed([
  [0, GUARD],
  [0.18, G({ Hips: { p: [0, -0.06, -0.06] }, Spine: { r: [-0.3, -0.35, 0] }, Chest: { r: [-0.25, -0.35, 0] }, Head: { r: [-0.15, 0.2, 0] }, UpperArmR: { r: [-3.0, 0, -0.15] }, LowerArmR: { r: [-0.9, 0, 0] }, UpperArmL: { r: [-2.7, 0, 0.15] }, LowerArmL: { r: [-1.1, 0, 0] } })],
  [0.27, SWING_DOWN, 'out'],
  [0.33, merge(SWING_DOWN, { Spine: { r: [0.72, 0.25, 0] }, UpperArmR: { r: [-0.8, 0, -0.1] }, UpperArmL: { r: [-0.7, 0, 0.15] } }), 'io'],
  [0.44, SWING_DOWN, 'io'],
  [0.62, GUARD, 'io']]));
const STAB_EXT = G({ Hips: { p: [0, -0.12, 0.16], r: [0, -0.3, 0] }, Spine: { r: [0.2, 0.4, 0] }, Chest: { r: [0.1, 0.35, 0] }, UpperArmR: { r: [-1.55, 0, -0.05] }, LowerArmR: { r: [-0.05, 0, 0] }, UpperLegL: { r: [-0.85, 0, 0.12] }, LowerLegL: { r: [0.85, 0, 0] }, UpperLegR: { r: [0.5, 0, -0.1] }, LowerLegR: { r: [0.2, 0, 0] } });
clip('stab', 0.5, false, keyed([
  [0, GUARD], [0.12, G({ Spine: { r: [0.08, -0.35, 0] }, Chest: { r: [0.04, -0.3, 0] }, UpperArmR: { r: [-0.4, 0.3, -0.3] }, LowerArmR: { r: [-2.3, 0, 0] }, Hips: { p: [0, -0.12, -0.06] } })],
  [0.2, STAB_EXT, 'out'], [0.34, merge(STAB_EXT, { Chest: { r: [0.15, 0.45, 0] } }), 'io'], [0.5, GUARD, 'io']]));
clip('pickup', 0.5, false, keyed([
  [0, GUARD], [0.22, G({ Hips: { p: [0, -0.42, 0.05] }, Spine: { r: [0.75, 0, 0] }, UpperArmR: { r: [-0.5, 0, -0.1] }, LowerArmR: { r: [-0.3, 0, 0] }, UpperLegL: { r: [-1.1, 0, 0.12] }, LowerLegL: { r: [1.7, 0, 0] }, UpperLegR: { r: [-0.6, 0, -0.1] }, LowerLegR: { r: [1.5, 0, 0] } }), 'io'],
  [0.5, GUARD, 'io']]));
const BECKON_A = G({ Hips: { p: [0, -0.04, 0] }, Chest: { r: [-0.2, 0, 0] }, Head: { r: [-0.25, 0, 0] }, UpperArmR: { r: [-1.4, 0.3, -0.1] }, LowerArmR: { r: [-0.5, 0, 0] }, HandR: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-0.1, 0, 0.35] }, LowerArmL: { r: [-0.3, 0, 0] } });
const BECKON_B = merge(BECKON_A, { LowerArmR: { r: [-1.9, 0, 0] }, HandR: { r: [0.6, 0, 0] } });
clip('taunt', 1.4, false, keyed([
  [0, GUARD],
  [0.25, G({ Chest: { r: [-0.3, 0, 0] }, Head: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-0.3, 0, 1.25] }, UpperArmR: { r: [-0.3, 0, -1.25] }, LowerArmL: { r: [-0.6, 0, 0] }, LowerArmR: { r: [-0.6, 0, 0] } }), 'back'],
  [0.5, G({ Chest: { r: [-0.3, 0, 0] }, Head: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-0.3, 0, 1.25] }, UpperArmR: { r: [-0.3, 0, -1.25] }, LowerArmL: { r: [-0.6, 0, 0] }, LowerArmR: { r: [-0.6, 0, 0] } })],
  [0.66, BECKON_A, 'io'], [0.8, BECKON_B, 'io'], [0.94, BECKON_A, 'io'], [1.08, BECKON_B, 'io'],
  [1.4, GUARD, 'io']]));
const SLAM_DOWN = G({ Hips: { p: [0, -0.34, 0.2] }, Spine: { r: [0.95, 0, 0] }, Chest: { r: [0.4, 0, 0] }, Head: { r: [-0.4, 0, 0] }, UpperArmL: { r: [-0.5, 0, 0.3] }, UpperArmR: { r: [-0.5, 0, -0.3] }, LowerArmL: { r: [-0.2, 0, 0] }, LowerArmR: { r: [-0.2, 0, 0] },
  UpperLegL: { r: [-1.0, 0, 0.2] }, LowerLegL: { r: [1.2, 0, 0] }, UpperLegR: { r: [0.1, 0, -0.2] }, LowerLegR: { r: [0.9, 0, 0] } });
clip('slam', 1.1, false, keyed([
  [0, GUARD],
  [0.5, G({ Hips: { p: [0, -0.02, -0.12] }, Spine: { r: [-0.55, 0, 0] }, Chest: { r: [-0.35, 0, 0] }, Head: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-3.05, 0, 0.3] }, UpperArmR: { r: [-3.05, 0, -0.3] }, LowerArmL: { r: [-0.4, 0, 0] }, LowerArmR: { r: [-0.4, 0, 0] }, FootL: { r: [0.4, 0, 0] }, FootR: { r: [0.4, 0, 0] } }), 'io'],
  [0.64, SLAM_DOWN, 'in'],
  [0.72, merge(SLAM_DOWN, { Hips: { p: [0, -0.38, 0.2] }, Spine: { r: [1.05, 0, 0] } }), 'out'],
  [0.92, SLAM_DOWN, 'io'],
  [1.1, GUARD, 'io']]));
// spin: the whole body turns once, arms out
const SPIN_ARMS = { UpperArmL: { r: [-0.15, 0, 1.45] }, UpperArmR: { r: [-0.15, 0, -1.45] }, LowerArmL: { r: [-0.15, 0, 0] }, LowerArmR: { r: [-0.15, 0, 0] }, Spine: { r: [0.05, 0, 0] } };
clip('spin', 0.7, false, (t) => {
  if (t < 0.12) return lerpPose(GUARD, G({ ...SPIN_ARMS, Hips: { p: [0, -0.12, 0], r: [0, -0.4, 0] } }), EASE.io(t / 0.12));
  if (t < 0.52) { const u = (t - 0.12) / 0.4; return G({ ...SPIN_ARMS, Hips: { p: [0, -0.12 + 0.05 * S(u * PI), 0], r: [0, -0.4 + EASE.out(u) * (PI * 2 + 0.4), 0] }, Head: { r: [0, -0.3, 0] } }); }
  return lerpPose(G({ ...SPIN_ARMS, Hips: { p: [0, -0.12, 0], r: [0, PI * 2, 0] } }), merge(GUARD, { Hips: { p: [0, -0.07, 0], r: [0, PI * 2 + 0.18, 0] } }), EASE.io((t - 0.52) / 0.18));
});
clip('block', 0.5, true, (t) => { const b = S((t / 0.5) * PI * 2); return G({ Hips: { p: [0, -0.14, 0] }, Spine: { r: [0.2, 0, 0] }, UpperArmL: { r: [-1.15 + b * 0.04, 0.35, 0.05] }, LowerArmL: { r: [-2.35, 0, 0] }, UpperArmR: { r: [-1.15 - b * 0.04, -0.35, -0.05] }, LowerArmR: { r: [-2.35, 0, 0] }, Head: { r: [0.25, 0, 0] } }); });

// a 1950s tough: fists up high by the chin, a bounce on the toes, the comb run back through the pompadour (a taunt), a
// lowered-shoulder charge, a jacket flick (both hands snap the leather open, then a backhand)
const BOXER = G({ Hips: { p: [0, -0.09, 0], r: [0, 0.3, 0] }, Spine: { r: [0.16, -0.12, 0] }, Chest: { r: [0.08, -0.16, 0] }, Head: { r: [0.1, 0.12, 0] },
  UpperArmL: { r: [-0.95, -0.3, 0.22] }, LowerArmL: { r: [-2.35, 0, 0] }, UpperArmR: { r: [-1.0, 0.25, -0.18] }, LowerArmR: { r: [-2.4, 0, 0] } });
clip('idle_boxer', 0.9, true, (t) => {
  const w = (t / 0.9) * PI * 2, b = Math.abs(S(w)), s2 = S(w);
  return merge(BOXER, { Hips: { p: [s2 * 0.012, -0.09 + b * 0.025, 0], r: [0, 0.3, 0] }, Chest: { r: [0.08 + b * 0.03, -0.16 + s2 * 0.05, 0] },
    UpperArmL: { r: [-0.95 - b * 0.06, -0.3, 0.22] }, UpperArmR: { r: [-1.0 + s2 * 0.08, 0.25, -0.18] }, Head: { r: [0.1, 0.12 + s2 * 0.04, 0] },
    LowerLegL: { r: [0.5 - b * 0.2, 0, 0] }, LowerLegR: { r: [0.45 - b * 0.2, 0, 0] }, FootL: { r: [-0.1 + b * 0.2, 0.1, 0] }, FootR: { r: [-0.25 + b * 0.2, 0.2, 0] } });
});
const COMB_HIP = { UpperArmL: { r: [0.15, 0, 0.55] }, LowerArmL: { r: [-1.5, 0, 0] }, HandL: { r: [0, 0, -0.3] } };
const COMB_A = merge(REST, COMB_HIP, { Hips: { p: [0, -0.02, 0], r: [0, -0.15, 0.04] }, Spine: { r: [-0.04, 0, 0] }, Head: { r: [-0.12, -0.25, 0.12] },
  UpperArmR: { r: [-2.4, 0.3, 0.75] }, LowerArmR: { r: [-1.6, 0, 0] }, HandR: { r: [-0.4, 0, 0.3] } });
const COMB_B = merge(COMB_A, { Head: { r: [0.08, -0.25, 0.12] }, UpperArmR: { r: [-2.85, 0.2, 0.55] }, LowerArmR: { r: [-1.15, 0, 0] }, HandR: { r: [0.2, 0, 0.3] } });
clip('comb', 1.6, false, keyed([
  [0, BOXER], [0.25, COMB_A, 'io'], [0.55, COMB_B, 'io'], [0.7, COMB_A, 'io'], [1.0, COMB_B, 'io'],
  [1.2, merge(COMB_B, { UpperArmR: { r: [-2.2, 0.2, -0.9] }, LowerArmR: { r: [-0.9, 0, 0] }, Head: { r: [-0.1, -0.1, 0] } }), 'io'], [1.6, BOXER, 'io']]));
clip('shoulder', 0.5, true, (t) => {
  const p = walkPose(t, 0.5, true);
  return merge(p, { Hips: { p: [0, -0.12, 0], r: [0.1, -0.7, 0] }, Spine: { r: [0.45, -0.35, 0.1] }, Chest: { r: [0.15, -0.3, 0.15] }, Head: { r: [-0.35, 0.6, 0] },
    UpperArmL: { r: [-0.4, 0, 0.15] }, LowerArmL: { r: [-1.9, 0, 0] }, UpperArmR: { r: [-0.6, 0.2, -0.3] }, LowerArmR: { r: [-2.2, 0, 0] } });
});
const LAPELS = G({ Spine: { r: [0.1, 0, 0] }, Chest: { r: [0.15, 0, 0] }, Head: { r: [0.15, 0, 0] }, UpperArmL: { r: [-0.6, 0.6, -0.35] }, LowerArmL: { r: [-2.3, 0, 0] }, UpperArmR: { r: [-0.6, -0.6, 0.35] }, LowerArmR: { r: [-2.3, 0, 0] } });
const FLICK_OUT = G({ Hips: { p: [0, -0.06, 0.06] }, Spine: { r: [-0.15, 0, 0] }, Chest: { r: [-0.25, 0, 0] }, Head: { r: [-0.25, 0, 0] }, UpperArmL: { r: [-0.2, 0.1, 0.95] }, LowerArmL: { r: [-0.5, 0, 0] }, UpperArmR: { r: [-0.2, -0.1, -0.95] }, LowerArmR: { r: [-0.5, 0, 0] } });
const BACKHAND = G({ Hips: { p: [0, -0.1, 0.14], r: [0, -0.5, 0] }, Spine: { r: [0.12, 0.45, 0] }, Chest: { r: [0.08, 0.6, 0] }, Head: { r: [0, -0.4, 0] },
  UpperArmR: { r: [-1.45, -0.5, -0.4] }, LowerArmR: { r: [-0.25, 0, 0] }, UpperArmL: { r: [-0.7, -0.15, 0.25] }, LowerArmL: { r: [-2.3, 0, 0] } });
clip('flick', 0.6, false, keyed([
  [0, GUARD], [0.1, LAPELS, 'io'], [0.2, FLICK_OUT, 'out'], [0.3, merge(FLICK_OUT, { Chest: { r: [-0.3, -0.3, 0] }, UpperArmR: { r: [-0.9, 0.6, -0.5] }, LowerArmR: { r: [-1.6, 0, 0] } }), 'io'],
  [0.38, BACKHAND, 'out'], [0.46, BACKHAND, 'io'], [0.6, GUARD, 'io']]));

// the thumbs-up taunt: weight dropped onto one hip, a lean back, chin up and cocked, the right arm out front with the
// thumb up (a fist, the thumb raised), the left hand on the hip; the thumb pumps twice on the beat
const THUMB = merge(REST, {
  Hips: { p: [0.035, -0.025, 0], r: [0, -0.3, 0.08] }, Spine: { r: [-0.07, 0.12, -0.1] }, Chest: { r: [-0.1, 0.16, -0.05] },
  Neck: { r: [-0.04, 0.05, 0] }, Head: { r: [-0.14, 0.22, 0.16] },
  UpperArmR: { r: [-1.25, -0.2, -0.12] }, LowerArmR: { r: [-0.6, 0, 0] }, HandR: { r: [0.1, 0, 0] },
  UpperArmL: { r: [0.15, 0, 0.55] }, LowerArmL: { r: [-1.5, 0, 0] }, HandL: { r: [0, 0, -0.3] },
  UpperLegL: { r: [-0.04, 0.08, 0.1] }, LowerLegL: { r: [0.04, 0, 0] },
  UpperLegR: { r: [-0.16, -0.28, -0.12] }, LowerLegR: { r: [0.36, 0, 0] }, FootR: { r: [0.12, -0.25, 0] },
});
const THUMB_PUMP = merge(THUMB, { UpperArmR: { r: [-1.38, -0.2, -0.12] }, LowerArmR: { r: [-0.5, 0, 0] }, Head: { r: [-0.06, 0.22, 0.12] }, Hips: { p: [0.035, -0.04, 0], r: [0, -0.3, 0.08] } });
clip('thumbsup', 1.5, false, keyed([
  [0, BOXER], [0.24, THUMB, 'back'], [0.42, THUMB_PUMP, 'out'], [0.56, THUMB, 'io'], [0.7, THUMB_PUMP, 'out'], [0.84, THUMB, 'io'],
  [1.15, THUMB, 'io'], [1.5, BOXER, 'io']]));
// the jukebox thump: the right fist goes up behind his head while he looks at the fighters (the wind-up is the tell),
// then a hammer fist down onto the jukebox in front of him at 0.6 s, a bounce off it, back to the guard
const THUMP_UP = G({ Hips: { p: [0, -0.04, -0.04], r: [0, 0.3, 0] }, Spine: { r: [-0.12, 0.18, 0.06] }, Chest: { r: [-0.16, 0.3, 0.1] }, Head: { r: [-0.05, 0.55, 0] },
  UpperArmR: { r: [-2.75, 0.1, -0.35] }, LowerArmR: { r: [-1.35, 0, 0] }, HandR: { r: [0.3, 0, 0] }, UpperArmL: { r: [-0.5, -0.1, 0.35] }, LowerArmL: { r: [-1.9, 0, 0] } });
const THUMP_HIT = G({ Hips: { p: [0, -0.13, 0.06], r: [0, -0.1, 0] }, Spine: { r: [0.3, -0.05, 0.05] }, Chest: { r: [0.22, -0.1, 0.08] }, Head: { r: [0.15, -0.05, 0] },
  UpperArmR: { r: [-1.1, 0.15, -0.3] }, LowerArmR: { r: [-0.35, 0, 0] }, HandR: { r: [0.35, 0, 0] }, UpperArmL: { r: [-0.4, -0.1, 0.4] }, LowerArmL: { r: [-1.6, 0, 0] },
  UpperLegL: { r: [-0.6, -0.1, 0.12] }, LowerLegL: { r: [0.75, 0, 0] } });
clip('thump', 1.0, false, keyed([
  [0, BOXER], [0.3, THUMP_UP, 'io'], [0.5, merge(THUMP_UP, { UpperArmR: { r: [-2.85, 0.1, -0.35] }, Head: { r: [-0.05, 0.6, 0] } }), 'io'],
  [0.6, THUMP_HIT, 'in'], [0.68, merge(THUMP_HIT, { UpperArmR: { r: [-1.25, 0.15, -0.3] }, Spine: { r: [0.26, -0.05, 0.05] } }), 'out'], [0.76, THUMP_HIT, 'io'], [1.0, BOXER, 'io']]));

// ── pose to rotation tracks ─────────────────────────────────────────────
// the redneck's haymaker: a big looping right, wound up from the hip with the whole body turned away (the tell), thrown
// round in an arc, then the follow-through carries him off balance and he has to catch himself (an opening)
const HAY_BACK = G({ Hips: { p: [0, -0.1, -0.06], r: [0, 0.45, 0] }, Spine: { r: [0.05, -0.4, 0] }, Chest: { r: [-0.08, -0.6, 0] }, Head: { r: [-0.05, 0.4, 0] },
  UpperArmR: { r: [0.15, 0.35, -1.25] }, LowerArmR: { r: [-1.35, 0, 0] }, HandR: { r: [0.2, 0, 0] }, UpperArmL: { r: [-0.95, -0.2, 0.35] }, LowerArmL: { r: [-1.5, 0, 0] },
  UpperLegL: { r: [-0.35, -0.1, 0.12] }, LowerLegL: { r: [0.3, 0, 0] }, UpperLegR: { r: [0.15, -0.25, -0.15] }, LowerLegR: { r: [0.65, 0, 0] } });
const HAY_HIT = G({ Hips: { p: [0, -0.12, 0.14], r: [0, -0.45, 0] }, Spine: { r: [0.25, 0.5, 0] }, Chest: { r: [0.15, 0.78, 0] }, Head: { r: [0.05, -0.45, 0] },
  UpperArmR: { r: [-1.5, -0.3, -0.6] }, LowerArmR: { r: [-0.55, 0, 0] }, UpperArmL: { r: [-0.3, 0, 0.65] }, LowerArmL: { r: [-1.0, 0, 0] },
  UpperLegL: { r: [-0.75, -0.1, 0.12] }, LowerLegL: { r: [0.7, 0, 0] }, UpperLegR: { r: [0.45, -0.25, -0.12] }, LowerLegR: { r: [0.25, 0, 0] }, FootR: { r: [0.4, 0.3, 0] } });
const HAY_OVER = merge(HAY_HIT, { Hips: { p: [0, -0.16, 0.2], r: [0, -0.6, 0.06] }, Spine: { r: [0.45, 0.62, 0] }, Chest: { r: [0.35, 0.95, 0.05] }, Head: { r: [0.25, -0.55, 0.12] },
  UpperArmR: { r: [-1.15, -0.65, 0.1] }, LowerArmR: { r: [-0.7, 0, 0] }, UpperArmL: { r: [-0.2, 0, 0.9] }, LowerArmL: { r: [-0.6, 0, 0] } });
clip('haymaker', 1.15, false, keyed([
  [0, GUARD],
  [0.43, HAY_BACK, 'io'],
  [0.5, HAY_HIT, 'out'],
  [0.6, HAY_OVER, 'io'],
  [0.85, merge(HAY_OVER, { UpperArmL: { r: [-0.5, 0, 1.1] }, Head: { r: [0.15, -0.4, 0.05] } }), 'io'],
  [1.15, GUARD, 'io']]), ['redneck']);
// the creep: a hunched lurching walk (one leg dragged), and the flash: he spreads his coat wide with both hands and lunges
clip('lurch', 1.3, true, (t) => {
  const p = walkPose(t, 1.3, false), w = (t / 1.3) * PI * 2;
  return merge(p, { Spine: { r: [0.32, -S(w) * 0.12, S(w) * 0.06] }, Chest: { r: [0.18, -S(w) * 0.1, 0] }, Neck: { r: [-0.15, 0, 0] }, Head: { r: [-0.25, S(w * 0.5) * 0.25, S(w) * 0.1] },
    Hips: { p: [S(w) * 0.03, -0.04 - Math.abs(S(w)) * 0.03, 0], r: [0, S(w) * 0.08, S(w) * 0.08] }, UpperLegR: { r: [p.UpperLegR.r[0] * 0.45 + 0.1, 0, -0.08] }, LowerLegR: { r: [0.15, 0, 0] }, FootR: { r: [0.25, 0, 0] },
    UpperArmL: { r: [-0.35, 0, 0.12] }, LowerArmL: { r: [-1.1, 0, 0] }, UpperArmR: { r: [-0.3 - S(w) * 0.2, 0, -0.1] }, LowerArmR: { r: [-0.9, 0, 0] } });
}, ['creep']);
const FLASH_OPEN = merge(REST, { Hips: { p: [0, -0.06, 0.12] }, Spine: { r: [-0.18, 0, 0] }, Chest: { r: [-0.22, 0, 0] }, Head: { r: [-0.2, 0, 0] },
  UpperArmL: { r: [-0.5, 0, 1.35] }, LowerArmL: { r: [-0.5, 0, 0] }, UpperArmR: { r: [-0.5, 0, -1.35] }, LowerArmR: { r: [-0.5, 0, 0] },
  UpperLegL: { r: [-0.6, 0, 0.1] }, LowerLegL: { r: [0.6, 0, 0] }, UpperLegR: { r: [0.35, 0, -0.1] }, LowerLegR: { r: [0.2, 0, 0] } });
clip('flash', 1.1, false, keyed([
  [0, merge(REST, { Spine: { r: [0.3, 0, 0] }, UpperArmL: { r: [-0.9, 0.3, 0.1] }, LowerArmL: { r: [-1.9, 0, 0] }, UpperArmR: { r: [-0.9, -0.3, -0.1] }, LowerArmR: { r: [-1.9, 0, 0] } })],
  [0.42, merge(REST, { Hips: { p: [0, -0.1, -0.06] }, Spine: { r: [0.45, 0, 0] }, Head: { r: [0.1, 0, 0] }, UpperArmL: { r: [-0.6, 0.4, 0.05] }, LowerArmL: { r: [-2.2, 0, 0] }, UpperArmR: { r: [-0.6, -0.4, -0.05] }, LowerArmR: { r: [-2.2, 0, 0] }, UpperLegL: { r: [-0.3, 0, 0.1] }, LowerLegL: { r: [0.6, 0, 0] }, UpperLegR: { r: [-0.1, 0, -0.1] }, LowerLegR: { r: [0.5, 0, 0] } }), 'io'],
  [0.52, FLASH_OPEN, 'out'],
  [0.8, merge(FLASH_OPEN, { Spine: { r: [-0.25, 0, 0] }, Head: { r: [-0.3, 0.2, 0] } }), 'io'],
  [1.1, REST, 'io']]), ['creep']);
function quat(r) { // rest-relative euler, q = qz * qy * qx
  const [x, y, z] = r;
  const qx = [Math.sin(x / 2), 0, 0, Math.cos(x / 2)], qy = [0, Math.sin(y / 2), 0, Math.cos(y / 2)], qz = [0, 0, Math.sin(z / 2), Math.cos(z / 2)];
  const m = (a, b) => [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0], a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
  return m(qz, m(qy, qx));
}

// ── the body ────────────────────────────────────────────────────────────
const hexRaw = (h) => [((h >> 16) & 255) / 255, ((h >> 8) & 255) / 255, (h & 255) / 255];
const hex = (h) => hexRaw(h).map((c) => Math.pow(c, 2.2));
const TEX = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'textures');
const ss = (a, b, x) => { const t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const gauss = (x, w) => Math.exp(-(x * x) / (w * w));
const wrapA = (a) => { a = ((a + PI) % (PI * 2) + PI * 2) % (PI * 2) - PI; return a; }; // -PI..PI
const FRONT = PI / 2, BACK = -PI / 2; // loft angles: +Z is the front
/** a soft fold pattern: narrow ridges at the given heights, stronger on one side (angle) */
const folds = (y, a, list) => { let m = 0; for (const [fy, amp, w, side, sw] of list) m += amp * gauss(y - fy, w) * (side == null ? 1 : 0.35 + 0.65 * gauss(wrapA(a - side), sw ?? 1.2)); return m; };
const noise1 = (x, s) => { const i = Math.floor(x), f = x - i, h = (n) => { let v = Math.imul(n ^ s, 0x27d4eb2d); v ^= v >>> 15; v = Math.imul(v, 0x85ebca6b); v ^= v >>> 13; return (v >>> 0) / 4294967296; }; const u = f * f * (3 - 2 * f); return lerp(h(i), h(i + 1), u); };

function build(sp) {
  const m = new SkinMesh();
  const { world } = skeleton(sp);
  const B = (n) => world[n];
  const X = [1, 0, 0], Y = [0, 1, 0], Z = [0, 0, 1];
  const W = sp.width ?? 1, AR = sp.arm ?? 1, LG = sp.leg ?? 1, HS = sp.head ?? 1.15, HN = sp.hand ?? 1.35, belly = sp.belly ?? 0, chest = sp.chest ?? 1;
  const sw = sp.sw ?? 0.2, hw = sp.hw ?? 0.1;
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
  const armW = (P, side) => {
    const y = P[1], sd = side;
    const a = ss(ySh + 0.07, ySh - 0.05, y), b = ss(yEl + 0.04, yEl - 0.04, y), c = ss(yWr + 0.03, yWr - 0.02, y);
    return [[BI.Chest, 1 - a], [BI['UpperArm' + sd], a * (1 - b)], [BI['LowerArm' + sd], a * b * (1 - c)], [BI['Hand' + sd], a * b * c]];
  };
  const legW = (P, side) => {
    const y = P[1], sd = side;
    const a = ss(yHip + 0.07, yHip - 0.08, y), b = ss(yKn + 0.05, yKn - 0.05, y), c = ss(yAn + 0.06, yAn + 0.0, y);
    return [[BI.Hips, 1 - a], [BI['UpperLeg' + sd], a * (1 - b)], [BI['LowerLeg' + sd], a * b * (1 - c)], [BI['Foot' + sd], a * b * c]];
  };
  const headW = (P) => { const t = ss(yHd - 0.02, yHd + 0.05, P[1]); return [[BI.Neck, 1 - t], [BI.Head, t]]; };
  const tailW = (P, side) => { // coat skirts: hips at the waist, the spring bones toward the hem, a share of the thigh
    const y = P[1], sd = side, t = ss(yH - 0.02, yKn + 0.05, y), back = lerp(0.3, 1, ss(0.08, -0.08, P[2])); // front panels follow the thigh, the back the spring bone, blended round the side (a hard switch tore the side seam on kicks)
    return [[BI.Hips, 1 - t], [BI['Coat' + sd], t * 0.75 * back], [BI['UpperLeg' + sd], t * (1 - 0.75 * back)]];
  };

  // ── the torso: rings from the crotch to the neck; pecs, glutes, belly, shoulder mass by angle ──
  const bw = 0.78 + 0.22 * W, bd = 0.85 + 0.15 * W, sws = sw / 0.2;
  // [y, half width, half depth, z offset, superellipse]
  const PROF = [[yHip - 0.08, 0.04, 0.045, 0, 2], [yHip - 0.05, 0.13, 0.095, 0, 2.2], [yHip, 0.172, 0.115, -0.005, 2.4], [yHip + 0.06, 0.18, 0.122, -0.008, 2.5],
    [yH + 0.07, 0.168, 0.112, -0.004, 2.5], [yS + 0.03, 0.152, 0.104, 0, 2.5], [yS + 0.11, 0.165, 0.112, 0.004, 2.6], [yC + 0.02, 0.19, 0.124, 0.008, 2.7],
    [yC + 0.09, 0.2, 0.13, 0.008, 2.8], [ySh - 0.05, 0.205, 0.124, 0.002, 3.0], [ySh, 0.185, 0.11, -0.004, 3.2], [ySh + 0.04, 0.145, 0.094, -0.01, 3.0],
    [yN + 0.0, 0.085, 0.068, -0.012, 2.4], [yN + 0.035, 0.064, 0.062, -0.01, 2.2]];
  const prof = (y) => { // interpolate the profile at y
    let i = 0; while (i < PROF.length - 2 && PROF[i + 1][0] < y) i++;
    const A = PROF[i], Bq = PROF[i + 1], t = EASE.io(Math.max(0, Math.min(1, (y - A[0]) / (Bq[0] - A[0]))));
    return A.map((v, k) => lerp(v, Bq[k], t));
  };
  const torsoMod = (y, extra = 0, foldList = null) => (a) => {
    let k = 1 + extra;
    const s = Math.sin(a), c = Math.cos(a);
    const front = Math.max(0, s), back = Math.max(0, -s);
    k += 0.07 * chest * gauss(y - (yC + 0.08), 0.06) * (gauss(wrapA(a - (FRONT - 0.5)), 0.45) + gauss(wrapA(a - (FRONT + 0.5)), 0.45)); // pecs
    k += 0.08 * gauss(y - (yHip + 0.03), 0.06) * (gauss(wrapA(a - (BACK - 0.5)), 0.5) + gauss(wrapA(a - (BACK + 0.5)), 0.5)); // seat
    k += belly * 0.3 * gauss(y - (yS - 0.01), 0.1) * Math.pow(front, 2.5); // belly
    k += 0.05 * gauss(y - (yC + 0.03), 0.07) * Math.pow(Math.abs(c), 3); // lats
    k -= 0.025 * gauss(y - yC, 0.15) * gauss(wrapA(a - BACK), 0.18); // the spine groove
    if (foldList) k += folds(y, a, foldList);
    return k;
  };
  // below the ribs the width follows the build; from the chest up it follows the shoulders, so arms never sink into it
  const ringAt = (y, k = 1, kz = 1, add = 0, mod = null) => { const p = prof(y); const u = ss(yS + 0.05, yC + 0.06, y); const wk = lerp(bw, sws * (0.92 + 0.08 * chest), u); return { c: [0, y, p[3]], U: X, V: Z, ru: p[1] * wk * k + add, rv: p[2] * bd * (1 + (chest - 1) * 0.6 * u) * kz + add, e: p[4], mod }; };
  const ys = (y0, y1, step = 0.025) => { const n = Math.max(1, Math.round((y1 - y0) / step)), r = []; for (let i = 0; i <= n; i++) r.push(y0 + ((y1 - y0) * i) / n); return r; };

  const top = sp.top ?? 'tee', bottom = sp.bottom ?? 'jeans';
  const waist = yH + 0.06; // where trousers end and the top begins
  const shirtMat = sp.shirtMat ?? 'shirt', shirtTile = sp.shirtTile ?? 0.5; // a 1 m tile: a print or stains laid out once round the torso
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
    m.loft(shirtMat, rings, { n: 32, weights: (k, P, a) => torsoW(P, a), tile: shirtTile });
    if (top === 'tank') { // straps over the shoulders and skin above
      m.loft('skin', ys(shirtTop - 0.02, yN + 0.035).map((y) => ringAt(y, 1, 1, 0, torsoMod(y))), { n: 32, weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
      for (const ac of [0.82, -0.82, PI - 0.82, -PI + 0.82]) // straps: front and back, left and right
        m.loft(shirtMat, ys(shirtTop - 0.03, ySh + 0.05, 0.012).map((y) => ringAt(y, 1, 1, 0.007, torsoMod(y))), { n: 3, arc: [ac - 0.11, ac + 0.11], weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    } else { // a collar
      m.loft(shirtMat, [ringAt(yN - 0.005, 1.0, 1.0, 0.012), ringAt(yN + 0.025, 1.0, 1.0, 0.016), ringAt(yN + 0.04, 0.96, 0.96, 0.012)], { n: 24, weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    }
  }
  if (sp.belt) {
    m.loft('belt', [ringAt(waist - 0.035, 1.07, 1.08, 0.016), ringAt(waist - 0.03, 1.09, 1.1, 0.02), ringAt(waist + 0.005, 1.09, 1.1, 0.02), ringAt(waist + 0.01, 1.07, 1.08, 0.016)], { n: 32, weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    const p = prof(waist - 0.012);
    m.ellipsoid('buckle', [0, waist - 0.012, p[2] * bd * 1.1 + 0.022 + p[3]], 0.032, 0.024, 0.008, { bone: BI.Hips, steps: 4, n: 10, e: 3 });
  }
  // open garments over the top: jackets, vests, coats (double sided, the front left open), armour plates
  const outer = sp.outer;
  if (outer) {
    const open = sp.openFront ?? 0.38, kind = sp.outerKind ?? 'jacket';
    const y0 = kind === 'vest' || kind === 'jacket' || kind === 'hivis' ? waist - 0.07 : waist - 0.02;
    const y1 = ySh + 0.04;
    const of = [[y0 + 0.08, 0.02, 0.014, FRONT, 0.8], [yC - 0.05, 0.016, 0.02, FRONT - 0.9, 0.5], [yC - 0.02, 0.016, 0.02, FRONT + 0.9, 0.5], [y0 + 0.2, 0.012, 0.015, BACK, 1]];
    const rings = ys(y0, y1).map((y) => ringAt(y, 1.1, 1.13, 0.024 + 0.01 * ss(yC, y0, y), torsoMod(y, 0, of)));
    rings.push({ ...ringAt(yN + 0.01, 1.02, 1.02, 0.02), c: [0, yN + 0.01, prof(yN)[3] - 0.01] });
    rings.push({ ...ringAt(yN + 0.055, 1.25, 1.25, 0.03), c: [0, yN + 0.055, prof(yN)[3] - 0.015] }); // the collar stands and rolls out
    m.loft('outer', rings, { n: 36, arc: [FRONT + open, FRONT - open + PI * 2], weights: (k, P, a) => torsoW(P, a), tile: 0.5 });
    if (kind === 'hivis') for (const y of [yS + 0.06, yC + 0.06]) m.loft('reflect', [ringAt(y - 0.018, 1.1, 1.13, 0.028), ringAt(y + 0.018, 1.1, 1.13, 0.028)].map((r) => ({ ...r, mod: torsoMod(r.c[1]) })), { n: 36, arc: [FRONT + open, FRONT - open + PI * 2], weights: (k, P, a) => torsoW(P, a), tile: 0.3 });
    if (kind === 'longcoat' || kind === 'tailcoat') { // skirts below the waist (tails are split at the back)
      const yb = kind === 'tailcoat' ? yKn + 0.02 : yKn - 0.1;
      const rr = ys(yb, waist - 0.01, 0.03).reverse().map((y) => { const t = ss(waist, yb, y); const r = ringAt(Math.max(y, PROF[1][0]), 1.12 + 0.18 * t, 1.18 + 0.2 * t, 0.03 + 0.035 * t, torsoMod(Math.max(y, PROF[1][0]), 0, [[y, 0, 1, null]])); r.c = [0, y, r.c[2] - 0.01 * t]; r.ru = Math.max(r.ru, prof(yHip)[1] * bw * 1.12 + 0.03 + 0.06 * t); r.rv = Math.max(r.rv, prof(yHip)[2] * bd * 1.15 + 0.03 + 0.05 * t); r.mod = (a) => 1 + 0.03 * Math.sin(a * 7 + y * 9) * t; return r; });
      if (kind === 'tailcoat') { m.loft('outer', rr, { n: 12, arc: [BACK + 0.06, BACK + 1.6], weights: (k, P) => tailW(P, 'L'), tile: 0.5 }); m.loft('outer', rr, { n: 12, arc: [BACK - 1.6, BACK - 0.06], weights: (k, P) => tailW(P, 'R'), tile: 0.5 }); }
      else { m.loft('outer', rr, { n: 18, arc: [FRONT + open + 0.15, FRONT + PI - 0.03], weights: (k, P) => tailW(P, 'R'), tile: 0.5 }); m.loft('outer', rr, { n: 18, arc: [FRONT + PI + 0.03, FRONT + 2 * PI - open - 0.15], weights: (k, P) => tailW(P, 'L'), tile: 0.5 }); }
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
  const nr = 0.058 * (0.85 + 0.15 * W) * (sp.neck ?? 1);
  m.loft('skin', ys(yN - 0.02, yHd + 0.06, 0.02).map((y) => ({ c: [0, y, -0.008 + 0.012 * ss(yN, yHd + 0.06, y)], U: X, V: Z, ru: nr * (1 + 0.25 * ss(yN + 0.03, yN - 0.02, y)), rv: nr * 0.95, e: 2.2, mod: (a) => 1 + 0.06 * gauss(wrapA(a - FRONT), 0.4) * gauss(y - (yN + 0.06), 0.03) })), { n: 18, weights: (k, P) => (P[1] < yN + 0.02 ? torsoW(P, 0) : headW(P)), tile: 0.3 });

  // ── head: a sculpted face (sockets, eyes and lids, nose, lips and a mouth that opens, teeth, ears) on the head envelope ──
  const M = headOf(sp, world), { hc, R, RZ, HY } = M.H, headRing = M.H.ring, F = M.F;
  buildHead(m, M, BI, { glowEyes: !!sp.glowEyes, piercings: sp.piercings, lipRing: sp.lipRing });
  if (sp.shades) { // pushed up on the forehead
    const P = M.surf(0, 0.4);
    m.ellipsoid('shades', add(P, [0, 0.004, 0.008 * HS]), 0.1 * HS, 0.02 * HS, 0.016 * HS, { bone: BI.Head, steps: 6, n: 16, e: 3, axis: norm([0, 1, -0.35]), U: X, V: norm([0, 0.35, 1]) });
  }
  if (sp.moustache) { // along the upper lip, riding on the lip and corner bones; a handlebar curls up at the ends
    for (const sd of [1, -1]) {
      const pts = [], rad = [];
      for (let i = 0; i <= 7; i++) {
        const u = i / 7, xn = sd * lerp(0.01, F.mw * 1.15, u), yn = F.tm + 0.085 - 0.03 * u * u + (sp.moustache === 'handlebar' ? 0.16 * ss(0.7, 1, u) : -0.06 * ss(0.75, 1, u));
        const P = M.surf(Math.max(-0.95, Math.min(0.95, xn)), yn);
        pts.push(add(P, [0, 0, (0.035 - 0.012 * u) * R])); rad.push((0.06 - 0.03 * u) * R * (sp.moustache === 'thick' ? 1.4 : sp.moustache === 'pencil' ? 0.38 : 1));
      }
      m.loft('hair', pts.map((p, i) => { const t = norm(sub(pts[Math.min(7, i + 1)], pts[Math.max(0, i - 1)])); const U = norm([t[1], -t[0], 0]); return { c: p, U, V: Z, ru: rad[i], rv: rad[i] * 0.55, mod: (a) => 1 + 0.12 * Math.sin(a * 5 + i) }; }),
        { n: 8, weights: (k) => { const u = k / 7; return [[BI.LipU, 0.6 * (1 - u)], [BI['Mouth' + (sd > 0 ? 'L' : 'R')], 0.6 * u], [BI.Head, 0.4]]; }, tile: 0.1 });
    }
  }
  if (sp.goatee) { const P = M.surf(0, -0.8); m.cone('hair', add(P, [0, 0.01, -0.012]), add(P, [0, -0.07 * HS, 0.01 * HS]), 0.034 * HS, { bone: BI.Jaw, n: 10, ratio: 0.6, bend: [0, 0, 0.01] }); }
  if (sp.beard) shellGrid(m, 'hair', M, -1, F.tm - 0.04, 1.7, 0.008, { jaw: BI.Jaw, head: BI.Head, rows: 10, cols: 30, rough: 0.12 });
  if (sp.mask) shellGrid(m, 'mask', M, -1, -0.04, 1.85, 0.006, { jaw: BI.Jaw, head: BI.Head, rows: 18, cols: 40, drape: true, folds: 0.02 });

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
  // the front of the crown shell sinks under the skin below the hairline, higher at the temples
  const hairline = (t, a) => { const f = Math.pow(Math.max(0, Math.sin(a)), 1.4), ax = Math.abs(Math.cos(a)); return 1 - 0.13 * f * ss(0.5 + 0.06 * gauss(ax - 0.42, 0.18), 0.36, t); };
  const scalp = (k = 1.06, add = 0.006, seed = 1) => {
    m.loft('hair', [...shell(0.28, 1.0, k, add, 10, { mod: (t, i) => (a) => tuft(seed, 0.1)(t, i)(a) * hairline(t, a) })], { n: 32, bone: BI.Head, tile: 0.25 }); // crown down to the hairline (it sinks under the forehead skin)
    m.loft('hair', shell(-0.45, 0.32, k, add, 6, { mod: tuft(seed + 3, 0.08) }), { n: 16, arc: [BACK - 1.3, BACK + 1.3], bone: BI.Head, tile: 0.25 }); // the back, behind the ears
    m.loft('hair', shell(0.14, 0.32, k, add, 3, { mod: tuft(seed + 5, 0.08) }), { n: 22, arc: [BACK - 1.95, BACK + 1.95], bone: BI.Head, tile: 0.25 }); // over the ears
  };
  // swept clumps over the scalp break up the outline (a smooth shell reads as a helmet)
  const clumps = (rows, len, rad, sweep, seed) => {
    for (let r = 0; r < rows; r++) for (let i = 0, nI = 5 + r * 2; i < nI; i++) {
      const ph = 0.12 + (r / rows) * 1.35, th = -PI * 0.9 + 1.8 * PI * ((i + 0.5) / nI) + (noise1(i * 3 + r * 17, seed) - 0.5) * 0.3;
      const nrm = norm([Math.sin(ph) * Math.sin(th), Math.cos(ph), Math.sin(ph) * Math.cos(th)]);
      if (nrm[2] > 0.42 && nrm[1] < 0.84) continue; // keep the forehead clear
      const base = add(hc, [nrm[0] * R * 1.02, nrm[1] * HY * 1.02, nrm[2] * RZ * 1.02]);
      const tdir = norm(add(mul(nrm, 0.2), [nrm[0] * 0.3, -0.25 - 0.5 * sweep, -0.85 * sweep - 0.2]));
      const side = norm([tdir[1] * nrm[2] - tdir[2] * nrm[1], tdir[2] * nrm[0] - tdir[0] * nrm[2], tdir[0] * nrm[1] - tdir[1] * nrm[0]]);
      m.cone('hair', base, add(base, mul(tdir, len * (0.75 + 0.5 * noise1(i + r * 7, seed + 1)))), rad, { ratio: 0.42, U: side, V: nrm, bone: BI.Head, n: 7, rings: 4, bend: mul(nrm, -0.02) });
    }
  };
  // ── hair cards: ribbons of the alpha-tested strand texture (assets/textures/hair_card) grown along curves over the
  // scalp, lit with the outward normal of the hair mass (so they shade like hair, not flat planes), with real tangents for
  // the anisotropic highlight along the strands. Each style keeps a solid core under its cards so it reads at game distance.
  let rs = sp.name.length * 977 + 13;
  const rnd = () => { rs = (Math.imul(rs, 1664525) + 1013904223) >>> 0; return rs / 4294967296; };
  const hcC = [0, hc[1] - 0.02 * HS, hc[2]];
  const ell = (p) => Math.hypot(p[0] / R, (p[1] - hc[1]) / HY, (p[2] - hc[2]) / RZ); // 1 on the skull ellipsoid
  /** a point on the scalp at ring height t and loft angle a (PI/2 the front), pushed out by o metres, and its outward direction */
  const scalpPt = (t, a, o = 0) => { const r = headRing(t); const cs = Math.cos(a), sn = Math.sin(a); const p = [r.ru * Math.sign(cs) * Math.pow(Math.abs(cs), 2 / r.e), r.c[1], r.c[2] + r.rv * Math.sign(sn) * Math.pow(Math.abs(sn), 2 / r.e)]; const n = norm(sub(p, hcC)); return [add(p, mul(n, o)), n]; };
  /** grow a curve from p along dir in seg steps of len/seg, bending by bend(i, p, d); kept off (head units) off the skull */
  const grow = (p, dir, len, seg, bend, off = 0) => { const pts = [p]; let d = norm(dir); for (let i = 0; i < seg; i++) { d = norm(bend(i, pts[pts.length - 1], d)); let q = add(pts[pts.length - 1], mul(d, len / seg)); if (off > 0) { const e = ell(q); if (e < 1 + off) q = add(q, mul(norm(sub(q, hcC)), (1 + off - e) * R)); } pts.push(q); } return pts; };
  /** q put back onto the scalp `o` metres out (the same height and angle around the head): lying cards follow the head */
  const onScalp = (q, o) => { const t = Math.max(-0.95, Math.min(0.995, (q[1] - hc[1]) / HY)), r = headRing(t); return scalpPt(t, Math.atan2((q[2] - r.c[2]) / r.rv, q[0] / r.ru), o)[0]; };
  /** grow a curve that lies on the scalp: each step along the (bent) direction, then back onto the scalp at o metres */
  const growS = (p, dir, len, seg, bend, o) => { const pts = [p]; let d = norm(dir); for (let i = 0; i < seg; i++) { d = norm(bend(i, pts[pts.length - 1], d)); const q = onScalp(add(pts[pts.length - 1], mul(d, len / seg)), o); d = norm(sub(q, pts[pts.length - 1])); pts.push(q); } return pts; };
  const crossv = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const dotv = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  // the cards' facing: per triangle, the sign of (face normal . its vertices' normals), summed over every card (checked below)
  const facing = { out: 0, in: 0 };
  /** one card along pts: width w0 at the root to w1 at the tip; twist 0 lies flat on the mass, PI/2 stands; o.outAt(i, p) is
      the outward direction of the mass there (the skull's by default), o.mat the material */
  const card = (pts, w0, w1, twist, weights, o = {}) => {
    const verts = [], tris = [], K = pts.length, u0 = o.u0 ?? rnd() * 0.6, uw = o.uw ?? 0.4, mat = o.mat ?? 'hairCard';
    for (let i = 0; i < K; i++) {
      const sv = i / (K - 1), tg = norm(sub(pts[Math.min(K - 1, i + 1)], pts[Math.max(0, i - 1)])), out = o.outAt ? o.outAt(i, pts[i]) : norm(sub(pts[i], hcC));
      const side0 = norm(crossv(tg, out)), up0 = norm(crossv(side0, tg));
      const side = add(mul(side0, Math.cos(twist)), mul(up0, Math.sin(twist))), w = lerp(w0, w1, Math.pow(sv, 0.8)) * 0.5;
      const n = norm(add(out, mul(tg, -0.15)));
      const tn = norm(sub(side, mul(n, dotv(side, n))));
      for (const k of [-1, 1]) verts.push({ p: add(pts[i], mul(side, k * w)), n, t: [...tn, 1], uv: [u0 + (k > 0 ? uw : 0), sv], w: weights(sv, pts[i]), tag: 'hair' });
      if (i) { const a = (i - 1) * 2, b = i * 2; tris.push([a, b + 1, b], [a, a + 1, b + 1]); }
    }
    // wound so the front face looks the way the normals point (out of the hair mass). A card wound the other way is a back
    // face from outside: double-sided shading turns its normal inward and the rim and key lights light the hair pale. Which
    // winding that is depends on the card's twist (a lying card and a standing tuft face different ways), so it is chosen
    // per card from the sum over its triangles. BRAWLER_CARD_WINDING=inward keeps the opposite choice: the facing check
    // below must stop it (its control).
    const fd = ([a, b, c]) => dotv(crossv(sub(verts[b].p, verts[a].p), sub(verts[c].p, verts[a].p)), add(add(verts[a].n, verts[b].n), verts[c].n));
    if ((tris.reduce((q, t) => q + fd(t), 0) < 0) !== CARD_INWARD) for (const t of tris) [t[1], t[2]] = [t[2], t[1]];
    for (const t of tris) { const d = fd(t); if (d > 0) facing.out++; else if (d < 0) facing.in++; }
    m.raw(mat, verts, tris);
  };
  const onHead = () => [[BI.Head, 1]];
  const thinShell = (mat, t0, t1, steps, arc) => { const rr = []; for (let i = 0; i <= steps; i++) { const t = t0 + (t1 - t0) * (i / steps), e = ss(0, 0.25, i / steps); rr.push(headRing(Math.min(t, 0.999), 1 + 0.006 * e, 0.0006 + 0.0012 * e)); } m.loft(mat, rr, { n: 32, bone: BI.Head, tile: 0.25, ...(arc ? { arc } : {}) }); };
  if (hairCut === 'spiky') { scalp(1.05, 0.004, sp.name.length); clumps(3, 0.085 * HS, 0.03 * HS, 0.6, sp.name.length); }
  if (hairCut === 'crop' || hairCut === 'long') {
    scalp(1.04, 0.003, sp.name.length);
    // short hair combed back: layered lying cards over the crown and the back (none low on the sides: the shell carries those)
    const n = hairCut === 'crop' ? 220 : 170;
    for (let i = 0; i < n; i++) {
      const t = lerp(0.38, 0.97, Math.pow(rnd(), 0.8)), a = PI / 2 + (rnd() * 2 - 1) * PI * 0.97;
      if (Math.sin(a) > 0.3 && t < 0.5 + 0.1 * Math.sin(a)) continue; // the forehead stays clear below the hairline
      const [p, nn] = scalpPt(t, a, 0.004 + 0.004 * rnd());
      const back = [0, hairCut === 'long' ? -0.35 : -0.15, -1];
      const dir = add(sub(back, mul(nn, dotv(back, nn))), [(rnd() - 0.5) * 0.25, 0, 0]);
      const len = (hairCut === 'crop' ? 0.04 + 0.03 * rnd() : 0.07 + 0.05 * rnd()) * HS;
      const pts = growS(p, dir, len, 4, (k, q, d) => d, 0.004 + 0.005 * rnd());
      card(pts, (hairCut === 'crop' ? 0.03 : 0.04) * HS, 0.012 * HS, 0, onHead);
    }
  }
  if (hairCut === 'long') { // a fall of hair down the back to the shoulder blades: a solid core and long cards over it
    const ya = hc[1] + 0.02, yb = yC + 0.06;
    const rr = ys(yb, ya, 0.03).reverse().map((y, i) => {
      const t = ss(ya, yb, y), hr = headRing(Math.max(-0.75, (y - hc[1]) / HY), 1.06, 0.006);
      hr.c = [0, y, hc[2] - 0.035 - 0.05 * t]; hr.ru = Math.max(hr.ru, R * (0.93 + 0.23 * t)); hr.rv = Math.max(hr.rv * 0.9, RZ * 0.68);
      hr.mod = (a) => 1 + 0.07 * Math.sin(a * 11 + i * 0.3) * t; return hr;
    });
    m.loft('hair', rr, { n: 26, arc: [BACK - 1.55, BACK + 1.55], weights: (k, P) => hairW(P), tile: 0.3 });
    for (let i = 0; i < 110; i++) {
      const a = BACK + (rnd() * 2 - 1) * 1.45, [p] = scalpPt(lerp(0.35, 0.75, rnd()), a, 0.012);
      const out = norm([Math.cos(a), 0, Math.sin(a)]);
      const pts = grow(p, [out[0] * 0.25, -1, out[2] * 0.25 - 0.15], (0.3 + 0.08 * rnd()) * HS, 8, (k, q, d) => add(d, [out[0] * 0.03, 0, out[2] * 0.06 - 0.02]));
      card(pts, 0.05 * HS, 0.025 * HS, 0, (sv, q) => hairW(q), { outAt: (k, q) => norm(add([q[0], 0, q[2] - hc[2]], [0, 0, -0.02])) });
    }
  }
  if (hairCut === 'mohawk') { // shaved sides; a crest of tall stiff tufts along the midline over thin cores, tips raked back
    m.loft('stubble', shell(0.3, 1.0, 1.015, 0.002, 7), { n: 28, bone: BI.Head, tile: 0.25 }); m.loft('stubble', shell(-0.5, 0.32, 1.015, 0.002, 6), { n: 18, arc: [BACK - 2.05, BACK + 2.05], bone: BI.Head, tile: 0.25 });
    for (let i = 0; i < 7; i++) {
      const u = i / 6, ang = lerp(0.55, -1.75, u), dir = norm([0, Math.cos(ang) * 0.9 + 0.1, Math.sin(ang) + 0.15]);
      const base = add(hc, [0, Math.cos(ang) * HY * 0.9, Math.sin(ang) * RZ * 0.9]);
      const h = (0.1 + 0.05 * Math.sin(u * PI)) * HS * (sp.mohawk ?? 1);
      m.cone('hair', base, add(base, mul(dir, h)), 0.026 * HS, { ratio: 0.4, U: [0, -dir[2], dir[1]], V: X, weights: () => (u > 0.6 ? [[BI.Head, 0.6], [BI.HairA, 0.4]] : [[BI.Head, 1]]), n: 8, bend: [0, -0.01, -0.035] });
    }
    for (let i = 0; i < 80; i++) {
      const u = i / 79, ang = lerp(0.62, -1.7, u), base = add(hc, [(rnd() - 0.5) * 0.022 * HS, Math.cos(ang) * HY * 0.97, Math.sin(ang) * RZ * 0.97]);
      const h = (0.11 + 0.07 * Math.sin(u * PI) + 0.03 * rnd()) * HS * (sp.mohawk ?? 1), nn = norm(sub(base, hcC));
      const dir = add(nn, [(rnd() - 0.5) * 0.25, 0.25, -0.15]);
      const pts = grow(base, dir, h, 5, (k, q, d) => add(d, [0, -0.02 * k, -0.07 * k]));
      const w = u > 0.55 ? [[BI.Head, 0.6], [BI.HairA, 0.4]] : [[BI.Head, 1]], tw = rnd() * PI;
      card(pts, 0.03 * HS, 0.004 * HS, PI / 2 + (rnd() - 0.5) * 0.4, () => w); card(pts, 0.022 * HS, 0.004 * HS, tw, () => w);
    }
  }
  if (hairCut === 'ponytail') { // swept back over the scalp to a tie at the crown, the tail on its spring bones
    thinShell('hair', 0.56, 1, 9); thinShell('hair', -0.42, 0.58, 8, [BACK - 1.75, BACK + 1.75]);
    const tie = add(hc, [0, HY * 0.5, -RZ * 0.9]);
    m.capsule('band', add(tie, [0, 0.008, 0.012]), add(tie, [0, -0.006, -0.01]), 0.02 * HS, 0.02 * HS, { bone: BI.Head, n: 12, caps: 2 });
    for (let i = 0; i < 190; i++) {
      const t = lerp(0.5, 0.98, Math.pow(rnd(), 0.7)), a = PI / 2 + (rnd() * 2 - 1) * PI * 0.97;
      const [p] = scalpPt(t, a, 0.0012 + 0.003 * rnd());
      const to = sub(tie, p), L0 = Math.hypot(...to);
      if (L0 < 0.03) continue;
      const pts = growS(p, to, L0 * 0.98, 6, (k, q, d) => add(d, mul(norm(sub(tie, q)), 0.35)), 0.002 + 0.003 * rnd());
      card(pts, 0.034 * HS, 0.03 * HS, 0, onHead);
    }
    { const a = add(tie, [0, -0.004, -0.012]), b = add(a, [0, -0.12, -0.07]), c = add(b, [0, -0.15, -0.01]);
      m.capsule('hair', a, b, 0.022 * HS, 0.03 * HS, { weights: (t) => [[BI.Head, 1 - t], [BI.HairA, t]], n: 12, caps: 3, ratio: 0.8, mod: (an) => 1 + 0.08 * Math.sin(an * 6) });
      m.capsule('hair', b, c, 0.03 * HS, 0.008 * HS, { weights: (t) => [[BI.HairA, 1 - t], [BI.HairB, t]], n: 12, caps: 3, ratio: 0.8, mod: (an) => 1 + 0.08 * Math.sin(an * 6) }); }
    const tw = (sv) => (sv < 0.4 ? [[BI.Head, 1 - sv / 0.4], [BI.HairA, sv / 0.4]] : [[BI.HairA, 1 - (sv - 0.4) / 0.6], [BI.HairB, (sv - 0.4) / 0.6]]);
    for (let i = 0; i < 46; i++) {
      const ang = rnd() * PI * 2, rr2 = 0.012 * Math.sqrt(rnd()), p = add(tie, [Math.cos(ang) * rr2, Math.sin(ang) * rr2 * 0.6, -0.008]);
      const len = (0.27 + 0.08 * rnd()) * HS, spread = [Math.cos(ang) * 0.35, 0.1, Math.sin(ang) * 0.15 - 0.6];
      const pts = grow(p, add([0, 0.25, -1], spread), len, 9, (k, q, d) => add(d, [Math.cos(ang) * 0.04 * (k < 4 ? 1 : -1.2), -0.32, 0.02]));
      card(pts, 0.04 * HS, 0.008 * HS, ang, (sv) => tw(sv));
    }
  }
  if (hairCut === 'pompadour') {
    // a 1950s pompadour as hair: a dark core (combed grooves, a soft pomade sheen) under layered cards: the sides combed
    // straight back from the temples to a duck tail where they meet at the back, and the tall front swoop rising off the
    // forehead, rolling forward and over, then lying back across the crown. The cards catch an anisotropic highlight
    // along the comb lines (the pomade), not a plastic clear coat.
    // the sides are combed back over the temples (no bare temple under the roll): the shell sinks under the skin only across
    // the middle of the forehead, where the roll rises from the hairline
    const pompLine = (t, a) => 1 - 0.13 * Math.pow(Math.max(0, Math.sin(a)), 6) * ss(0.56, 0.36, t);
    const groove = (t) => (a) => (1 + 0.016 * Math.sin(a * 34 + t * 3) + 0.006 * Math.sin(a * 91)) * pompLine(t, a);
    m.loft('pomade', shell(0.28, 1.0, 1.035, 0.002, 72, { mod: groove }), { n: 112, bone: BI.Head, tile: 0.25 }); // fine rings: where the shell sinks under the forehead skin the edge is a smooth curve, not a staircase
    m.loft('pomade', shell(-0.45, 0.32, 1.035, 0.002, 14, { mod: (t) => (a) => 1 + 0.012 * Math.sin(a * 30 + t * 2) }), { n: 40, arc: [BACK - 1.3, BACK + 1.3], bone: BI.Head, tile: 0.25 });
    m.loft('pomade', shell(0.06, 0.32, 1.035, 0.002, 10), { n: 52, arc: [BACK - 2.35, BACK + 2.35], bone: BI.Head, tile: 0.25 }); // its ends run in under the roll, not at the temple (a square step there)
    // The quiff (B-PT11, after the 1950s references in findings/brawler.md): the top hair is long (8 cm and more), backcombed
    // at the roots and combed up and over that padding, so it is one solid mass from the scalp up: it rises steeply off the
    // forehead hairline, stands about 9 cm over the skull at the front, leans some 5 cm out past the brow (the roll
    // overhangs the forehead), then lies back over the crown, narrowing and sinking toward the back of the head. Its side
    // section front to back, in head units (z over RZ from the head centre; the top and the underside over HY; the half
    // width over R): the underside sits just inside the skull (no daylight under the roll), lifting off it at the overhang.
    const PZ = [[1.25, 1.32, 0.8, 0.55], [1.18, 1.56, 0.56, 0.86], [1.06, 1.65, 0.42, 0.98], [0.9, 1.61, 0.42, 1.02], [0.66, 1.48, 0.5, 1.0], [0.35, 1.31, 0.72, 0.95],
      [0.0, 1.17, 0.88, 0.88], [-0.4, 1.04, 0.8, 0.8], [-0.7, 0.88, 0.6, 0.66], [-0.88, 0.7, 0.42, 0.45]];
    const pz = (u) => { const f = Math.max(0, Math.min(0.9999, u)) * (PZ.length - 1), i = Math.floor(f), t = EASE.io(f - i); return PZ[i].map((v, k) => lerp(v, PZ[i + 1][k], t)); };
    const K = 30;
    // qf(u): u 0 at the front of the roll, 1 at the back of the crown: the section's centre, its up direction and radii
    const qf = (u) => { const [z, top, bot, w] = pz(u); return { c: add(hc, [0, HY * (top + bot) / 2, RZ * z]), tg: [0, 0, -1], Vv: Y, ru: R * w, rv: HY * (top - bot) / 2 }; };
    const rr = [];
    for (let k = 0; k <= K; k++) { const u = k / K, q = qf(u), cap = Math.sin(Math.min(1, k / 3) * PI / 2) * Math.sin(Math.min(1, (K - k) / 2) * PI / 2); rr.push({ c: q.c, U: X, V: q.Vv, ru: Math.max(0.003, q.ru * 0.92 * Math.sqrt(cap)), rv: Math.max(0.003, q.rv * 0.9 * Math.sqrt(cap)), e: 2.1, mod: (a) => 1 + 0.03 * Math.sin(a * 13 + u * 6) }); }
    m.loft('pomade', rr, { n: 34, bone: BI.Head, tile: 0.2 });
    /** a point on the swoop's surface: u along it, s across (-1 .. 1), lift metres off the core */
    const swoop = (u, s, lift) => { const q = qf(u), cs = Math.sqrt(Math.max(0, 1 - s * s)); return add(q.c, add(mul(X, q.ru * s * 0.98), mul(q.Vv, q.rv * cs * 0.95 + lift))); };
    const swoopOut = (u, s) => { const q = qf(u), cs = Math.sqrt(Math.max(0, 1 - s * s)); return norm(add(mul(X, s / Math.max(0.05, q.ru)), mul(q.Vv, cs / Math.max(0.05, q.rv)))); };
    const pc = { mat: 'pomadeCard' };
    // the sides: from the temples and over the ears straight back, a little up, layered three deep
    for (let i = 0; i < 180; i++) {
      const sd = rnd() < 0.5 ? 1 : -1, t = lerp(0.12, 0.6, rnd()), a = sd > 0 ? lerp(0.15, -1.1, rnd()) : PI - lerp(0.15, -1.1, rnd());
      const [p, nn] = scalpPt(t, a, 0.006 + 0.004 * rnd());
      const dir = sub([0, 0.18, -1], mul(nn, dotv([0, 0.18, -1], nn)));
      const pts = growS(p, dir, (0.1 + 0.06 * rnd()) * HS, 6, (k, q, d) => add(d, [-sd * 0.06 * (k > 2 ? 1 : 0), 0, 0]), 0.006 + 0.006 * rnd());
      card(pts, 0.008 * HS, 0.036 * HS, 0, onHead, pc); // fine at the root: the square ends of wide cards made a stair-step along the hairline
    }
    // the hairline from the temples down past the ears: short tapered cards combed back along the edge of the slicked sides,
    // so the hair ends in strands instead of the hard edge of the shell
    for (let i = 0; i < 90; i++) {
      const sd = rnd() < 0.5 ? 1 : -1, t = lerp(0.06, 0.34, rnd()), a = sd > 0 ? lerp(0.35, -0.9, rnd()) : PI - lerp(0.35, -0.9, rnd());
      const [p, nn] = scalpPt(t, a, 0.004);
      const dir = sub([0, -0.05, -1], mul(nn, dotv([0, -0.05, -1], nn)));
      const pts = growS(p, dir, (0.035 + 0.025 * rnd()) * HS, 4, (k, q, d) => d, 0.004 + 0.002 * rnd());
      card(pts, 0.005 * HS, 0.022 * HS, 0, onHead, pc);
    }
    // the duck tail: the two sides meet down the back of the head, tips pointing down and in
    for (let i = 0; i < 110; i++) { // the sides meet in a seam down the back of the head, like the tail of a duck
      const sd = i & 1 ? 1 : -1, t = lerp(-0.45, 0.4, rnd()), [p, nn] = scalpPt(t, BACK + sd * lerp(0.25, 1.0, rnd()), 0.008);
      const dir = sub([-sd * 0.85, -0.45, -0.25], mul(nn, dotv([-sd * 0.85, -0.45, -0.25], nn)));
      const pts = growS(p, dir, (0.08 + 0.04 * rnd()) * HS, 5, (k, q, d) => d, 0.009 + 0.003 * rnd());
      card(pts, 0.04 * HS, 0.015 * HS, 0, onHead, pc);
    }
    // the swoop: strands from the hairline rise up the front of the roll, curl over it and lie back along its top
    for (let i = 0; i < 150; i++) {
      const s = (rnd() * 2 - 1) * 0.92, lift = 0.002 + 0.006 * rnd(), u1 = lerp(0.55, 1.0, rnd());
      // from the front of the roll back over it (the roll is solid down to the hairline now: strands hanging under the
      // overhang only showed their ragged ends)
      const pts = [], steps = 9;
      for (let k = 0; k <= steps; k++) pts.push(swoop(lerp(0.04, u1, k / steps), s * lerp(0.75, 1, k / steps), lift));
      card(pts, 0.05 * HS, 0.03 * HS, 0, onHead, { ...pc, outAt: (k) => swoopOut(lerp(0.04, u1, k / steps), s) });
    }
    // the top layer: shorter strands lying back along the crest, so the comb lines show
    for (let i = 0; i < 0; i++) { // (none: seen from the front, flat lying cards on the crest caught the sky as white patches)
      const s = (rnd() * 2 - 1) * 0.8, u0 = lerp(0.03, 0.4, rnd()), u1 = Math.min(1, u0 + lerp(0.3, 0.55, rnd())), steps = 6;
      const pts = []; for (let k = 0; k <= steps; k++) pts.push(swoop(lerp(u0, u1, k / steps), s, 0.009));
      card(pts, 0.045 * HS, 0.025 * HS, 0, onHead, { ...pc, outAt: (k) => swoopOut(lerp(u0, u1, k / steps), s) });
    }
    // the front curl: a few strands slipping off the front of the roll and curling down over the forehead
    for (let i = 0; i < 8; i++) {
      const s = 0.1 + (rnd() - 0.5) * 0.35, u0 = lerp(0.1, 0.18, rnd()), steps = 7, rad = (0.02 + 0.008 * rnd()) * HS;
      const p0 = swoop(u0, s, 0.006), o0 = swoopOut(u0, s);
      const pts = []; for (let k = 0; k <= steps; k++) { const a = (k / steps) * PI * 1.15; pts.push(add(p0, [0.004 * k * (s > 0.1 ? 1 : -1), rad * (Math.cos(a) - 1) * 1.2, rad * Math.sin(a) * 1.1 + 0.006 * k])); }
      card(pts, 0.03 * HS, 0.012 * HS, 0, onHead, { ...pc, outAt: (k) => norm(add(o0, [0, -0.3 * k / steps, 0.6 * k / steps])) });
    }
  }
  if (facing.out + facing.in > 0) {
    // most card triangles must face the way their normals point (standing cards sit edge on to the normal, so not all)
    console.log(`${sp.name}: hair cards face out ${facing.out}, in ${facing.in}`);
    if (facing.out <= 4 * facing.in) throw new Error(`${sp.name}: hair cards wound inward (${facing.out} triangles face out, ${facing.in} in): double-sided shading would light them from inside`);
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
  if (hairCut === 'trucker') { // a trucker cap pushed back on a mullet: the foam front stands tall with the letters on it
    scalp(1.05, 0.004, 5);
    const t0 = 0.2, steps = 12, front = { mod: (t) => (a) => 1 + 0.07 * gauss(wrapA(a - FRONT), 0.55) * ss(0.2, 0.65, t) };
    // u once round the head (the front at 0.5, decreasing with the loft angle so the words read left to right from
    // outside), v from the top of the crown (0) down to the band (1): the layout cap_logo is drawn for
    m.loft('capLogo', shell(t0, 1.0, 1.1, 0.012, steps, front), { n: 48, bone: BI.Head, uvFn: (p, k, j, n, a) => [0.75 - a / (PI * 2), 1 - (k / steps)] });
    const y = hc[1] + HY * 0.24;
    m.ellipsoid('capLogo', [0, y, hc[2] + RZ * 1.22], 0.095 * HS, 0.008, 0.08 * HS, { bone: BI.Head, steps: 4, n: 18, e: 2.4, uvFn: (p) => [0.02 + 0.3 * (p[0] + 0.12), 0.3 + 0.3 * (p[2] - hc[2])] });
    // the mullet: business under the cap, a fall to the collar at the back over a solid core
    const ya = hc[1] - 0.01, yb = yN + 0.035;
    const rr = ys(yb, ya, 0.025).reverse().map((yy, i) => {
      const tt = ss(ya, yb, yy), hr = headRing(Math.max(-0.75, (yy - hc[1]) / HY), 1.06, 0.006);
      hr.c = [0, yy, hc[2] - 0.03 - 0.035 * tt]; hr.ru = Math.max(hr.ru, R * (0.9 + 0.12 * tt)); hr.rv = Math.max(hr.rv * 0.9, RZ * 0.66);
      hr.mod = (a) => 1 + 0.08 * Math.sin(a * 9 + i * 0.4) * tt; return hr;
    });
    m.loft('hair', rr, { n: 22, arc: [BACK - 1.35, BACK + 1.35], weights: (k, P) => hairW(P), tile: 0.3 });
    for (let i = 0; i < 70; i++) {
      const a = BACK + (rnd() * 2 - 1) * 1.25, [p] = scalpPt(lerp(0.05, 0.3, rnd()), a, 0.012);
      const out = norm([Math.cos(a), 0, Math.sin(a)]);
      const pts = grow(p, [out[0] * 0.2, -1, out[2] * 0.3 - 0.1], (0.15 + 0.05 * rnd()) * HS, 6, (k, q, d) => add(d, [out[0] * 0.03, 0, out[2] * 0.07 - 0.01]));
      card(pts, 0.05 * HS, 0.03 * HS, 0, (sv, q) => hairW(q), { outAt: (k, q) => norm(add([q[0], 0, q[2] - hc[2]], [0, 0, -0.02])) });
    }
    // these cards come after the facing check above: the same rule, here
    console.log(`${sp.name}: hair cards face out ${facing.out}, in ${facing.in}`);
    if (facing.out <= 4 * facing.in) throw new Error(`${sp.name}: hair cards wound inward (${facing.out} triangles face out, ${facing.in} in)`);
  }
  if (hairCut === 'fedora') { // a sweat-stained fedora; under it a bald pate and a thin slicked fringe of hair at the sides
    m.loft('hair', shell(-0.45, 0.3, 1.02, 0.002, 8, { mod: tuft(4, 0.04) }), { n: 22, arc: [BACK - 1.6, BACK + 1.6], bone: BI.Head, tile: 0.25 });
    const y0 = hc[1] + HY * 0.32, pinch = (a) => 1 - 0.1 * Math.pow(Math.max(0, Math.sin(a)), 4);
    m.loft('hat', [[0, 0.002, 0.002], [0, 1.03, 1.05], [0.06, 1.02, 1.04], [0.11, 0.97, 0.99], [0.125, 0.8, 0.82], [0.13, 0.002, 0.002]].map(([dy, k, kk], i) => ({ c: [0, y0 + dy * HS, hc[2] - 0.005], U: X, V: Z, ru: R * k, rv: RZ * kk, e: 2.3, mod: i >= 3 ? pinch : null })), { n: 28, bone: BI.Head, tile: 0.3 });
    m.loft('band', [{ c: [0, y0 + 0.012 * HS, hc[2] - 0.005], U: X, V: Z, ru: R * 1.04, rv: RZ * 1.06 }, { c: [0, y0 + 0.032 * HS, hc[2] - 0.005], U: X, V: Z, ru: R * 1.035, rv: RZ * 1.055 }], { n: 28, bone: BI.Head, tile: 0.3 });
    m.ellipsoid('hat', [0, y0 + 0.004, hc[2] - 0.005], R * 1.6, 0.008, RZ * 1.5, { bone: BI.Head, steps: 4, n: 28, e: 2.2, mod: (a) => 1 + 0.06 * Math.sin(a) });
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
    const hmod = { mod: (t) => (a) => 1 + 0.035 * gauss(wrapA(a - FRONT), 0.18) * ss(0.2, 0.7, t) };
    if (hairCut === 'hardhat') m.loft('helmet', shell(0.1, 1.0, 1.17, 0.014, 10, hmod), { n: 32, bone: BI.Head, tile: 0.3 });
    else { // an open-faced riot helmet: the crown, then sides and back that leave the face clear
      m.loft('helmet', shell(0.4, 1.0, 1.2, 0.014, 7, hmod), { n: 32, bone: BI.Head, tile: 0.3 });
      m.loft('helmet', shell(-0.32, 0.42, 1.2, 0.014, 6), { n: 26, arc: [FRONT + 0.92, FRONT - 0.92 + PI * 2], bone: BI.Head, tile: 0.3 });
    }
    if (hairCut === 'hardhat') m.ellipsoid('helmet', [0, hc[1] + HY * 0.12, hc[2] + 0.01], R * 1.42, 0.01, RZ * 1.38, { bone: BI.Head, steps: 4, n: 28, e: 2.3, mod: (a) => 1 + 0.25 * Math.pow(Math.max(0, Math.sin(a)), 3) });
    if (hairCut === 'helmet') { // a visor bar and cheek guards
      m.loft('helmet', [headRing(0.36, 1.2, 0.012), headRing(0.4, 1.26, 0.022), headRing(0.47, 1.26, 0.022), headRing(0.5, 1.2, 0.014)], { n: 24, arc: [FRONT - 1.05, FRONT + 1.05], bone: BI.Head, tile: 0.3 });
      m.loft('glow', [headRing(0.415, 1.268, 0.023), headRing(0.455, 1.268, 0.023)], { n: 24, arc: [FRONT - 0.8, FRONT + 0.8], bone: BI.Head, tile: 0.3 });
      for (const sd of [1, -1]) m.ellipsoid('helmet', [sd * R * 1.1, hc[1] - 0.06 * HS, hc[2] - 0.02], 0.03 * HS, 0.065 * HS, 0.065 * HS, { bone: BI.Head, steps: 5, n: 12 });
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
    const ax = sx * sw; const sd = sx > 0 ? 'L' : 'R';
    const ringA = (y, k = 1, add = 0, fl = null) => { const [ru, rv] = armRad(y); return { c: [ax + sx * 0.008 * gauss(y - ySh, 0.05), y, 0.004], U: X, V: Z, ru: ru * k + add, rv: rv * k + add, e: 2.15, mod: fl ? (a) => armMod(y)(a) + folds(y, a, fl) : armMod(y) }; };
    const yTop = ySh + 0.05, yWristEnd = yWr - 0.005;
    const sleeveEnd = outerSleeve || sleeve === 'long' ? yWr + 0.03 : sleeve === 'short' ? ySh - 0.15 : yTop;
    const sFolds = [[yEl + 0.04, 0.05, 0.012, BACK, 0.9], [yEl - 0.0, 0.045, 0.012, FRONT, 0.6], [yEl - 0.035, 0.04, 0.012, BACK, 0.9], [yEl + 0.08, 0.03, 0.014, null], [yWr + 0.06, 0.035, 0.012, null]];
    const skinY = ys(yWristEnd, Math.min(yTop, sleeveEnd + 0.02), 0.02).reverse();
    if (skinY.length > 1) m.loft('skin', skinY.map((y) => ringA(y)), { n: 20, weights: (k, P) => armW(P, sd), tile: 0.3 });
    if (sleeveEnd < yTop) {
      const mat = outerSleeve ? 'outer' : shirtMat, th = outerSleeve ? 0.024 : 0.012;
      const yy = ys(sleeveEnd, yTop, 0.018).reverse();
      const rr = yy.map((y) => ringA(y, 1.06, th, sFolds));
      rr.push({ ...ringA(sleeveEnd, 1.08, th + 0.006), c: [ax, sleeveEnd - 0.005, 0.004] }); // the cuff
      m.loft(mat, rr, { n: 22, weights: (k, P) => armW(P, sd), tile: 0.4 });
      if (outerSleeve && sleeve !== 'long') { /* the jacket hides the shirt sleeve */ }
    }
    if (sp.pads) m.ellipsoid(sp.padMat ?? 'pad', [ax + sx * 0.035, ySh + 0.04, 0], 0.085 * (0.8 + 0.2 * W), 0.05, 0.095 * (0.8 + 0.2 * W), { weights: () => [[BI.Chest, 0.4], [BI['UpperArm' + (sx > 0 ? 'L' : 'R')], 0.6]], steps: 6, n: 16, e: 2.4 });
    if (sp.spikes) for (let i = 0; i < 3; i++) { const base = [ax + sx * (0.05 + 0.02 * i), ySh + 0.08 - 0.012 * i, -0.04 + 0.04 * i]; m.cone('buckle', base, add(base, [sx * 0.03, 0.05, 0]), 0.012, { weights: () => [[BI['UpperArm' + (sx > 0 ? 'L' : 'R')], 1]], n: 6 }); }
    if (sp.bracers) m.loft('metal', ys(yWr + 0.02, yEl - 0.04, 0.02).reverse().map((y) => ringA(y, 1.12, 0.012)), { n: 18, weights: (k, P) => armW(P, sd), tile: 0.3 });
    if (sp.wristband) m.loft('band', [ringA(yWr + 0.05, 1.12, 0.008), ringA(yWr + 0.015, 1.12, 0.008)], { n: 16, weights: (k, P) => armW(P, sd), tile: 0.3 });
    // the hand: a palm, four fingers in two segments (FingX1 curls them at the knuckles, FingX2 at the middle joints), a
    // thumb on its own bone; the clips pose them open, as fists or gripping
    const hg = handGeom(sp, sx, world), s = hg.s, hm = sp.gloves ? 'glove' : 'skin';
    const H = BI['Hand' + sd], F1 = BI['Fing' + sd + '1'], F2 = BI['Fing' + sd + '2'], TH = BI['Thumb' + sd], LA = BI['LowerArm' + sd];
    const { hx, hy } = hg, ky = hg.k1[1], kx = hg.k1[0];
    const palm = ys(ky + 0.006 * s, hy + 0.005, 0.01).reverse().map((y) => { const t = ss(hy, ky, y); return { c: [hx - sx * 0.002 * s * t, y, 0.004 * s], U: X, V: Z, ru: 0.021 * s * (1 - 0.1 * t), rv: 0.044 * s * (1 + 0.1 * gauss(t - 0.7, 0.3)), e: 3.0, mod: (a) => 1 + 0.12 * gauss(wrapA(a - (sx > 0 ? PI : 0)), 0.5) * gauss(t - 0.45, 0.3) }; });
    palm.push({ c: [kx, ky - 0.004 * s, 0.004 * s], U: X, V: Z, ru: 0.012 * s, rv: 0.036 * s, e: 3 });
    m.loft(hm, palm, { n: 18, weights: (k, P) => (P[1] > hy - 0.01 ? [[H, 0.7], [LA, 0.3]] : [[H, 1]]), tile: 0.2 });
    const FL = [[0.04, 1.0, 0.052], [0.014, 1.04, 0.058], [-0.012, 0.98, 0.054], [-0.036, 0.86, 0.042]]; // z, thickness, distal length
    for (const [fz, th, l2] of FL) {
      const z = fz * s, r = 0.0122 * s * th, K = [kx, ky + 0.006 * s, z], J = [kx, hg.k2[1], z], T = [kx - sx * 0.003 * s, hg.k2[1] - l2 * s, z];
      m.capsule(hm, K, J, r * 1.05, r, { n: 10, caps: 2, rings: 3, weights: (t) => (t < 0.25 ? [[H, 0.5 - 2 * t], [F1, 0.5 + 2 * t]] : t > 0.85 ? [[F1, 1 - (t - 0.85) * 3.3], [F2, (t - 0.85) * 3.3]] : [[F1, 1]]) });
      m.capsule(hm, J, T, r * 0.98, r * 0.82, { n: 10, caps: 2, rings: 3, weights: (t) => (t < 0.15 ? [[F1, 0.5 - 3.3 * t], [F2, 0.5 + 3.3 * t]] : [[F2, 1]]) });
      if (!sp.gloves) m.ellipsoid('nail', add(T, [sx * r * 0.72, 0.009 * s, 0]), 0.0035 * s, 0.01 * s, r * 0.72, { bone: F2, steps: 3, n: 6, e: 3 }); // a nail on the back of the tip
    }
    const tb = hg.tb, dT = norm([-sx * 0.3, -0.75, 0.6]), tm = add(tb, mul(dT, 0.036 * s)), tt = add(tm, mul(norm([-sx * 0.25, -0.85, 0.45]), 0.036 * s));
    m.capsule(hm, tb, tm, 0.017 * s, 0.014 * s, { n: 10, caps: 2, rings: 3, weights: (t) => [[H, 0.6 * (1 - t)], [TH, 0.4 + 0.6 * t]] });
    m.capsule(hm, tm, tt, 0.0135 * s, 0.011 * s, { n: 10, caps: 2, rings: 3, bone: TH });
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
  const pantsEnd = sp.cuffs ? yAn + 0.11 : bottom === 'shorts' ? yKn + 0.1 : bottom === 'none' ? yHip + 0.1 : sp.boots ? yAn + 0.2 : yAn + 0.075;
  const loose = bottom === 'cargo' ? 0.02 : bottom === 'tight' ? 0.004 : bottom === 'jeans' ? 0.014 : 0.02;
  for (const sx of [1, -1]) {
    const lx = sx * hw; const sd = sx > 0 ? 'L' : 'R';
    const ringL = (y, k = 1, add = 0, fl = null) => { const [ru, rv] = legRad(y); return { c: [lx, y, 0.0], U: X, V: Z, ru: ru * k + add, rv: rv * k + add, e: 2.1, mod: fl ? (a) => legMod(y, sx)(a) + folds(y, a, fl) : legMod(y, sx) }; };
    const yTop = yHip + 0.07;
    const pFolds = [[yKn + 0.05, 0.05, 0.014, BACK, 0.8], [yKn - 0.01, 0.04, 0.012, BACK, 0.8], [yKn + 0.02, 0.035, 0.014, FRONT, 0.5], [yHip - 0.06, 0.03, 0.015, FRONT + sx * 0.5, 0.5],
      [pantsEnd + 0.05, 0.05, 0.012, null], [pantsEnd + 0.1, 0.04, 0.012, null], [pantsEnd + 0.16, 0.03, 0.015, FRONT, 1]];
    if (bottom === 'cargo') pFolds.push([yHip - 0.25, 0.06, 0.045, sx > 0 ? 0 : PI, 0.45]); // side pockets
    if (pantsEnd > yAn + 0.03) m.loft('skin', ys(yAn + 0.02, pantsEnd + 0.02, 0.025).reverse().map((y) => ringL(y)), { n: 20, weights: (k, P) => legW(P, sd), tile: 0.3 });
    const rr = ys(pantsEnd, yTop, 0.022).reverse().map((y) => ringL(y, 1.05, loose + 0.01 * ss(yKn, yAn, y) * (bottom === 'jeans' ? 1 : 0), bottom === 'none' ? null : pFolds));
    rr.push({ ...ringL(pantsEnd, 1.08, loose + 0.006), c: [lx, pantsEnd - 0.006, 0] });
    m.loft(bottom === 'none' ? 'skin' : 'pants', rr, { n: 24, weights: (k, P) => legW(P, sd), tile: 0.5 });
    if (sp.cuffs) { // turned-up hems: a band of the lighter reverse of the denim, a rolled top edge and a stitched lower fold
      const cy = pantsEnd - 0.004, ch = 0.068, cl = loose + 0.016;
      m.loft('cuff', [[cy - 0.004, 1.06, cl - 0.002], [cy, 1.1, cl + 0.002], [cy + 0.01, 1.1, cl + 0.004], [cy + ch * 0.5, 1.1, cl + 0.002], [cy + ch - 0.008, 1.1, cl + 0.005], [cy + ch, 1.1, cl + 0.002], [cy + ch + 0.004, 1.05, cl - 0.006]].reverse().map(([y, k, a]) => ({ ...ringL(y, k, a), c: [lx, y, 0] })), { n: 24, weights: (k, P) => legW(P, sd), tile: 0.3 });
    }
    if (bottom === 'joggers' || bottom === 'tight') m.loft('band', ys(yHip - 0.35, yHip - 0.02, 0.03).reverse().map((y) => ringL(y, 1.05, loose + 0.003)), { n: 4, arc: sx > 0 ? [-0.12, 0.12] : [PI - 0.12, PI + 0.12], weights: (k, P) => legW(P, sd), tile: 0.3 }); // a side stripe
    if (sp.kneepads) m.ellipsoid('pad', [lx, yKn + 0.01, legR * 0.85], 0.05, 0.06, 0.03, { weights: () => [[BI['LowerLeg' + (sx > 0 ? 'L' : 'R')], 0.5], [BI['UpperLeg' + (sx > 0 ? 'L' : 'R')], 0.5]], steps: 5, n: 14 });
    // footwear: a loft heel to toe with a thick sole, boots with a shaft
    const f = B('Foot' + (sx > 0 ? 'L' : 'R')), F = BI['Foot' + (sx > 0 ? 'L' : 'R')], fs = sp.foot ?? 1.12, wsh = (0.82 + 0.18 * W) * fs;
    const sh = [[-0.1, 0.034, 0.03], [-0.09, 0.05, 0.06], [-0.04, 0.055, 0.072], [0.03, 0.057, 0.062], [0.1, 0.058, 0.052], [0.16, 0.052, 0.042], [0.2, 0.038, 0.032], [0.225, 0.016, 0.018]];
    m.loft('shoe', sh.map(([z, w, h]) => ({ c: [f[0], 0.022 + h * 0.5, f[2] + z * fs], U: X, V: Y, ru: w * wsh, rv: h * 0.55 * fs, e: 2.6 })), { n: 18, weights: (k, P) => { const t = 0.4 * ss(yAn - 0.03, yAn + 0.03, P[1]); return [[F, 1 - t], [BI['LowerLeg' + sd], t]]; }, tile: 0.3 });
    m.loft('sole', [[-0.104, 0.05], [-0.06, 0.06], [0.12, 0.064], [0.2, 0.046], [0.232, 0.02]].map(([z, w]) => ({ c: [f[0], 0.014, f[2] + z * fs], U: X, V: Y, ru: w * wsh * 1.06, rv: 0.016 * fs, e: 3.2 })), { n: 14, bone: F, tile: 0.3 });
    if (sp.boots) m.loft('shoe', ys(yAn - 0.01, sp.cuffs ? yAn + 0.1 : yAn + 0.22, 0.03).reverse().map((y) => ringL(y, 1.1, 0.016 + loose * 0.3)).concat([{ ...ringL(yAn - 0.02, 1.15, 0.02), c: [lx, yAn - 0.02, 0.01] }]), { n: 18, weights: (k, P) => legW(P, sd), tile: 0.3 });
  }
  return { m, world };
}

/** skin tones lose a little saturation so they read as skin, not orange plastic, under a warm key */
const desat = (c, k) => { const l = 0.3 * c[0] + 0.55 * c[1] + 0.15 * c[2]; return c.map((x) => lerp(x, l, k)); };
/** the face and eye textures each character wears */
function writeFace(sp) {
  const { M } = skeleton(sp), p = sp.paint ?? {};
  const skin = desat(hexRaw(sp.colors.skin), 0.14), hair = hexRaw(sp.colors.hair), col = (h) => (h == null ? undefined : Array.isArray(h) ? h : hexRaw(h));
  const t = paintFace(M, {
    seed: sp.name.length * 7 + sp.name.charCodeAt(0), skin, hair: col(p.beardColor) ?? hair, brow: col(p.browColor) ?? hair.map((c) => c * 0.75),
    stubble: p.stubble ?? (sp.stubble ? 1 : 0), shadow: p.shadow ?? (sp.stubble ? 0.6 : 0), goatee: sp.goatee, chinstrap: p.chinstrap,
    frown: sp.frown, age: p.age ?? 0, freckles: p.freckles, ruddy: p.ruddy, lipstick: col(sp.lip), shadowCol: col(p.eyeShadow), shadowAmt: p.eyeShadowAmt,
    liner: p.liner, rouge: col(p.rouge), war: col(sp.war), scars: p.scars ?? (sp.scar ? [[0.3, 0.32, 0.5, -0.12, 0.012, true]] : []), tattoos: p.tattoos,
    mole: p.mole, scalp: p.scalp, hairline: p.hairline, browSparse: p.browSparse, browSlit: p.browSlit,
  }, 512);
  writeSet(TEX, 'face_' + sp.name, t, 2.2);
  writeSet(TEX, 'eye_' + sp.name, paintEye(sp.eyes ? hexRaw(sp.eyes) : col(p.iris) ?? [0.3, 0.19, 0.1], 128, sp.name.length), 1.0);
}
// The file carries the shading (glTF material extensions, read by the engine's glTF loading), so the game converts
// nothing: ordinary cloth is matte with a soft grazing sheen and little specular; leather a thin clear coat; skin a
// moderate roughness with skin shading (extras.skin_shading: wrap and translucency) and a faint sheen; eyes a wet coat;
// hair a sheen, and hair cards the anisotropic highlight along the strands (KHR_materials_anisotropy, rotated so it runs
// along the card's v; the cards carry real tangents). Brows, lashes and stubble get no anisotropy (no clean direction).
function shading(cls, t, sp) {
  t = t ?? [1, 1, 1];
  const ext = {};
  if (cls === 'cloth') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((x) => 0.3 * (0.02 + 0.3 * x)), sheenRoughnessFactor: 0.85 }; ext.KHR_materials_specular = { specularFactor: 0.12 }; }
  if (cls === 'skin') {
    const sk = hexRaw(sp.colors.skin), dark = 1 - (sk[0] + sk[1] + sk[2]) / 3;
    ext.KHR_materials_sheen = { sheenColorFactor: [0.45 * (0.1 + 0.35 * t[0]), 0.45 * (0.03 + 0.15 * t[1]), 0.45 * (0.02 + 0.1 * t[2])], sheenRoughnessFactor: 0.6 };
    ext.KHR_materials_specular = { specularFactor: 0.45 }; ext.KHR_materials_ior = { ior: 1.4 };
    return { ext, extras: { skin_shading: { scatter: [lerp(1, 0.9, dark), lerp(0.36, 0.3, dark), lerp(0.24, 0.2, dark)], wrap: 0.5, translucency: lerp(0.38, 0.22, dark) } } };
  }
  if (cls === 'hair') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((x) => 0.4 * (0.05 + 0.4 * x)), sheenRoughnessFactor: 0.5 }; ext.KHR_materials_specular = { specularFactor: 0.12 }; }
  if (cls === 'card') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((x) => 0.02 + 0.15 * x), sheenRoughnessFactor: 0.6 }; ext.KHR_materials_specular = { specularFactor: 0.2 }; ext.KHR_materials_anisotropy = { anisotropyStrength: 0.3, anisotropyRotation: PI / 2 }; }
  // pomade is one of the things that shines: a wet, slick highlight along the comb lines (the same extensions as hair, so the
  // same program states, with more specular and a tighter sheen)
  if (cls === 'pomadecore') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((x) => 0.05 + 0.15 * x), sheenRoughnessFactor: 0.35 }; ext.KHR_materials_specular = { specularFactor: 0.5 }; }
  // (the cards: the gloss comes from the core under them; bright cards read as white flakes on the front of the roll)
  if (cls === 'pomade') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((x) => 0.02 + 0.06 * x), sheenRoughnessFactor: 0.5 }; ext.KHR_materials_specular = { specularFactor: 0.12 }; ext.KHR_materials_anisotropy = { anisotropyStrength: 0.25, anisotropyRotation: PI / 2 }; }
  if (cls === 'fur') { ext.KHR_materials_sheen = { sheenColorFactor: t.map((x) => 0.02 + 0.15 * x), sheenRoughnessFactor: 0.6 }; ext.KHR_materials_specular = { specularFactor: 0.15 }; }
  if (cls === 'leather') ext.KHR_materials_clearcoat = { clearcoatFactor: 0.35, clearcoatRoughnessFactor: 0.38 };
  if (cls === 'eye') { ext.KHR_materials_clearcoat = { clearcoatFactor: 0.7, clearcoatRoughnessFactor: 0.09 }; ext.KHR_materials_ior = { ior: 1.376 }; }
  if (cls === 'enamel') ext.KHR_materials_specular = { specularFactor: 0.6 };
  return { ext };
}
function materials(glb, sp) {
  const c = Object.fromEntries(Object.entries(sp.colors).map(([k, v]) => [k, hex(v)]));
  c.skin = desat(hexRaw(sp.colors.skin), 0.14).map((x) => Math.pow(x, 2.2));
  const d = TEX, P = (n, cls, set, o) => { const sh = shading(cls === 'gloss' && n === 'teeth' ? 'enamel' : cls === 'hair' && n === 'pomade' ? 'pomadecore' : cls, o.tint, sp); return [n, glb.pbr(cls + '_' + n, d, set, { ext: sh.ext, extras: sh.extras, ...o })]; };
  const fab = (k) => (k === 'denim' ? 'fab_denim' : k === 'knit' ? 'fab_knit' : k === 'leather' ? 'fab_leather' : k === 'dirty' ? 'fab_dirty' : 'fab_cloth');
  return Object.fromEntries([
    P('skin', 'skin', 'fab_skin', { tint: c.skin, rough: 1, normal: 0.7 }),
    P('face', 'skin', 'face_' + sp.name, { rough: 1, normal: 1 }),
    // eyes are wet (the game gives them a glossy clear coat), lashes are hair, teeth and the mouth are their own surfaces
    P('eye', 'eye', 'eye_' + sp.name, { rough: 1, normal: 0.6 }), P('lash', 'fur', 'fab_hair', { tint: c.hair.map((x) => x * 0.35 + 0.004), rough: 1 }),
    P('teeth', 'gloss', 'fab_skin', { tint: [0.78, 0.74, 0.64], rough: 0.62, normal: 0.2 }), P('mouth', 'gloss', 'fab_skin', { tint: [0.2, 0.035, 0.035], rough: 0.75, normal: 0.3 }),
    P('nail', 'skin', 'fab_skin', { tint: c.skin.map((x, i) => x * [1.05, 0.92, 0.92][i] + 0.04), rough: 0.7, normal: 0.3 }),
    P('shirt', sp.shirtFab === 'leather' ? 'leather' : 'cloth', fab(sp.shirtFab ?? 'cloth'), { tint: c.shirt, rough: 1, normal: 0.45 }),
    P('pants', sp.pantsFab === 'leather' ? 'leather' : 'cloth', fab(sp.pantsFab ?? 'denim'), { tint: c.pants, rough: 1, normal: 0.5 }),
    P('outer', (sp.outerFab ?? 'leather') === 'leather' ? 'leather' : 'cloth', fab(sp.outerFab ?? 'leather'), { tint: c.outer ?? c.accent, rough: 1, normal: 0.6, doubleSided: true }),
    P('shoe', 'leather', 'fab_leather', { tint: c.shoes, rough: 0.85 }), P('sole', 'cloth', 'fab_leather', { tint: [0.035, 0.035, 0.035], rough: 1 }),
    P('belt', 'leather', 'fab_leather', { tint: c.belt || [0.03, 0.025, 0.02], rough: 0.9 }), P('buckle', 'metal', 'paint_metal', { tint: [0.85, 0.75, 0.45], rough: 0.45 }),
    P('glove', 'leather', 'fab_leather', { tint: c.glove ?? c.accent, rough: 0.7 }),
    P('hair', 'hair', 'fab_hair', { tint: c.hair, rough: 1, normal: 1.2, doubleSided: true }),
    // the pompadour's solid core under its cards: combed grooves in the normal map, a soft pomade sheen (not a clear coat)
    P('pomade', 'hair', 'fab_hair', { tint: c.hair.map((x) => x * 0.85), rough: 0.55, normal: 1.6, doubleSided: true }),
    P('hairCard', 'card', 'hair_card', { tint: c.hair, rough: 1, normal: 0.4, doubleSided: true, alphaMode: 'MASK', alphaCutoff: 0.32 }),
    P('pomadeCard', 'pomade', 'hair_card', { tint: c.hair, rough: 0.85, normal: 0.5, doubleSided: true, alphaMode: 'MASK', alphaCutoff: 0.32 }),
    P('cuff', 'cloth', 'fab_denim', { tint: c.cuff ?? c.pants, rough: 1, normal: 0.6 }), P('stubble', 'fur', 'fab_hair', { tint: c.hair.map((x, i) => x * 0.6 + c.skin[i] * 0.4), rough: 1 }),
    P('hat', sp.hatFab === 'knit' ? 'cloth' : 'cloth', fab(sp.hatFab ?? 'cloth'), { tint: c.hat ?? c.accent, rough: 1 }),
    P('band', 'cloth', 'fab_cloth', { tint: c.band ?? c.accent, rough: 1 }),
    // the trucker cap: red twill with "Dumb" stitched on it, all in its own texture (tools/make-textures.mjs cap_logo)
    // (only in the files that wear it: every listed material and its textures go into each glb)
    ...(sp.hair === 'trucker' ? [P('capLogo', 'cloth', 'cap_logo', { tint: [1, 1, 1], rough: 1, normal: 0.8 })] : []),
    P('mask', 'cloth', 'fab_cloth', { tint: c.mask ?? c.accent, rough: 1 }), P('pad', 'leather', 'fab_leather', { tint: c.pad ?? c.accent, rough: 0.7 }),
    P('helmet', sp.hair === 'hardhat' ? 'gloss' : 'metal', 'paint_metal', { tint: c.helmet ?? c.hair, rough: sp.hair === 'hardhat' ? 0.35 : 0.5, metal: sp.hair === 'hardhat' ? 0 : 1 }), P('metal', 'metal', 'paint_metal', { tint: c.metal ?? [0.5, 0.52, 0.55], rough: 0.4 }),
    P('shades', 'gloss', 'fab_leather', { tint: [0.015, 0.015, 0.02], rough: 0.15, metal: 0 }),
    P('reflect', 'gloss', 'fab_cloth', { tint: [0.7, 0.72, 0.68], rough: 0.4, emissive: [0.22, 0.24, 0.2] }),
    P('glow', 'gloss', null, { tint: [0.1, 0.4, 0.5], rough: 0.2, emissive: c.glow ?? [0.2, 1.4, 1.8] }),
  ]);
}

// ── faces and hands per clip: which expression (weights over time u 0..1 through the clip) and how each hand is held ──
const strike = (peak) => (u) => ss(0, peak, u) * (1 - 0.55 * ss(0.72, 1, u)); // build to the blow, ease off on recovery
const hitEnv = (u) => ss(0, 0.12, u) * (1 - 0.6 * ss(0.45, 1, u));
const CLIP_FACE = {
  run: () => [['effort', 0.4]], jump: (u) => [['effort', 0.6 * Math.sin(u * PI)]],
  punch1: (u) => [['snarl', strike(0.35)(u)]], punch2: (u) => [['snarl', strike(0.38)(u)]], kick: (u) => [['snarl', strike(0.38)(u)]],
  stab: (u) => [['snarl', strike(0.4)(u)]], knee: (u) => [['snarl', strike(0.32)(u)]],
  finisher: (u) => [['shout', strike(0.45)(u)]], haymaker: (u) => [['effort', ss(0, 0.3, u) * (1 - ss(0.38, 0.44, u))], ['shout', ss(0.38, 0.46, u) * (1 - 0.7 * ss(0.6, 1, u))]], jumpkick: (u) => [['shout', strike(0.25)(u)]], swing: (u) => [['shout', strike(0.45)(u)]],
  slam: (u) => [['effort', ss(0, 0.4, u) * (1 - ss(0.45, 0.6, u))], ['shout', ss(0.45, 0.62, u) * (1 - 0.6 * ss(0.8, 1, u))]],
  spin: (u) => [['shout', strike(0.2)(u)]], throw: (u) => [['effort', ss(0, 0.3, u) * (1 - ss(0.35, 0.5, u))], ['shout', ss(0.35, 0.55, u) * (1 - 0.6 * ss(0.8, 1, u))]],
  grab: () => [['effort', 0.7]], pickup: (u) => [['effort', 0.5 * Math.sin(u * PI)]], getup: (u) => [['effort', 0.8 * Math.sin(u * PI * 0.9)]], getup_prone: (u) => [['effort', 0.8 * Math.sin(u * PI * 0.9)]],
  hit: (u) => [['wince', hitEnv(u)]], hit_gut: (u) => [['wince', hitEnv(u)], ['grimace', 0.4 * hitEnv(u)]], hit_hard: (u) => [['wince', hitEnv(u)], ['grimace', 0.3 * hitEnv(u)]],
  knockdown: (u) => [['wince', ss(0, 0.1, u) * (1 - ss(0.4, 0.7, u))], ['ko', ss(0.45, 0.85, u)]],
  down: () => [['ko', 1]], dead: () => [['dead', 1]], grabbed: () => [['grimace', 0.85]], thrown: () => [['wince', 0.7], ['grimace', 0.4]],
  flash: (u) => [['bliss', ss(0.3, 0.5, u) * (1 - ss(0.85, 1, u))]], lurch: () => [['grin', 0.35]],
  comb: (u) => [['grin', ss(0.05, 0.2, u) * (1 - ss(0.88, 1, u))]], shoulder: () => [['effort', 0.85]], flick: (u) => [['snarl', strike(0.6)(u)]],
  thumbsup: (u) => [['grin', ss(0.04, 0.18, u) * (1 - ss(0.86, 1, u))]], thump: (u) => [['effort', ss(0, 0.3, u) * (1 - ss(0.55, 0.62, u))], ['shout', ss(0.55, 0.62, u) * (1 - ss(0.7, 0.95, u))]],
  taunt: (u) => [['grin', ss(0.05, 0.25, u) * (1 - ss(0.85, 1, u))]], block: () => [['wince', 0.35], ['effort', 0.3]],
};
// hand holds: [knuckle curl, middle curl, thumb across (0 open .. 1 over the fingers)]
const THUMB_UP = [1.55, 1.8, 0, [-1.05, 0.0, 0.3]]; // fingers curled tight, the thumb raised off them
const FIST = [1.5, 1.75, 1], LOOSE = [0.55, 0.75, 0.5], OPEN = [0.08, 0.12, 0], GRIP = [0.95, 1.1, 0.65], CLAW = [0.55, 1.0, 0.3];
const both = (h) => () => ({ L: h, R: h });
const CLIP_HANDS = {
  walk: both(LOOSE), hit_hard: both(OPEN), knockdown: (u) => ({ L: u < 0.3 ? CLAW : LOOSE, R: u < 0.3 ? CLAW : LOOSE }), down: both(LOOSE), dead: both(LOOSE),
  getup: (u) => ({ L: u < 0.6 ? OPEN : FIST, R: u < 0.6 ? OPEN : FIST }), getup_prone: (u) => ({ L: u < 0.6 ? OPEN : FIST, R: u < 0.6 ? OPEN : FIST }), grab: both(GRIP), knee: both(GRIP), throw: (u) => ({ L: u < 0.45 ? GRIP : OPEN, R: u < 0.45 ? GRIP : OPEN }),
  grabbed: both(CLAW), thrown: both(OPEN), spin: both(OPEN), pickup: (u) => ({ L: LOOSE, R: u < 0.15 ? OPEN : GRIP }), jump: both(LOOSE),
  taunt: (u) => { const t = u * 1.4, b = t > 0.6 && t < 1.15 ? 0.5 + 0.5 * Math.cos(((t - 0.66) / 0.14) * PI) : 0; return { L: OPEN, R: t < 0.55 ? OPEN : [lerp(0.1, 1.3, b), lerp(0.1, 1.5, b), 0.2] }; },
  hit: both(LOOSE), hit_gut: both(LOOSE),
  comb: () => ({ L: LOOSE, R: GRIP }), flick: (u) => ({ L: u < 0.55 ? GRIP : FIST, R: u < 0.55 ? GRIP : FIST }),
  thumbsup: (u) => ({ L: FIST, R: u > 0.1 && u < 0.85 ? THUMB_UP : FIST }), thump: both(FIST),
};
/** the face and hand bones of one sampled pose: rotations (radians) and offsets in head units (scaled by R in emit) */
function faceHands(name, u) {
  const out = {};
  for (const [ex, w] of (CLIP_FACE[name] ?? (() => []))(u)) {
    for (const [b, v] of Object.entries(EXPRESSIONS[ex])) {
      if (b !== 'Jaw') continue; // the rest of the face is morph targets the game drives by name; the Jaw bone opens the mouth
      const o = (out[b] ||= { r: [0, 0, 0], p: [0, 0, 0] });
      for (let i = 0; i < 3; i++) { o.r[i] += (v.r?.[i] ?? 0) * w; o.p[i] += (v.p?.[i] ?? 0) * w; }
    }
  }
  const hands = (CLIP_HANDS[name] ?? both(FIST))(u);
  for (const [sd, sx] of [['L', 1], ['R', -1]]) {
    const [c1, c2, th, tr] = hands[sd];
    out['Fing' + sd + '1'] = { r: [0, 0, -sx * c1] }; out['Fing' + sd + '2'] = { r: [0, 0, -sx * c2] };
    out['Thumb' + sd] = { r: tr ? [tr[0], sx * tr[1], sx * tr[2]] : [lerp(-0.45, 0.55, th), sx * lerp(0.0, 0.85, th), -sx * lerp(-0.35, 0.95, th)] };
  }
  return out;
}

function emit(sp) {
  const glb = new Glb();
  writeFace(sp);
  const mats = materials(glb, sp);
  const { m: g, world } = build(sp);
  const { T, M } = skeleton(sp);
  const FR = M.H.R; // expression offsets are in head units
  // two skinned mesh nodes: Face (the head surface with its lids and ears, the lash rims: the expression morph targets) and
  // Body (the rest). A morphed mesh draws on its own and reads every target per vertex, so only the head carries them.
  const targets = faceTargets(g, M, HEAD_GROUPS);
  const names = FACE_TARGETS.filter((n) => HEAD_GROUPS.some((h) => targets[n][h]?.any));
  const head = [], body = [];
  for (const [gg, mi] of g.prims(mats)) {
    const gname = [...g.groups].find(([, x]) => x === gg)[0];
    if (HEAD_GROUPS.includes(gname)) { gg.targets = names.map((n) => targets[n][gname]); head.push([gg, mi]); } else body.push([gg, mi]);
  }
  const jointNodes = BONE_NAMES.map((n) => glb.node({ name: n, translation: T[n] }));
  BONE_NAMES.forEach((n, i) => { if (PARENT[n] !== -1) (glb.nodes[jointNodes[BI[PARENT[n]]]].children ||= []).push(jointNodes[i]); });
  const ibm = new Float32Array(BONE_NAMES.length * 16);
  BONE_NAMES.forEach((n, i) => { const w = world[n]; ibm.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -w[0], -w[1], -w[2], 1], i * 16); });
  glb.skins.push({ name: 'Armature', inverseBindMatrices: glb.acc(ibm, 'MAT4', 5126, 0), joints: jointNodes, skeleton: jointNodes[0] });
  const meshNodes = [glb.node({ name: 'Body', mesh: glb.mesh(sp.name + '_body', body), skin: 0 }), glb.node({ name: 'Face', mesh: glb.mesh(sp.name + '_head', head, null, names), skin: 0 })];
  const s = sp.scale ?? 1; // the root hip node scales the whole body
  glb.nodes[jointNodes[0]].translation = [0, T.Hips[1] * s, 0];
  glb.nodes[jointNodes[0]].scale = [s, s, s];
  glb.sceneNodes.push(...meshNodes, jointNodes[0]);
  for (const c of CLIPS) {
    if (c.only && !c.only.includes(sp.name)) continue;
    const n = Math.max(2, Math.round(c.len * 30) + 1);
    const times = Array.from({ length: n }, (_, i) => (i / (n - 1)) * c.len);
    const poses = times.map((t) => ({ ...c.pose(t), ...faceHands(c.name, t / c.len) })), chans = [];
    for (const name of BONE_NAMES) {
      const rot = [], pos = [];
      let prev = null;
      poses.forEach((p, i) => {
        let r = (p[name] && p[name].r) || [0, 0, 0];
        if (c.loop && i === n - 1) r = (poses[0][name] && poses[0][name].r) || [0, 0, 0];
        let q = quat(r);
        if (prev && q[0] * prev[0] + q[1] * prev[1] + q[2] * prev[2] + q[3] * prev[3] < 0) q = q.map((v) => -v); // keep one hemisphere
        prev = q; rot.push(...q);
        let o = (p[name] && p[name].p) || [0, 0, 0];
        if (c.loop && i === n - 1) o = (poses[0][name] && poses[0][name].p) || [0, 0, 0];
        if (name === 'Hips') pos.push(T.Hips[0] + o[0] * s, T.Hips[1] * s + o[1] * s, T.Hips[2] + o[2] * s);
      });
      const node = jointNodes[BI[name]];
      // face and hand bones are keyed in every clip (the game layers blinks over the lids and reads the clip's value back)
      const always = FACE_BONES.includes(name) || HAND_BONES.includes(name);
      if (always || rot.some((v, i) => Math.abs(v - [0, 0, 0, 1][i % 4]) > 1e-6)) chans.push({ node, path: 'rotation', times, values: rot });
      if (name === 'Hips') chans.push({ node, path: 'translation', times, values: pos });
      if (FACE_BONES.includes(name)) {
        const tv = []; poses.forEach((p, i) => { const pp = (c.loop && i === n - 1 ? poses[0] : p)[name]?.p ?? [0, 0, 0]; tv.push(T[name][0] + pp[0] * FR, T[name][1] + pp[1] * FR, T[name][2] + pp[2] * FR); });
        chans.push({ node, path: 'translation', times, values: tv });
      }
    }
    glb.anim(c.name, chans);
  }
  glb.write(join(OUT, sp.name + '.glb'));
  console.log(sp.name, 'tris', g.tris(), 'targets', names.join(','));
}

// fighters, enemies, bosses: proportions, outfit pieces and colours. Silhouette first: each type differs in bulk, head
// shape and outline (hood, mohawk, long coat, tails, top hat, hard hat, plates) before colour.
export const SPECS = [
  // fighters
  { name: 'rook', face: { noseW: 1.05, hook: 0.6, brow: 1.2, browThick: 0.075, jawAng: 1.3, chin: 1.2, cleft: 0.5 }, paint: { age: 0.2, hairline: true, iris: 0x5a3418, scars: [[0.17, 0.27, 0.28, 0.13, 0.01, false]] },
    stubble: true, scale: 1.06, width: 1.3, chest: 1.08, sw: 0.23, arm: 1.3, leg: 1.12, head: 1.12, hand: 1.3, jaw: 1.25, hair: 'crop', gloves: true, belt: true, boots: true,
    top: 'tee', outer: true, outerKind: 'jacket', outerFab: 'leather', openFront: 0.45, sleeves: 'short', bottom: 'jeans',
    colors: { skin: 0xc98e60, shirt: 0xd8d2c0, pants: 0x2c4a80, shoes: 0x3b2618, hair: 0x2a1d14, accent: 0xb02a2a, outer: 0x4a2a1c, glove: 0xb8231f, belt: 0x201a14 } },
  { name: 'mika', face: { eyeR: 0.185, noseLen: 0.85, noseW: 0.82, noseProj: 0.85, tipUp: 1, lipU: 1.2, lipL: 1.25, mw: 0.23, cheek: 1.25, jawAng: 0.6, chin: 0.8, chinW: 0.8, brow: 0.6, browThick: 0.04, browArch: 0.06, tilt: 0.08, socket: 0.9, naso: 0.4 }, paint: { freckles: 1, liner: 0.6, eyeShadow: [0.55, 0.36, 0.36], eyeShadowAmt: 0.3, iris: [0.25, 0.42, 0.22], hairline: true },
    scale: 0.97, width: 0.9, chest: 0.95, sw: 0.185, hw: 0.095, arm: 0.95, leg: 1.0, head: 1.14, hand: 1.25, jaw: 0.75, nose: 0.8, hair: 'ponytail', hairRoot: [0, 0.203, -0.1], hairB: [0, -0.13, -0.06], headband: true, wristband: true,
    top: 'tank', bottom: 'tight', pantsFab: 'cloth', lip: 0xb04a50,
    colors: { skin: 0xf0c4a0, shirt: 0xe89a20, pants: 0x262836, shoes: 0xe8e8e8, hair: 0x8a2d1a, accent: 0xd03040, band: 0xd03040 } },
  { name: 'jet', face: { noseW: 1.35, noseLen: 0.95, noseProj: 0.85, lipU: 1.35, lipL: 1.4, mw: 0.27, browThick: 0.055, cheek: 1.1, jawAng: 1.1 }, paint: { chinstrap: true, browSlit: 1, beardColor: [0.07, 0.05, 0.04], iris: [0.16, 0.09, 0.05], scalp: 0.7 },
    scale: 1.0, width: 1.05, sw: 0.205, arm: 1.12, leg: 1.05, head: 1.13, hand: 1.3, hair: 'mohawk', shades: true, wristband: true,
    top: 'tank', bottom: 'cargo', pantsFab: 'cloth', shirtFab: 'knit', kneepads: true,
    colors: { skin: 0x8a5838, shirt: 0x2f9a72, pants: 0x55583a, shoes: 0xf0d030, hair: 0xe0e0e8, accent: 0xf0d030, band: 0xf0d030, pad: 0x202020 } },
  // enemies
  { name: 'thug', face: { noseW: 1.25, hook: 0.3, crook: 0.6, brow: 1.5, browThick: 0.07, eyeR: 0.16, socket: 1.25, jawAng: 1.4, chin: 1.1, jowl: 0.4, cheek: 0.8 }, paint: { age: 0.3, scars: [[-0.55, -0.05, -0.42, -0.35, 0.012, true]], tattoos: [{ kind: 'bars', x: -0.78, y: 0.12, s: 0.08 }], iris: [0.3, 0.24, 0.16] },
    stubble: true, frown: true, scale: 1.02, width: 1.2, belly: 0.5, sw: 0.215, arm: 1.15, leg: 1.1, head: 1.1, jaw: 1.2, hair: 'beanie', hatFab: 'knit', hood: true,
    top: 'hoodie', shirtFab: 'cloth', bottom: 'jeans', boots: false,
    colors: { skin: 0xc0906a, shirt: 0x5a2626, pants: 0x34384a, shoes: 0x1c1c1c, hair: 0x1a1410, accent: 0x2a2a2a, hat: 0x282828, band: 0xd8d8d8 } },
  { name: 'punk', face: { noseLen: 1.05, noseW: 0.85, hook: 0.2, cheek: 1.2, hollow: 0.8, jawAng: 0.8, chin: 1.3, chinW: 0.7, brow: 0.9, browThick: 0.035, eyeR: 0.17, tilt: 0.1 }, paint: { liner: 1, browSparse: 0.3, scalp: 0.6, iris: [0.3, 0.38, 0.4], tattoos: [{ kind: 'tear', x: 0.37, y: -0.17, s: 0.05 }, { kind: 'star', x: -0.6, y: -0.18, s: 0.09 }, { kind: 'tribal', x: 0.78, y: 0.32, s: 0.16 }] }, piercings: [[0.12, -0.36, 0.022], [-0.55, 0.25, 0.018], [-0.47, 0.27, 0.018]], lipRing: true,
    war: 0xb02060, frown: true, scale: 0.97, width: 0.88, sw: 0.19, arm: 0.95, head: 1.12, hair: 'mohawk', mohawk: 1.5, pads: true, spikes: true, padMat: 'outer', wristband: true, boots: true, belt: true,
    top: 'none', outer: true, outerKind: 'vest', outerFab: 'leather', openFront: 0.55, bottom: 'tight', pantsFab: 'cloth',
    colors: { skin: 0xe0b890, shirt: 0x2a2a30, pants: 0x4a2a5a, shoes: 0x101010, hair: 0x30d0a0, accent: 0xa0a0b0, outer: 0x18181c, band: 0x101010 } },
  { name: 'runner', face: { mono: 1, fold: 0, lidUp: 0.24, eyeR: 0.165, tilt: 0.1, noseProj: 0.75, cheek: 1.25, hollow: 0.5, jawAng: 0.9, chin: 0.9, socket: 0.8, brow: 0.75, browThick: 0.05, browArch: 0.015 }, paint: { iris: [0.12, 0.07, 0.04], shadow: 0.2, hairline: true },
    scale: 0.95, width: 0.82, sw: 0.185, hw: 0.095, arm: 0.92, leg: 0.95, legLen: 1.04, head: 1.1, hair: 'capback', jaw: 0.9,
    top: 'longsleeve', outer: true, outerKind: 'jacket', outerFab: 'cloth', openFront: 0.12, bottom: 'joggers', pantsFab: 'cloth',
    colors: { skin: 0xb07e58, shirt: 0xe8e8e8, pants: 0x2a2e3a, shoes: 0xd04030, hair: 0x201810, accent: 0xd04030, outer: 0x2a5aa8, hat: 0xd04030, band: 0xe8e8e8 } },
  { name: 'knifer', face: { eyeR: 0.16, socket: 1.3, brow: 1.3, bags: 1, lidUp: 0.22, tilt: -0.03 }, paint: { age: 0.35, iris: [0.35, 0.4, 0.3], scars: [[0.27, 0.36, 0.47, -0.04, 0.011, true]], hairline: true },
    frown: true, scale: 1.0, width: 0.92, sw: 0.195, arm: 1.0, head: 1.1, jaw: 0.9, mask: true, hair: 'crop', boots: true,
    top: 'longsleeve', outer: true, outerKind: 'longcoat', outerFab: 'leather', openFront: 0.3, bottom: 'tight', pantsFab: 'cloth', gloves: false,
    colors: { skin: 0xc49470, shirt: 0x2a2a2a, pants: 0x1c2a1c, shoes: 0x101010, hair: 0x101010, accent: 0x505850, outer: 0x3a3a30, mask: 0x6a1a1a } },
  { name: 'bruiser', face: { brow: 1.7, browThick: 0.075, eyeR: 0.15, socket: 1.35, noseW: 1.4, crook: -0.8, hook: 0.5, noseProj: 0.9, jawAng: 1.6, chin: 1.4, jowl: 0.3, cauli: 1, earOut: 1.3, cheek: 0.9, naso: 1.2 }, paint: { scalp: 1, age: 0.45, iris: [0.3, 0.25, 0.18], scars: [[0.3, 0.32, 0.5, -0.12, 0.012, true], [-0.2, 0.78, 0.15, 0.56, 0.014, true]], tattoos: [{ kind: 'cross', x: -0.84, y: 0.2, s: 0.1 }] },
    stubble: true, scar: true, frown: true, goatee: true, scale: 1.24, width: 1.6, chest: 1.15, belly: 0.9, sw: 0.27, hw: 0.12, arm: 1.5, leg: 1.25, head: 1.05, hand: 1.45, jaw: 1.4, neck: 1.35, hair: 'none', belt: true, boots: true, wristband: true,
    top: 'tank', shirtFab: 'cloth', bottom: 'cargo', pantsFab: 'cloth',
    colors: { skin: 0xb07a52, shirt: 0xd0ccb8, pants: 0x4a4430, shoes: 0x201810, hair: 0x281810, accent: 0x8a2a1a, belt: 0x141010, band: 0x101010 } },
  // the ranged thrower: tall and lean, forward cap, spiky hair under it, a sleeveless vest over a tee, shorts
  { name: 'thrower', face: { noseLen: 1.1, noseW: 0.95, eyeR: 0.17, brow: 0.9, cheek: 0.9, hollow: 0.3, chin: 0.9, jawAng: 0.9, mw: 0.24, lipU: 0.9 }, paint: { freckles: 0.7, stubble: 0.6, iris: [0.35, 0.5, 0.6], hairline: true, tattoos: [{ kind: 'star', x: -0.55, y: -0.2, s: 0.06, color: [0.06, 0.08, 0.18] }] },
    stubble: true, scale: 1.0, width: 0.86, sw: 0.19, hw: 0.095, arm: 1.02, leg: 0.98, legLen: 1.06, head: 1.1, jaw: 1.0, hair: 'cap', wristband: true,
    top: 'tee', outer: true, outerKind: 'vest', outerFab: 'cloth', openFront: 0.4, sleeves: 'short', bottom: 'shorts', pantsFab: 'cloth',
    colors: { skin: 0xd09a70, shirt: 0xe0c040, pants: 0x3a4a2a, shoes: 0x6a6a6a, hair: 0x302010, accent: 0x50a030, outer: 0x2a3a20, hat: 0x50a030, band: 0xe0c040 } },
  // the guard: broad, riot helmet with a visor, armour plates and pads (the shield is a separate prop in the game)
  { name: 'guard', face: { eyeR: 0.16, brow: 1.3, jawAng: 1.5, chin: 1.3, cleft: 1, noseW: 1.1, noseLen: 1.05, cheek: 0.9, lidUp: 0.26, mw: 0.24, lipU: 0.8, lipL: 0.9 }, paint: { shadow: 0.4, age: 0.25 },
    frown: true, scale: 1.08, width: 1.35, chest: 1.1, sw: 0.235, hw: 0.11, arm: 1.25, leg: 1.15, head: 1.0, hand: 1.35, jaw: 1.25, neck: 1.2, hair: 'helmet', pads: true, padMat: 'metal', armor: true, gloves: true, boots: true, belt: true, kneepads: true,
    top: 'longsleeve', shirtFab: 'cloth', bottom: 'cargo', pantsFab: 'cloth',
    colors: { skin: 0xb88a64, shirt: 0x1c2638, pants: 0x232a36, shoes: 0x111111, hair: 0x202020, accent: 0xd0d8e0, metal: 0x3a4a64, helmet: 0x26324a, glove: 0x161616, belt: 0x0e0e0e, pad: 0x3a4a64 }, eyes: 0xa0c0ff },
  // the redneck: a big soft-bellied man in a red trucker cap that says "Dumb", a grimy stained tee, faded jeans and
  // work boots, a mullet and a scruffy moustache (a regular enemy: a wild haymaker and a shoulder charge)
  { name: 'redneck', face: { noseW: 1.2, noseLen: 1.05, hook: 0.15, crook: 0.3, brow: 1.2, browThick: 0.06, eyeR: 0.155, socket: 1.1, jawAng: 1.1, chin: 1.0, jowl: 0.7, cheek: 0.95, naso: 1.1, lipU: 0.85, mw: 0.25 },
    paint: { age: 0.35, ruddy: 0.8, stubble: 0.85, shadow: 0.3, iris: [0.3, 0.38, 0.45], hairline: true, browColor: [0.3, 0.2, 0.12], beardColor: [0.34, 0.23, 0.13] },
    stubble: true, frown: true, moustache: 'thick', scale: 1.06, width: 1.4, belly: 1.15, chest: 1.0, sw: 0.225, hw: 0.11, arm: 1.25, leg: 1.12, head: 1.08, hand: 1.4, jaw: 1.2, neck: 1.25,
    hair: 'trucker', boots: true, belt: true, top: 'tee', sleeves: 'short', shirtFab: 'dirty', shirtTile: 1.0, bottom: 'jeans', pantsFab: 'denim',
    colors: { skin: 0xd49a78, shirt: 0xf4f0e6, pants: 0x46607e, shoes: 0x4a3220, hair: 0x5a3c22, accent: 0x8a2a1a, belt: 0x3a2614 } },
  // the creep: a pale, sweaty man in a long worn trench coat (collar up, belted loose), a fedora over a bald pate, a pencil
  // moustache and dark rings under the eyes; he enjoys being hit (a regular enemy)
  { name: 'creep', face: { eyeR: 0.165, socket: 1.35, bags: 1.4, lidUp: 0.2, noseLen: 1.12, noseW: 0.9, hook: 0.35, cheek: 0.8, hollow: 0.9, jawAng: 0.75, chin: 0.85, chinW: 0.75, brow: 0.8, browThick: 0.03, browArch: 0.06, mw: 0.26, lipU: 0.7, lipL: 0.9 },
    paint: { age: 0.45, shadow: 0.2, iris: [0.32, 0.3, 0.22], eyeShadow: [0.5, 0.36, 0.38], eyeShadowAmt: 0.45, browColor: [0.25, 0.2, 0.16], beardColor: [0.2, 0.15, 0.12], ruddy: 0.15 },
    moustache: 'pencil', scale: 1.0, width: 0.95, belly: 0.35, sw: 0.2, hw: 0.1, arm: 1.0, leg: 1.0, head: 1.12, hand: 1.3, jaw: 0.85, neck: 0.95,
    hair: 'fedora', boots: true, belt: true, top: 'longsleeve', shirtFab: 'cloth', outer: true, outerKind: 'longcoat', outerFab: 'cloth', openFront: 0.22, bottom: 'tight', pantsFab: 'cloth',
    colors: { skin: 0xe6cbbf, shirt: 0xcfc8b4, pants: 0x3a3630, shoes: 0x2a1e16, hair: 0x2e241c, accent: 0x5a4a34, outer: 0x8a7652, hat: 0x5c4a34, band: 0x2a2018, belt: 0x6a5a40 } },
  // bosses
  { name: 'foreman', face: { noseW: 1.3, noseLen: 1.15, noseProj: 1.1, hook: 0.3, bags: 1.2, jowl: 1.0, naso: 1.6, brow: 1.3, browThick: 0.075, browArch: 0.03, cheek: 0.8, jawAng: 1.2, ear: 1.15, lidUp: 0.22 }, paint: { age: 1, ruddy: 0.6, stubble: 0.8, browColor: [0.55, 0.53, 0.5], beardColor: [0.42, 0.39, 0.35], iris: [0.35, 0.4, 0.45], hairline: true },
    stubble: true, frown: true, moustache: 'thick', scale: 1.34, width: 1.6, belly: 1.0, chest: 1.1, sw: 0.25, hw: 0.11, arm: 1.5, leg: 1.25, head: 1.04, hand: 1.45, jaw: 1.35, neck: 1.3, hair: 'hardhat', boots: true, gloves: true, belt: true,
    top: 'longsleeve', shirtFab: 'cloth', outer: true, outerKind: 'hivis', outerFab: 'cloth', openFront: 0.3, bottom: 'cargo', pantsFab: 'denim', kneepads: true,
    colors: { skin: 0xa8704a, shirt: 0x3a4a60, pants: 0x30343a, shoes: 0x3a2410, hair: 0x2a2a2a, accent: 0xe0b020, outer: 0xf08a10, helmet: 0xf0c020, glove: 0x8a6a40, belt: 0x141010, pad: 0x202020 } },
  { name: 'matron', face: { eyeR: 0.17, tilt: 0.12, noseLen: 1.1, noseW: 0.85, hook: 0.6, noseProj: 1.1, cheek: 1.4, hollow: 0.7, lipL: 1.05, mw: 0.24, jawAng: 0.8, chinW: 0.8, brow: 0.8, browThick: 0.035, browArch: 0.08, browTilt: 0.06, bags: 0.4, naso: 0.9 }, paint: { age: 0.55, liner: 0.9, eyeShadow: [0.32, 0.1, 0.35], eyeShadowAmt: 0.75, rouge: [0.7, 0.25, 0.3], mole: [-0.3, -0.48], browColor: [0.5, 0.5, 0.56], hairline: true },
    frown: true, scale: 1.2, width: 1.2, chest: 1.05, sw: 0.21, hw: 0.105, arm: 1.1, head: 1.1, jaw: 0.85, nose: 0.9, hair: 'long', pads: true, padMat: 'outer', boots: true, belt: true, lip: 0x901838,
    top: 'longsleeve', shirtFab: 'cloth', outer: true, outerKind: 'longcoat', outerFab: 'leather', openFront: 0.35, bottom: 'tight', pantsFab: 'leather', hairRoot: [0, 0.08, -0.12], hairB: [0, -0.16, -0.02],
    colors: { skin: 0xe8c0a0, shirt: 0x2a1a2a, pants: 0x1a1a24, shoes: 0x500a28, hair: 0xd0d0e0, accent: 0xe0d060, outer: 0x6a1a5a, belt: 0x201018 }, eyes: 0xe02060 },
  { name: 'ringmaster', face: { noseLen: 1.1, noseProj: 1.1, tipUp: -0.5, cheek: 1.2, jowl: 0.5, chin: 1.2, cleft: 0.4, browThick: 0.06, browArch: 0.08, browTilt: 0.04, lidUp: 0.26, mw: 0.26, naso: 1.2 }, paint: { age: 0.5, rouge: [0.75, 0.3, 0.3], ruddy: 0.3, shadow: 0.25, iris: [0.2, 0.3, 0.15], hairline: true },
    scale: 1.14, width: 1.1, belly: 0.6, sw: 0.215, arm: 1.05, head: 1.12, jaw: 1.1, moustache: 'handlebar', hair: 'tophat', belt: false, boots: true, pads: true, padMat: 'buckle',
    top: 'longsleeve', shirtFab: 'cloth', outer: true, outerKind: 'tailcoat', outerFab: 'cloth', openFront: 0.32, bottom: 'tight', pantsFab: 'cloth',
    colors: { skin: 0xe0b890, shirt: 0xf0ece0, pants: 0xe8e4d8, shoes: 0x0a0a0a, hair: 0x1a1a1a, accent: 0xe8c840, outer: 0xb01a28, hat: 0x141414, band: 0xb01a28 } },
  { name: 'colossus', face: { brow: 2, browThick: 0.08, eyeR: 0.15, socket: 1.5, noseW: 1.3, crook: 0.5, jawAng: 2, chin: 1.6, chinW: 1.2, cheek: 1.3, ear: 0.9, cauli: 0.6, mw: 0.26, lipU: 0.8, lipL: 0.9 }, paint: { age: 0.4, browSparse: 0.6, scars: [[0.15, 0.42, 0.35, -0.3, 0.014, true], [-0.1, -0.45, -0.25, -0.75, 0.012, true], [-0.6, 0.1, -0.35, -0.2, 0.012, false]] }, glowEyes: true,
    scar: true, frown: true, scale: 1.62, width: 1.7, chest: 1.2, sw: 0.27, hw: 0.115, arm: 1.6, leg: 1.35, head: 1.0, hand: 1.5, jaw: 1.5, neck: 1.5, hair: 'helmet', pads: true, padMat: 'metal', armor: true, bracers: true, gloves: true, boots: true, belt: true,
    top: 'longsleeve', shirtFab: 'leather', bottom: 'cargo', pantsFab: 'leather',
    colors: { skin: 0x8a9696, shirt: 0x5a4030, pants: 0x3e3c40, shoes: 0x1a1612, hair: 0x6a7a8a, accent: 0x40c0d0, metal: 0xa0acb8, helmet: 0x8a96a2, glove: 0x4a3426, belt: 0x2a1c14, pad: 0x9aa6b2 }, eyes: 0x40e0ff },
  // Arthur: a 1950s greaser. Black leather jacket over a white tee, turned-up blue jeans over black boots, a tall glossy pompadour
  { name: 'arthur', face: { jawAng: 1.35, chin: 1.2, chinW: 1.05, cleft: 0.55, brow: 1.15, browThick: 0.062, browArch: 0.07, browTilt: -0.03, cheek: 1.2, hollow: 0.35, noseLen: 1.02, noseW: 0.95, noseProj: 1.1, lipU: 0.75, lipL: 0.85, mw: 0.25, tilt: 0.03, lidUp: 0.23, socket: 1.1 },
    paint: { iris: [0.2, 0.34, 0.55], shadow: 0.35, stubble: 0.25, hairline: true, browColor: [0.035, 0.03, 0.028], scars: [[0.52, 0.42, 0.62, 0.3, 0.008, false]] },
    scale: 1.12, width: 1.25, chest: 1.12, sw: 0.228, hw: 0.105, arm: 1.25, leg: 1.1, head: 1.08, hand: 1.35, jaw: 1.25, neck: 1.15, hair: 'pompadour', pomade: true, boots: true, belt: true, cuffs: true,
    top: 'tee', sleeves: 'short', outer: true, outerKind: 'jacket', outerFab: 'leather', openFront: 0.42, bottom: 'jeans', pantsFab: 'denim',
    colors: { skin: 0xd6a47c, shirt: 0xf0eee8, pants: 0x274a86, cuff: 0x7d98c4, shoes: 0x0b0b0c, hair: 0x0b0907, accent: 0x111111, outer: 0x0d0d0f, belt: 0x0f0d0c } },
];
const only = process.argv.slice(2);
for (const s of SPECS) if (!only.length || only.includes(s.name)) emit(s);
