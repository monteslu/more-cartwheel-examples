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
import { face } from './make-textures.mjs';
import { writeSet } from './texlib.mjs';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'models');
mkdirSync(OUT, { recursive: true });

// ── the skeleton: names and parents are fixed; translations come from each character's proportions ──
const BONE_NAMES = ['Hips', 'Spine', 'Chest', 'Neck', 'Head', 'UpperArmL', 'LowerArmL', 'HandL', 'UpperArmR', 'LowerArmR', 'HandR',
  'UpperLegL', 'LowerLegL', 'FootL', 'UpperLegR', 'LowerLegR', 'FootR', 'Belly', 'HairA', 'HairB', 'CoatL', 'CoatR'];
const PARENT = { Hips: -1, Spine: 'Hips', Chest: 'Spine', Neck: 'Chest', Head: 'Neck', UpperArmL: 'Chest', LowerArmL: 'UpperArmL', HandL: 'LowerArmL',
  UpperArmR: 'Chest', LowerArmR: 'UpperArmR', HandR: 'LowerArmR', UpperLegL: 'Hips', LowerLegL: 'UpperLegL', FootL: 'LowerLegL',
  UpperLegR: 'Hips', LowerLegR: 'UpperLegR', FootR: 'LowerLegR', Belly: 'Spine', HairA: 'Head', HairB: 'HairA', CoatL: 'Hips', CoatR: 'Hips' };
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
  for (const n of BONE_NAMES) { const p = PARENT[n] === -1 ? [0, 0, 0] : world[PARENT[n]]; world[n] = add(p, T[n]); }
  return { T, world };
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
const clip = (name, len, loop, pose) => CLIPS.push({ name, len, loop, pose });

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

// ── pose to rotation tracks ─────────────────────────────────────────────
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
  const nr = 0.058 * (0.85 + 0.15 * W) * (sp.neck ?? 1);
  m.loft('skin', ys(yN - 0.02, yHd + 0.06, 0.02).map((y) => ({ c: [0, y, -0.008 + 0.012 * ss(yN, yHd + 0.06, y)], U: X, V: Z, ru: nr * (1 + 0.25 * ss(yN + 0.03, yN - 0.02, y)), rv: nr * 0.95, e: 2.2, mod: (a) => 1 + 0.06 * gauss(wrapA(a - FRONT), 0.4) * gauss(y - (yN + 0.06), 0.03) })), { n: 18, weights: (k, P) => (P[1] < yN + 0.02 ? torsoW(P, 0) : headW(P)), tile: 0.3 });

  // ── head: cranium, jaw, cheekbones, brow, nose, ears; the painted face projects from the front ──
  const hc = [0, yHd + 0.125 * HS, 0.008 * HS];
  const R = 0.118 * HS, RZ = 0.128 * HS, HY = 0.142 * HS, jawW = sp.jaw ?? 1;
  const headRing = (t, k = 1, add = 0) => {
    const s = Math.sqrt(Math.max(0, 1 - t * t));
    const jaw = t < -0.15 ? lerp(1, 0.72 + 0.2 * jawW, ss(-0.15, -0.95, t)) : 1;
    const zoff = t < -0.25 ? 0.022 * HS * ss(-0.25, -0.9, t) : 0;
    return { c: [0, hc[1] + HY * t, hc[2] + zoff], U: X, V: Z, ru: Math.max(0.002, R * s * jaw * k + add), rv: Math.max(0.002, RZ * s * (t < -0.2 ? 0.9 : 1) * k + add), e: t < -0.3 ? 2.6 + 0.6 * jawW : 2.3,
      mod: (a) => 1 + 0.05 * gauss(t + 0.05, 0.12) * (gauss(wrapA(a - (FRONT - 0.85)), 0.35) + gauss(wrapA(a - (FRONT + 0.85)), 0.35)) // cheekbones
        + 0.06 * gauss(t - 0.15, 0.25) * gauss(wrapA(a - BACK), 1.0) }; // the back of the skull
  };
  const hrings = []; for (let k = 0; k <= 16; k++) hrings.push(headRing(-1 + (2 * k) / 16));
  m.loft('face', hrings, { n: 32, bone: BI.Head, uvFn: (p) => (p[2] < hc[2] - 0.02 ? [0.03, 0.03] : [0.5 + p[0] / (0.27 * HS), 0.5 - (p[1] - hc[1]) / (0.31 * HS)]) });
  const hb = { bone: BI.Head, steps: 6, n: 10 };
  m.ellipsoid('skin', [0, hc[1] - 0.012 * HS, hc[2] + RZ * 0.97], 0.019 * HS, 0.036 * HS, 0.03 * HS * (sp.nose ?? 1), hb); // nose bridge
  m.ellipsoid('skin', [0, hc[1] - 0.042 * HS, hc[2] + RZ * 1.03], 0.026 * HS, 0.016 * HS, 0.022 * HS * (sp.nose ?? 1), hb); // nose tip
  for (const sd of [1, -1]) m.ellipsoid('skin', [sd * R * 0.98, hc[1] - 0.006 * HS, hc[2] - 0.012], 0.014 * HS, 0.034 * HS, 0.024 * HS, hb); // ears
  m.ellipsoid('skin', [0, hc[1] + 0.035 * HS, hc[2] + RZ * 0.86], 0.092 * HS, 0.013 * HS, 0.024 * HS, { ...hb, e: 2 }); // brow ridge
  m.ellipsoid('skin', [0, hc[1] - 0.118 * HS, hc[2] + RZ * 0.6], 0.04 * HS * jawW, 0.024 * HS, 0.022 * HS, hb); // chin
  if (sp.shades) m.ellipsoid('shades', [0, hc[1] + 0.012 * HS, hc[2] + RZ * 0.92], 0.104 * HS, 0.026 * HS, 0.022 * HS, { bone: BI.Head, steps: 6, n: 16, e: 3 });
  if (sp.moustache) for (const sd of [1, -1]) m.capsule('hair', [sd * 0.008, hc[1] - 0.058 * HS, hc[2] + RZ * 0.98], [sd * 0.06 * HS, hc[1] - 0.07 * HS, hc[2] + RZ * 0.8], 0.012 * HS, 0.005 * HS, { bone: BI.Head, n: 8, caps: 2 });
  if (sp.beard) { const r = []; for (let i = 0; i <= 6; i++) { const t = -1.06 + 0.5 * (i / 6); const hr = headRing(t, 1.07, 0.006); hr.mod = (a) => 1 + 0.05 * noise1(a * 6 + i, 3); r.push(hr); } m.loft('hair', r, { n: 24, arc: [FRONT - 1.7, FRONT + 1.7], bone: BI.Head, tile: 0.2 }); }
  if (sp.goatee) m.cone('hair', [0, hc[1] - 0.115 * HS, hc[2] + RZ * 0.66], [0, hc[1] - 0.17 * HS, hc[2] + RZ * 0.62], 0.03 * HS, { bone: BI.Head, n: 10, ratio: 0.7 });
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
    m.loft('hair', shell(-0.55, 0.3, k, add, 6, { mod: tuft(seed + 3, 0.08) }), { n: 18, arc: [BACK - 2.05, BACK + 2.05], bone: BI.Head, tile: 0.25 }); // the sides and back
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
  if (hairCut === 'crop' || hairCut === 'long' || hairCut === 'ponytail' || hairCut === 'spiky') { scalp(1.05, 0.004, sp.name.length); clumps(hairCut === 'crop' ? 4 : 3, 0.085 * HS, 0.03 * HS, hairCut === 'ponytail' ? 0.9 : 0.6, sp.name.length); }
  if (hairCut === 'spiky') {
    for (let i = 0; i < 13; i++) {
      const th = (i / 13) * PI * 2, ph = 0.35 + 0.45 * ((i * 7) % 5) / 5, dir = norm([Math.cos(th) * Math.sin(ph), Math.cos(ph) + 0.25, Math.sin(th) * Math.sin(ph) - 0.35]);
      const base = add(hc, [dir[0] * R * 0.85, dir[1] * HY * 0.8, dir[2] * RZ * 0.85]);
      m.cone('hair', base, add(base, mul(dir, 0.085 * HS)), 0.035 * HS, { weights: () => [[BI.Head, 1]], n: 7, bend: [0, -0.01, -0.02] });
    }
  }
  if (hairCut === 'mohawk') {
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
  if (hairCut === 'ponytail') {
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
    const sleeveEnd = outerSleeve || sleeve === 'long' ? yWr + 0.03 : sleeve === 'short' ? ySh - 0.15 : yTop;
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
    if (sp.pads) m.ellipsoid(sp.padMat ?? 'pad', [ax + sx * 0.035, ySh + 0.04, 0], 0.085 * (0.8 + 0.2 * W), 0.05, 0.095 * (0.8 + 0.2 * W), { weights: () => [[BI.Chest, 0.4], [BI['UpperArm' + (sx > 0 ? 'L' : 'R')], 0.6]], steps: 6, n: 16, e: 2.4 });
    if (sp.spikes) for (let i = 0; i < 3; i++) { const base = [ax + sx * (0.05 + 0.02 * i), ySh + 0.08 - 0.012 * i, -0.04 + 0.04 * i]; m.cone('buckle', base, add(base, [sx * 0.03, 0.05, 0]), 0.012, { weights: () => [[BI['UpperArm' + (sx > 0 ? 'L' : 'R')], 1]], n: 6 }); }
    if (sp.bracers) m.loft('metal', ys(yWr + 0.02, yEl - 0.04, 0.02).reverse().map((y) => ringA(y, 1.12, 0.012)), { n: 18, weights: (k, P) => armW(P), tile: 0.3 });
    if (sp.wristband) m.loft('band', [ringA(yWr + 0.05, 1.12, 0.008), ringA(yWr + 0.015, 1.12, 0.008)], { n: 16, weights: (k, P) => armW(P), tile: 0.3 });
    // the fist: palm block, curled fingers with knuckles, the thumb across the front
    const hy = yWr, glove = sp.gloves ? 1.25 : 1, hm = sp.gloves ? 'glove' : 'skin', s = HN * glove;
    const hx = ax + sx * 0.002, H = BI['Hand' + (sx > 0 ? 'L' : 'R')];
    const palm = ys(hy - 0.075 * s, hy + 0.005, 0.012).reverse().map((y) => { const t = ss(hy, hy - 0.075 * s, y); return { c: [hx, y, 0.006 * s], U: X, V: Z, ru: 0.024 * s * (1 - 0.15 * t), rv: 0.045 * s * (1 + 0.08 * gauss(t - 0.6, 0.3)), e: 3.2 }; });
    palm.push({ c: [hx, hy - 0.083 * s, 0.006 * s], U: X, V: Z, ru: 0.012 * s, rv: 0.03 * s, e: 3 });
    m.loft(hm, palm, { n: 18, weights: (k, P) => (P[1] > hy - 0.01 ? [[H, 0.7], [BI['LowerArm' + (sx > 0 ? 'L' : 'R')], 0.3]] : [[H, 1]]), tile: 0.2 });
    const fz0 = -0.035 * s, fz1 = 0.052 * s, fy = hy - 0.072 * s, fx = hx - sx * 0.012 * s; // the curled fingers: a bar with four knuckles
    const fr = []; for (let i = 0; i <= 16; i++) { const z = lerp(fz0, fz1, i / 16), kn = 0.85 + 0.15 * Math.abs(Math.cos(((i / 16) * 4) * PI)); const e = ss(0, 0.12, i / 16) * ss(1, 0.88, i / 16); fr.push({ c: [fx, fy, z], U: X, V: Y, ru: Math.max(0.002, 0.027 * s * kn * (0.4 + 0.6 * e)), rv: Math.max(0.002, 0.025 * s * kn * (0.4 + 0.6 * e)), e: 2.3 }); }
    m.loft(hm, fr, { n: 14, bone: H, tile: 0.2 });
    m.capsule(hm, [hx - sx * 0.022 * s, hy - 0.025 * s, 0.035 * s], [hx - sx * 0.03 * s, hy - 0.06 * s, 0.05 * s], 0.014 * s, 0.012 * s, { bone: H, n: 10, caps: 3 }); // thumb
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
  const loose = bottom === 'cargo' ? 0.02 : bottom === 'tight' ? 0.004 : bottom === 'jeans' ? 0.014 : 0.02;
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
  return { m, world };
}

/** the face texture each character wears, and the materials a character is made of */
function writeFace(sp) {
  const skin = hexRaw(sp.colors.skin), hair = hexRaw(sp.colors.hair);
  writeSet(TEX, 'face_' + sp.name, face({ name: sp.name, skin, eyes: sp.eyes ? hexRaw(sp.eyes) : [0.28, 0.17, 0.09], brow: hair.map((c) => c * 0.5), stubble: sp.stubble, scar: sp.scar, frown: sp.frown, war: sp.war ? hexRaw(sp.war) : null, seed: sp.name.length, lip: sp.lip ? hexRaw(sp.lip) : undefined }), 1.5);
}
// material names start with a class the game reads (skin_, cloth_, hair_, ...) to give them sheen and softer specular
function materials(glb, sp) {
  const c = Object.fromEntries(Object.entries(sp.colors).map(([k, v]) => [k, hex(v)]));
  const d = TEX, P = (n, cls, set, o) => [n, glb.pbr(cls + '_' + n, d, set, o)];
  const fab = (k) => (k === 'denim' ? 'fab_denim' : k === 'knit' ? 'fab_knit' : k === 'leather' ? 'fab_leather' : 'fab_cloth');
  return Object.fromEntries([
    P('skin', 'skin', 'fab_skin', { tint: c.skin, rough: 1, normal: 0.7 }),
    P('face', 'skin', 'face_' + sp.name, { clamp: true, rough: 1, normal: 1 }),
    P('shirt', sp.shirtFab === 'leather' ? 'leather' : 'cloth', fab(sp.shirtFab ?? 'cloth'), { tint: c.shirt, rough: 1, normal: 0.45 }),
    P('pants', sp.pantsFab === 'leather' ? 'leather' : 'cloth', fab(sp.pantsFab ?? 'denim'), { tint: c.pants, rough: 1, normal: 0.5 }),
    P('outer', (sp.outerFab ?? 'leather') === 'leather' ? 'leather' : 'cloth', fab(sp.outerFab ?? 'leather'), { tint: c.outer ?? c.accent, rough: 1, normal: 0.6, doubleSided: true }),
    P('shoe', 'leather', 'fab_leather', { tint: c.shoes, rough: 0.85 }), P('sole', 'cloth', 'fab_leather', { tint: [0.035, 0.035, 0.035], rough: 1 }),
    P('belt', 'leather', 'fab_leather', { tint: c.belt || [0.03, 0.025, 0.02], rough: 0.9 }), P('buckle', 'metal', 'paint_metal', { tint: [0.85, 0.75, 0.45], rough: 0.45 }),
    P('glove', 'leather', 'fab_leather', { tint: c.glove ?? c.accent, rough: 0.7 }),
    P('hair', 'hair', 'fab_hair', { tint: c.hair, rough: 1, normal: 1.2, doubleSided: true }), P('stubble', 'hair', 'fab_hair', { tint: c.hair.map((x, i) => x * 0.6 + c.skin[i] * 0.4), rough: 1 }),
    P('hat', sp.hatFab === 'knit' ? 'cloth' : 'cloth', fab(sp.hatFab ?? 'cloth'), { tint: c.hat ?? c.accent, rough: 1 }),
    P('band', 'cloth', 'fab_cloth', { tint: c.band ?? c.accent, rough: 1 }),
    P('mask', 'cloth', 'fab_cloth', { tint: c.mask ?? c.accent, rough: 1 }), P('pad', 'leather', 'fab_leather', { tint: c.pad ?? c.accent, rough: 0.7 }),
    P('helmet', sp.hair === 'hardhat' ? 'gloss' : 'metal', 'paint_metal', { tint: c.helmet ?? c.hair, rough: sp.hair === 'hardhat' ? 0.35 : 0.5, metal: sp.hair === 'hardhat' ? 0 : 1 }), P('metal', 'metal', 'paint_metal', { tint: c.metal ?? [0.5, 0.52, 0.55], rough: 0.4 }),
    P('shades', 'gloss', 'fab_leather', { tint: [0.015, 0.015, 0.02], rough: 0.15, metal: 0 }),
    P('reflect', 'gloss', 'fab_cloth', { tint: [0.7, 0.72, 0.68], rough: 0.4, emissive: [0.22, 0.24, 0.2] }),
    P('glow', 'gloss', null, { tint: [0.1, 0.4, 0.5], rough: 0.2, emissive: c.glow ?? [0.2, 1.4, 1.8] }),
  ]);
}

function emit(sp) {
  const glb = new Glb();
  writeFace(sp);
  const mats = materials(glb, sp);
  const { m: g, world } = build(sp);
  const { T } = skeleton(sp);
  // one skinned mesh node per material class (Skin, Cloth, Leather, Hair, Metal, Gloss): the game finds them by name and gives
  // each class its shading (sheen on skin, cloth and hair; softer specular on skin)
  const byClass = new Map();
  for (const [gg, mi] of g.prims(mats)) { const cls = glb.materials[mi].name.split('_')[0]; if (!byClass.has(cls)) byClass.set(cls, []); byClass.get(cls).push([gg, mi]); }
  const jointNodes = BONE_NAMES.map((n) => glb.node({ name: n, translation: T[n] }));
  BONE_NAMES.forEach((n, i) => { if (PARENT[n] !== -1) (glb.nodes[jointNodes[BI[PARENT[n]]]].children ||= []).push(jointNodes[i]); });
  const ibm = new Float32Array(BONE_NAMES.length * 16);
  BONE_NAMES.forEach((n, i) => { const w = world[n]; ibm.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -w[0], -w[1], -w[2], 1], i * 16); });
  glb.skins.push({ name: 'Armature', inverseBindMatrices: glb.acc(ibm, 'MAT4', 5126, 0), joints: jointNodes, skeleton: jointNodes[0] });
  const meshNodes = [...byClass].map(([cls, prims]) => glb.node({ name: 'Body' + cls[0].toUpperCase() + cls.slice(1), mesh: glb.mesh(sp.name + '_' + cls, prims), skin: 0 }));
  const s = sp.scale ?? 1; // the root hip node scales the whole body
  glb.nodes[jointNodes[0]].translation = [0, T.Hips[1] * s, 0];
  glb.nodes[jointNodes[0]].scale = [s, s, s];
  glb.sceneNodes.push(...meshNodes, jointNodes[0]);
  for (const c of CLIPS) {
    const n = Math.max(2, Math.round(c.len * 30) + 1);
    const times = Array.from({ length: n }, (_, i) => (i / (n - 1)) * c.len);
    const poses = times.map((t) => c.pose(t)), chans = [];
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
      if (rot.some((v, i) => Math.abs(v - [0, 0, 0, 1][i % 4]) > 1e-6)) chans.push({ node, path: 'rotation', times, values: rot });
      if (name === 'Hips') chans.push({ node, path: 'translation', times, values: pos });
    }
    glb.anim(c.name, chans);
  }
  glb.write(join(OUT, sp.name + '.glb'));
  console.log(sp.name, 'tris', g.tris());
}

// fighters, enemies, bosses: proportions, outfit pieces and colours. Silhouette first: each type differs in bulk, head
// shape and outline (hood, mohawk, long coat, tails, top hat, hard hat, plates) before colour.
export const SPECS = [
  // fighters
  { name: 'rook', stubble: true, scale: 1.06, width: 1.3, chest: 1.08, sw: 0.23, arm: 1.3, leg: 1.12, head: 1.12, hand: 1.3, jaw: 1.25, hair: 'crop', gloves: true, belt: true, boots: true,
    top: 'tee', outer: true, outerKind: 'jacket', outerFab: 'leather', openFront: 0.45, sleeves: 'short', bottom: 'jeans',
    colors: { skin: 0xc98e60, shirt: 0xd8d2c0, pants: 0x2c4a80, shoes: 0x3b2618, hair: 0x2a1d14, accent: 0xb02a2a, outer: 0x4a2a1c, glove: 0xb8231f, belt: 0x201a14 } },
  { name: 'mika', scale: 0.97, width: 0.9, chest: 0.95, sw: 0.185, hw: 0.095, arm: 0.95, leg: 1.0, head: 1.14, hand: 1.25, jaw: 0.75, nose: 0.8, hair: 'ponytail', hairRoot: [0, 0.203, -0.1], hairB: [0, -0.13, -0.06], headband: true, wristband: true,
    top: 'tank', bottom: 'tight', pantsFab: 'cloth', lip: 0xb04a50,
    colors: { skin: 0xf0c4a0, shirt: 0xe89a20, pants: 0x262836, shoes: 0xe8e8e8, hair: 0x8a2d1a, accent: 0xd03040, band: 0xd03040 } },
  { name: 'jet', scale: 1.0, width: 1.05, sw: 0.205, arm: 1.12, leg: 1.05, head: 1.13, hand: 1.3, hair: 'mohawk', shades: true, wristband: true,
    top: 'tank', bottom: 'cargo', pantsFab: 'cloth', shirtFab: 'knit', kneepads: true,
    colors: { skin: 0x8a5838, shirt: 0x2f9a72, pants: 0x55583a, shoes: 0xf0d030, hair: 0xe0e0e8, accent: 0xf0d030, band: 0xf0d030, pad: 0x202020 } },
  // enemies
  { name: 'thug', stubble: true, frown: true, scale: 1.02, width: 1.2, belly: 0.5, sw: 0.215, arm: 1.15, leg: 1.1, head: 1.1, jaw: 1.2, hair: 'beanie', hatFab: 'knit', hood: true,
    top: 'hoodie', shirtFab: 'cloth', bottom: 'jeans', boots: false,
    colors: { skin: 0xc0906a, shirt: 0x5a2626, pants: 0x34384a, shoes: 0x1c1c1c, hair: 0x1a1410, accent: 0x2a2a2a, hat: 0x282828, band: 0xd8d8d8 } },
  { name: 'punk', war: 0xb02060, frown: true, scale: 0.97, width: 0.88, sw: 0.19, arm: 0.95, head: 1.12, hair: 'mohawk', mohawk: 1.5, pads: true, spikes: true, padMat: 'outer', wristband: true, boots: true, belt: true,
    top: 'none', outer: true, outerKind: 'vest', outerFab: 'leather', openFront: 0.55, bottom: 'tight', pantsFab: 'cloth',
    colors: { skin: 0xe0b890, shirt: 0x2a2a30, pants: 0x4a2a5a, shoes: 0x101010, hair: 0x30d0a0, accent: 0xa0a0b0, outer: 0x18181c, band: 0x101010 } },
  { name: 'runner', scale: 0.95, width: 0.82, sw: 0.185, hw: 0.095, arm: 0.92, leg: 0.95, legLen: 1.04, head: 1.1, hair: 'capback', jaw: 0.9,
    top: 'longsleeve', outer: true, outerKind: 'jacket', outerFab: 'cloth', openFront: 0.12, bottom: 'joggers', pantsFab: 'cloth',
    colors: { skin: 0xb07e58, shirt: 0xe8e8e8, pants: 0x2a2e3a, shoes: 0xd04030, hair: 0x201810, accent: 0xd04030, outer: 0x2a5aa8, hat: 0xd04030, band: 0xe8e8e8 } },
  { name: 'knifer', frown: true, scale: 1.0, width: 0.92, sw: 0.195, arm: 1.0, head: 1.1, jaw: 0.9, mask: true, hair: 'crop', boots: true,
    top: 'longsleeve', outer: true, outerKind: 'longcoat', outerFab: 'leather', openFront: 0.3, bottom: 'tight', pantsFab: 'cloth', gloves: false,
    colors: { skin: 0xc49470, shirt: 0x2a2a2a, pants: 0x1c2a1c, shoes: 0x101010, hair: 0x101010, accent: 0x505850, outer: 0x3a3a30, mask: 0x6a1a1a } },
  { name: 'bruiser', stubble: true, scar: true, frown: true, goatee: true, scale: 1.24, width: 1.6, chest: 1.15, belly: 0.9, sw: 0.27, hw: 0.12, arm: 1.5, leg: 1.25, head: 1.05, hand: 1.45, jaw: 1.4, neck: 1.35, hair: 'none', belt: true, boots: true, wristband: true,
    top: 'tank', shirtFab: 'cloth', bottom: 'cargo', pantsFab: 'cloth',
    colors: { skin: 0xb07a52, shirt: 0xd0ccb8, pants: 0x4a4430, shoes: 0x201810, hair: 0x281810, accent: 0x8a2a1a, belt: 0x141010, band: 0x101010 } },
  // bosses
  { name: 'foreman', stubble: true, frown: true, moustache: true, scale: 1.34, width: 1.6, belly: 1.0, chest: 1.1, sw: 0.25, hw: 0.11, arm: 1.5, leg: 1.25, head: 1.04, hand: 1.45, jaw: 1.35, neck: 1.3, hair: 'hardhat', boots: true, gloves: true, belt: true,
    top: 'longsleeve', shirtFab: 'cloth', outer: true, outerKind: 'hivis', outerFab: 'cloth', openFront: 0.3, bottom: 'cargo', pantsFab: 'denim', kneepads: true,
    colors: { skin: 0xa8704a, shirt: 0x3a4a60, pants: 0x30343a, shoes: 0x3a2410, hair: 0x2a2a2a, accent: 0xe0b020, outer: 0xf08a10, helmet: 0xf0c020, glove: 0x8a6a40, belt: 0x141010, pad: 0x202020 } },
  { name: 'matron', frown: true, scale: 1.2, width: 1.2, chest: 1.05, sw: 0.21, hw: 0.105, arm: 1.1, head: 1.1, jaw: 0.85, nose: 0.9, hair: 'long', pads: true, padMat: 'outer', boots: true, belt: true, lip: 0x901838,
    top: 'longsleeve', shirtFab: 'cloth', outer: true, outerKind: 'longcoat', outerFab: 'leather', openFront: 0.35, bottom: 'tight', pantsFab: 'leather', hairRoot: [0, 0.08, -0.12], hairB: [0, -0.16, -0.02],
    colors: { skin: 0xe8c0a0, shirt: 0x2a1a2a, pants: 0x1a1a24, shoes: 0x500a28, hair: 0xd0d0e0, accent: 0xe0d060, outer: 0x6a1a5a, belt: 0x201018 }, eyes: 0xe02060 },
  { name: 'ringmaster', scale: 1.14, width: 1.1, belly: 0.6, sw: 0.215, arm: 1.05, head: 1.12, jaw: 1.1, moustache: true, hair: 'tophat', belt: false, boots: true, pads: true, padMat: 'buckle',
    top: 'longsleeve', shirtFab: 'cloth', outer: true, outerKind: 'tailcoat', outerFab: 'cloth', openFront: 0.32, bottom: 'tight', pantsFab: 'cloth',
    colors: { skin: 0xe0b890, shirt: 0xf0ece0, pants: 0xe8e4d8, shoes: 0x0a0a0a, hair: 0x1a1a1a, accent: 0xe8c840, outer: 0xb01a28, hat: 0x141414, band: 0xb01a28 } },
  { name: 'colossus', scar: true, frown: true, scale: 1.62, width: 1.7, chest: 1.2, sw: 0.27, hw: 0.115, arm: 1.6, leg: 1.35, head: 1.0, hand: 1.5, jaw: 1.5, neck: 1.5, hair: 'helmet', pads: true, padMat: 'metal', armor: true, bracers: true, gloves: true, boots: true, belt: true,
    top: 'longsleeve', shirtFab: 'leather', bottom: 'cargo', pantsFab: 'leather',
    colors: { skin: 0x7a8a8a, shirt: 0x26303a, pants: 0x20282e, shoes: 0x0e1014, hair: 0x6a7a8a, accent: 0x40c0d0, metal: 0x5a6a78, helmet: 0x4a5866, glove: 0x1a2028, belt: 0x101418 }, eyes: 0x40e0ff },
];
const only = process.argv.slice(2);
for (const s of SPECS) if (!only.length || only.includes(s.name)) emit(s);
