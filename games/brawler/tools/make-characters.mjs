// Generates every fighter and enemy as a skinned glb (one shared skeleton, the
// same clip library, vertex colours) into assets/models/. Run:
//   node tools/make-characters.mjs
// Facing is +Z in the model. Rotation values below are offsets from the rest
// pose (arms and legs hang along -Y): -x swings a limb forward, +z on the left
// side (+x) lifts it out sideways.
import { mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb } from './gltf-writer.mjs';
import { SkinMesh } from './charmesh.mjs';
import { face } from './make-textures.mjs';
import { writeSet } from './texlib.mjs';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'models');
mkdirSync(OUT, { recursive: true });

// ── the skeleton (rest pose: translation relative to the parent) ─────────
const BONES = [
  ['Hips', -1, [0, 0.97, 0]],
  ['Spine', 0, [0, 0.12, 0]],
  ['Chest', 1, [0, 0.2, 0]],
  ['Neck', 2, [0, 0.24, 0]],
  ['Head', 3, [0, 0.09, 0]],
  ['UpperArmL', 2, [0.21, 0.17, 0]],
  ['LowerArmL', 5, [0, -0.28, 0]],
  ['HandL', 6, [0, -0.26, 0]],
  ['UpperArmR', 2, [-0.21, 0.17, 0]],
  ['LowerArmR', 8, [0, -0.28, 0]],
  ['HandR', 9, [0, -0.26, 0]],
  ['UpperLegL', 0, [0.1, -0.06, 0]],
  ['LowerLegL', 11, [0, -0.44, 0]],
  ['FootL', 12, [0, -0.43, 0]],
  ['UpperLegR', 0, [-0.1, -0.06, 0]],
  ['LowerLegR', 14, [0, -0.44, 0]],
  ['FootR', 15, [0, -0.43, 0]],
];
const BI = Object.fromEntries(BONES.map((b, i) => [b[0], i]));
const world = [];
BONES.forEach((b, i) => { const p = b[1] < 0 ? [0, 0, 0] : world[b[1]]; world[i] = [p[0] + b[2][0], p[1] + b[2][1], p[2] + b[2][2]]; });

// ── poses and clips ─────────────────────────────────────────────────────
const GUARD = {
  Hips: { p: [0, -0.06, 0], r: [0, 0, 0] }, Spine: { r: [0.08, 0, 0] }, Chest: { r: [0.04, 0, 0] }, Head: { r: [-0.08, 0, 0] },
  UpperArmL: { r: [-0.75, 0, 0.2] }, LowerArmL: { r: [-1.8, 0, 0] }, UpperArmR: { r: [-0.95, 0, -0.1] }, LowerArmR: { r: [-1.9, 0, 0] },
  UpperLegL: { r: [-0.3, 0, 0.12] }, LowerLegL: { r: [0.4, 0, 0] }, FootL: { r: [-0.1, 0, 0] },
  UpperLegR: { r: [0.3, 0, -0.1] }, LowerLegR: { r: [0.45, 0, 0] }, FootR: { r: [-0.15, 0, 0] },
};
const REST = {};
const merge = (...ps) => {
  const out = {};
  for (const p of ps) for (const [k, v] of Object.entries(p)) out[k] = { ...(out[k] || {}), ...v };
  return out;
};
const lerp = (a, b, t) => a + (b - a) * t;
const lerpPose = (a, b, t) => {
  const out = {};
  for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) {
    const A = a[k] || {}, B = b[k] || {};
    const o = {};
    for (const f of ['r', 'p']) {
      if (!A[f] && !B[f]) continue;
      const x = A[f] || [0, 0, 0], y = B[f] || [0, 0, 0];
      o[f] = [lerp(x[0], y[0], t), lerp(x[1], y[1], t), lerp(x[2], y[2], t)];
    }
    out[k] = o;
  }
  return out;
};
// keyframes [[t, pose], ...] eased between
const keyed = (keys) => (t) => {
  if (t <= keys[0][0]) return keys[0][1];
  for (let i = 1; i < keys.length; i++) {
    if (t <= keys[i][0]) {
      const [t0, a] = keys[i - 1], [t1, b] = keys[i];
      const u = (t - t0) / (t1 - t0);
      return lerpPose(a, b, u * u * (3 - 2 * u));
    }
  }
  return keys[keys.length - 1][1];
};
const G = (o) => merge(GUARD, o);
const S = Math.sin, C = Math.cos, PI = Math.PI;

const walkPose = (ph, amp, run) => {
  const s = S(ph), c = C(ph);
  const k = (v) => Math.max(0, v);
  return merge(run ? GUARD : REST, {
    Hips: { p: [0, -Math.abs(s) * 0.035 * (run ? 1.6 : 1) - (run ? 0.05 : 0), 0], r: [run ? -0.18 : -0.04, s * 0.08 * amp, 0] },
    Spine: { r: [run ? -0.1 : 0, -s * 0.12 * amp, 0] },
    Chest: { r: [0, -s * 0.12 * amp, 0] },
    UpperLegL: { r: [-s * 0.7 * amp, 0, 0.05] }, LowerLegL: { r: [0.15 + k(c) * 0.9 * amp, 0, 0] }, FootL: { r: [-0.1 * c, 0, 0] },
    UpperLegR: { r: [s * 0.7 * amp, 0, -0.05] }, LowerLegR: { r: [0.15 + k(-c) * 0.9 * amp, 0, 0] }, FootR: { r: [0.1 * c, 0, 0] },
    UpperArmL: { r: [s * (run ? 0.9 : 0.55) * amp, 0, 0.12], }, LowerArmL: { r: [-(run ? 1.5 : 0.3) - k(-s) * 0.3, 0, 0] },
    UpperArmR: { r: [-s * (run ? 0.9 : 0.55) * amp, 0, -0.12] }, LowerArmR: { r: [-(run ? 1.5 : 0.3) - k(s) * 0.3, 0, 0] },
    Head: { r: [run ? 0.15 : 0, 0, 0] },
  });
};

/** clip definitions: name, length, loop, pose(t seconds) */
const CLIPS = [];
const clip = (name, len, loop, pose) => CLIPS.push({ name, len, loop, pose });

clip('idle', 2, true, (t) => {
  const b = S((t / 2) * PI * 2);
  return G({ Hips: { p: [0, -0.06 + b * 0.01, 0] }, Chest: { r: [0.04 + b * 0.02, 0, 0] }, UpperArmL: { r: [-0.75 + b * 0.04, 0, 0.2] }, UpperArmR: { r: [-0.95 - b * 0.04, 0, -0.1] }, Head: { r: [-0.08, S((t / 2) * PI * 2 + 1) * 0.04, 0] } });
});
clip('walk', 1, true, (t) => walkPose((t / 1) * PI * 2, 1, false));
clip('run', 0.62, true, (t) => walkPose((t / 0.62) * PI * 2, 1.25, true));

// right jab then left cross, a front kick, an uppercut finisher
const WIND = (side) => G({
  Spine: { r: [0.05, side * 0.35, 0] }, Chest: { r: [0.04, side * 0.4, 0] },
  UpperArmR: { r: [-0.5, 0.3, -0.2] }, LowerArmR: { r: [-2.3, 0, 0] },
});
clip('punch1', 0.42, false, keyed([
  [0, GUARD], [0.1, G({ Spine: { r: [0.05, -0.3, 0] }, Chest: { r: [0.04, -0.35, 0] }, UpperArmR: { r: [-0.6, 0.2, -0.2] }, LowerArmR: { r: [-2.4, 0, 0] }, Hips: { p: [0, -0.08, -0.05] } })],
  [0.18, G({ Hips: { p: [0, -0.08, 0.1] }, Spine: { r: [0.05, 0.35, 0] }, Chest: { r: [0.04, 0.4, 0] }, UpperArmR: { r: [-1.55, 0, -0.05] }, LowerArmR: { r: [-0.1, 0, 0] }, UpperLegL: { r: [-0.5, 0, 0.12] }, LowerLegL: { r: [0.6, 0, 0] } })],
  [0.3, G({ Hips: { p: [0, -0.08, 0.1] }, Spine: { r: [0.05, 0.35, 0] }, Chest: { r: [0.04, 0.4, 0] }, UpperArmR: { r: [-1.55, 0, -0.05] }, LowerArmR: { r: [-0.1, 0, 0] } })],
  [0.42, GUARD]]));
clip('punch2', 0.45, false, keyed([
  [0, GUARD], [0.1, G({ Spine: { r: [0.05, 0.3, 0] }, Chest: { r: [0.04, 0.35, 0] }, UpperArmL: { r: [-0.6, -0.2, 0.2] }, LowerArmL: { r: [-2.4, 0, 0] }, Hips: { p: [0, -0.08, -0.05] } })],
  [0.2, G({ Hips: { p: [0, -0.08, 0.12] }, Spine: { r: [0.05, -0.4, 0] }, Chest: { r: [0.04, -0.45, 0] }, UpperArmL: { r: [-1.55, 0, 0.05] }, LowerArmL: { r: [-0.1, 0, 0] }, UpperLegR: { r: [-0.1, 0, -0.1] } })],
  [0.32, G({ Hips: { p: [0, -0.08, 0.12] }, Spine: { r: [0.05, -0.4, 0] }, Chest: { r: [0.04, -0.45, 0] }, UpperArmL: { r: [-1.55, 0, 0.05] }, LowerArmL: { r: [-0.1, 0, 0] } })],
  [0.45, GUARD]]));
clip('kick', 0.55, false, keyed([
  [0, GUARD], [0.14, G({ Hips: { p: [0, -0.12, -0.05] }, Chest: { r: [-0.2, 0, 0] }, UpperLegR: { r: [-1.0, 0, -0.1] }, LowerLegR: { r: [1.6, 0, 0] }, UpperLegL: { r: [0.2, 0, 0.1] }, LowerLegL: { r: [0.5, 0, 0] } })],
  [0.24, G({ Hips: { p: [0, -0.1, 0.05] }, Chest: { r: [-0.35, 0, 0] }, UpperLegR: { r: [-1.65, 0, -0.1] }, LowerLegR: { r: [0.15, 0, 0] }, FootR: { r: [-0.4, 0, 0] }, UpperLegL: { r: [0.2, 0, 0.1] }, LowerLegL: { r: [0.5, 0, 0] } })],
  [0.38, G({ Hips: { p: [0, -0.1, 0.05] }, Chest: { r: [-0.35, 0, 0] }, UpperLegR: { r: [-1.65, 0, -0.1] }, LowerLegR: { r: [0.15, 0, 0] }, UpperLegL: { r: [0.2, 0, 0.1] }, LowerLegL: { r: [0.5, 0, 0] } })],
  [0.55, GUARD]]));
clip('finisher', 0.7, false, keyed([
  [0, GUARD], [0.2, G({ Hips: { p: [0, -0.28, 0] }, Spine: { r: [0.3, -0.2, 0] }, UpperArmR: { r: [0.2, 0, -0.1] }, LowerArmR: { r: [-0.6, 0, 0] }, UpperLegL: { r: [-0.9, 0, 0.1] }, LowerLegL: { r: [1.2, 0, 0] }, UpperLegR: { r: [0.6, 0, -0.1] }, LowerLegR: { r: [0.8, 0, 0] } })],
  [0.34, G({ Hips: { p: [0, 0.08, 0.08] }, Spine: { r: [-0.25, 0.2, 0] }, Chest: { r: [-0.3, 0.2, 0] }, UpperArmR: { r: [-2.9, 0, -0.1] }, LowerArmR: { r: [-0.2, 0, 0] }, UpperLegL: { r: [-0.1, 0, 0.1] }, LowerLegL: { r: [0.2, 0, 0] }, UpperLegR: { r: [0.1, 0, -0.1] }, LowerLegR: { r: [0.9, 0, 0] } })],
  [0.5, G({ Hips: { p: [0, 0.08, 0.08] }, Spine: { r: [-0.25, 0.2, 0] }, Chest: { r: [-0.3, 0.2, 0] }, UpperArmR: { r: [-2.9, 0, -0.1] }, LowerArmR: { r: [-0.2, 0, 0] } })],
  [0.7, GUARD]]));
clip('jumpkick', 0.8, false, keyed([
  [0, G({ Hips: { p: [0, -0.15, 0] }, UpperLegL: { r: [-0.6, 0, 0.1] }, LowerLegL: { r: [1.0, 0, 0] }, UpperLegR: { r: [-0.6, 0, -0.1] }, LowerLegR: { r: [1.0, 0, 0] } })],
  [0.2, G({ Hips: { p: [0, 0, 0] }, Chest: { r: [-0.45, 0, 0] }, UpperLegR: { r: [-1.6, 0, -0.1] }, LowerLegR: { r: [0.1, 0, 0] }, UpperLegL: { r: [0.3, 0, 0.1] }, LowerLegL: { r: [1.0, 0, 0] }, UpperArmL: { r: [-0.3, 0, 0.9] }, UpperArmR: { r: [-0.3, 0, -0.9] }, LowerArmL: { r: [-0.4, 0, 0] }, LowerArmR: { r: [-0.4, 0, 0] } })],
  [0.55, G({ Hips: { p: [0, 0, 0] }, Chest: { r: [-0.45, 0, 0] }, UpperLegR: { r: [-1.6, 0, -0.1] }, LowerLegR: { r: [0.1, 0, 0] }, UpperLegL: { r: [0.3, 0, 0.1] }, LowerLegL: { r: [1.0, 0, 0] }, UpperArmL: { r: [-0.3, 0, 0.9] }, UpperArmR: { r: [-0.3, 0, -0.9] } })],
  [0.8, GUARD]]));
clip('jump', 0.8, false, keyed([
  [0, G({ Hips: { p: [0, -0.2, 0] }, UpperLegL: { r: [-0.8, 0, 0.1] }, LowerLegL: { r: [1.4, 0, 0] }, UpperLegR: { r: [-0.8, 0, -0.1] }, LowerLegR: { r: [1.4, 0, 0] } })],
  [0.25, G({ UpperArmL: { r: [-0.2, 0, 0.9] }, UpperArmR: { r: [-0.2, 0, -0.9] }, LowerArmL: { r: [-0.3, 0, 0] }, LowerArmR: { r: [-0.3, 0, 0] }, UpperLegL: { r: [-0.6, 0, 0.1] }, LowerLegL: { r: [0.9, 0, 0] }, UpperLegR: { r: [0.2, 0, -0.1] }, LowerLegR: { r: [0.9, 0, 0] } })],
  [0.8, G({ Hips: { p: [0, -0.2, 0] }, UpperLegL: { r: [-0.6, 0, 0.1] }, LowerLegL: { r: [1.1, 0, 0] }, UpperLegR: { r: [-0.4, 0, -0.1] }, LowerLegR: { r: [1.0, 0, 0] } })]]));

// grabs: the grabber holds the victim in front; throw swings over; the victim hangs
clip('grab', 0.9, true, (t) => {
  const b = S((t / 0.9) * PI * 2) * 0.04;
  return G({ Hips: { p: [0, -0.1, 0] }, Spine: { r: [0.2, 0, 0] }, UpperArmL: { r: [-1.35 + b, 0, 0.12] }, LowerArmL: { r: [-0.9, 0, 0] }, UpperArmR: { r: [-1.35 - b, 0, -0.12] }, LowerArmR: { r: [-0.9, 0, 0] } });
});
clip('knee', 0.5, false, keyed([
  [0, G({ Hips: { p: [0, -0.1, 0] }, Spine: { r: [0.2, 0, 0] }, UpperArmL: { r: [-1.35, 0, 0.12] }, LowerArmL: { r: [-0.9, 0, 0] }, UpperArmR: { r: [-1.35, 0, -0.12] }, LowerArmR: { r: [-0.9, 0, 0] } })],
  [0.15, G({ Hips: { p: [0, -0.1, 0.05] }, Spine: { r: [0.35, 0, 0] }, UpperArmL: { r: [-1.35, 0, 0.12] }, LowerArmL: { r: [-0.9, 0, 0] }, UpperArmR: { r: [-1.35, 0, -0.12] }, LowerArmR: { r: [-0.9, 0, 0] }, UpperLegR: { r: [-1.6, 0, -0.1] }, LowerLegR: { r: [1.7, 0, 0] } })],
  [0.5, G({ Hips: { p: [0, -0.1, 0] }, Spine: { r: [0.2, 0, 0] }, UpperArmL: { r: [-1.35, 0, 0.12] }, LowerArmL: { r: [-0.9, 0, 0] }, UpperArmR: { r: [-1.35, 0, -0.12] }, LowerArmR: { r: [-0.9, 0, 0] } })]]));
clip('throw', 0.7, false, keyed([
  [0, G({ Hips: { p: [0, -0.1, 0] }, Spine: { r: [0.2, 0, 0] }, UpperArmL: { r: [-1.35, 0, 0.12] }, LowerArmL: { r: [-0.9, 0, 0] }, UpperArmR: { r: [-1.35, 0, -0.12] }, LowerArmR: { r: [-0.9, 0, 0] } })],
  [0.25, G({ Hips: { p: [0, -0.05, -0.05] }, Spine: { r: [-0.35, 0, 0] }, Chest: { r: [-0.2, 0, 0] }, UpperArmL: { r: [-2.7, 0, 0.12] }, LowerArmL: { r: [-0.3, 0, 0] }, UpperArmR: { r: [-2.7, 0, -0.12] }, LowerArmR: { r: [-0.3, 0, 0] } })],
  [0.42, G({ Hips: { p: [0, -0.15, 0.1] }, Spine: { r: [0.6, 0, 0] }, Chest: { r: [0.2, 0, 0] }, UpperArmL: { r: [-0.6, 0, 0.12] }, LowerArmL: { r: [-0.3, 0, 0] }, UpperArmR: { r: [-0.6, 0, -0.12] }, LowerArmR: { r: [-0.3, 0, 0] } })],
  [0.7, GUARD]]));
clip('grabbed', 0.8, true, (t) => {
  const s = S((t / 0.8) * PI * 2);
  return merge(REST, { Hips: { p: [0, -0.1, 0] }, Spine: { r: [0.35, 0, 0] }, Head: { r: [0.3 + s * 0.1, 0, 0] }, UpperArmL: { r: [-0.3 + s * 0.3, 0, 0.5] }, UpperArmR: { r: [-0.3 - s * 0.3, 0, -0.5] }, LowerArmL: { r: [-0.5, 0, 0] }, LowerArmR: { r: [-0.5, 0, 0] }, UpperLegL: { r: [-0.5 + s * 0.4, 0, 0.1] }, UpperLegR: { r: [-0.5 - s * 0.4, 0, -0.1] }, LowerLegL: { r: [0.6, 0, 0] }, LowerLegR: { r: [0.6, 0, 0] } });
});
clip('thrown', 0.7, true, (t) => {
  const s = S((t / 0.7) * PI * 2);
  return merge(REST, { Hips: { r: [-0.9, 0, 0] }, UpperArmL: { r: [-2.4, 0, 0.6 + s * 0.2] }, UpperArmR: { r: [-2.4, 0, -0.6 - s * 0.2] }, UpperLegL: { r: [-0.6, 0, 0.2] }, UpperLegR: { r: [-0.3, 0, -0.2] }, LowerLegL: { r: [0.8, 0, 0] }, LowerLegR: { r: [0.6, 0, 0] } });
});

// hurt, down, up, dead
clip('hit', 0.32, false, keyed([
  [0, GUARD], [0.07, G({ Hips: { p: [0, -0.08, -0.08] }, Spine: { r: [-0.25, 0, 0] }, Chest: { r: [-0.2, 0, 0] }, Head: { r: [-0.5, 0, 0] }, UpperArmL: { r: [-0.4, 0, 0.5] }, UpperArmR: { r: [-0.4, 0, -0.5] }, LowerArmL: { r: [-0.9, 0, 0] }, LowerArmR: { r: [-0.9, 0, 0] } })],
  [0.32, GUARD]]));
clip('hit_hard', 0.42, false, keyed([
  [0, GUARD], [0.09, G({ Hips: { p: [0, -0.1, -0.15], r: [-0.25, 0, 0] }, Spine: { r: [-0.35, 0.3, 0] }, Chest: { r: [-0.3, 0.3, 0] }, Head: { r: [-0.6, 0.4, 0] }, UpperArmL: { r: [-0.2, 0, 0.9] }, UpperArmR: { r: [-0.2, 0, -0.9] }, LowerArmL: { r: [-0.5, 0, 0] }, LowerArmR: { r: [-0.5, 0, 0] } })],
  [0.42, GUARD]]));
const LYING = merge(REST, {
  Hips: { p: [0, -0.82, 0], r: [-1.5708, 0, 0] }, Head: { r: [0.2, 0.3, 0] },
  UpperArmL: { r: [-0.4, 0, 0.6] }, UpperArmR: { r: [-0.3, 0, -0.7] }, LowerArmL: { r: [-0.3, 0, 0] }, LowerArmR: { r: [-0.5, 0, 0] },
  UpperLegL: { r: [0.1, 0, 0.2] }, UpperLegR: { r: [0.2, 0, -0.3] }, LowerLegL: { r: [0.2, 0, 0] }, LowerLegR: { r: [0.5, 0, 0] }, FootL: { r: [0.3, 0, 0] }, FootR: { r: [0.2, 0, 0] },
});
clip('knockdown', 0.85, false, keyed([
  [0, GUARD],
  [0.18, merge(GUARD, { Hips: { p: [0, 0.05, -0.2], r: [-0.5, 0, 0] }, Spine: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-0.2, 0, 1.0] }, UpperArmR: { r: [-0.2, 0, -1.0] }, UpperLegL: { r: [-0.2, 0, 0.2] }, UpperLegR: { r: [0.1, 0, -0.2] } })],
  [0.5, merge(LYING, { Hips: { p: [0, -0.7, -0.5], r: [-1.9, 0, 0] } })],
  [0.64, merge(LYING, { Hips: { p: [0, -0.74, -0.55], r: [-1.4, 0, 0] } })],
  [0.85, LYING]]));
clip('down', 1.5, true, (t) => merge(LYING, { Chest: { r: [S((t / 1.5) * PI * 2) * 0.03, 0, 0] } }));
clip('getup', 0.9, false, keyed([
  [0, LYING],
  [0.35, merge(REST, { Hips: { p: [0, -0.55, -0.1], r: [-0.8, 0, 0] }, Spine: { r: [0.5, 0, 0] }, UpperArmL: { r: [-0.9, 0, 0.3] }, UpperArmR: { r: [-0.9, 0, -0.3] }, UpperLegL: { r: [-1.6, 0, 0.2] }, LowerLegL: { r: [2.0, 0, 0] }, UpperLegR: { r: [-0.4, 0, -0.1] }, LowerLegR: { r: [1.0, 0, 0] } })],
  [0.9, GUARD]]));
clip('dead', 1, true, () => LYING);

// weapons: a two-handed overhead swing, a stab, pick-up, a taunt, boss moves
clip('swing', 0.62, false, keyed([
  [0, GUARD], [0.18, G({ Spine: { r: [-0.25, -0.3, 0] }, Chest: { r: [-0.2, -0.3, 0] }, UpperArmR: { r: [-2.9, 0, -0.1] }, LowerArmR: { r: [-0.6, 0, 0] }, UpperArmL: { r: [-2.6, 0, 0.1] }, LowerArmL: { r: [-0.8, 0, 0] } })],
  [0.3, G({ Hips: { p: [0, -0.15, 0.1] }, Spine: { r: [0.5, 0.2, 0] }, Chest: { r: [0.3, 0.2, 0] }, UpperArmR: { r: [-1.3, 0, -0.1] }, LowerArmR: { r: [-0.15, 0, 0] }, UpperArmL: { r: [-1.2, 0, 0.1] }, LowerArmL: { r: [-0.2, 0, 0] } })],
  [0.44, G({ Hips: { p: [0, -0.15, 0.1] }, Spine: { r: [0.5, 0.2, 0] }, Chest: { r: [0.3, 0.2, 0] }, UpperArmR: { r: [-1.3, 0, -0.1] }, LowerArmR: { r: [-0.15, 0, 0] }, UpperArmL: { r: [-1.2, 0, 0.1] }, LowerArmL: { r: [-0.2, 0, 0] } })],
  [0.62, GUARD]]));
clip('stab', 0.5, false, keyed([
  [0, GUARD], [0.12, G({ Spine: { r: [0.05, -0.3, 0] }, UpperArmR: { r: [-0.6, 0.2, -0.2] }, LowerArmR: { r: [-2.2, 0, 0] }, Hips: { p: [0, -0.1, -0.05] } })],
  [0.22, G({ Hips: { p: [0, -0.1, 0.15] }, Spine: { r: [0.1, 0.4, 0] }, UpperArmR: { r: [-1.5, 0, -0.05] }, LowerArmR: { r: [-0.05, 0, 0] }, UpperLegL: { r: [-0.7, 0, 0.12] }, LowerLegL: { r: [0.7, 0, 0] } })],
  [0.34, G({ Hips: { p: [0, -0.1, 0.15] }, Spine: { r: [0.1, 0.4, 0] }, UpperArmR: { r: [-1.5, 0, -0.05] }, LowerArmR: { r: [-0.05, 0, 0] }, UpperLegL: { r: [-0.7, 0, 0.12] }, LowerLegL: { r: [0.7, 0, 0] } })],
  [0.5, GUARD]]));
clip('pickup', 0.5, false, keyed([
  [0, GUARD], [0.22, G({ Hips: { p: [0, -0.4, 0.05] }, Spine: { r: [0.7, 0, 0] }, UpperArmR: { r: [-0.4, 0, -0.1] }, LowerArmR: { r: [-0.4, 0, 0] }, UpperLegL: { r: [-1.0, 0, 0.12] }, LowerLegL: { r: [1.6, 0, 0] }, UpperLegR: { r: [-0.6, 0, -0.1] }, LowerLegR: { r: [1.4, 0, 0] } })],
  [0.5, GUARD]]));
clip('taunt', 1.4, false, keyed([
  [0, GUARD], [0.3, G({ Chest: { r: [-0.25, 0, 0] }, Head: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-0.2, 0, 1.2] }, UpperArmR: { r: [-0.2, 0, -1.2] }, LowerArmL: { r: [-0.3, 0, 0] }, LowerArmR: { r: [-0.3, 0, 0] } })],
  [1.1, G({ Chest: { r: [-0.25, 0, 0] }, Head: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-0.2, 0, 1.2] }, UpperArmR: { r: [-0.2, 0, -1.2] }, LowerArmL: { r: [-0.3, 0, 0] }, LowerArmR: { r: [-0.3, 0, 0] } })],
  [1.4, GUARD]]));
clip('slam', 1.1, false, keyed([
  [0, GUARD], [0.5, G({ Hips: { p: [0, -0.1, -0.1] }, Spine: { r: [-0.5, 0, 0] }, Chest: { r: [-0.3, 0, 0] }, UpperArmL: { r: [-3.0, 0, 0.3] }, UpperArmR: { r: [-3.0, 0, -0.3] }, LowerArmL: { r: [-0.3, 0, 0] }, LowerArmR: { r: [-0.3, 0, 0] } })],
  [0.66, G({ Hips: { p: [0, -0.3, 0.2] }, Spine: { r: [0.9, 0, 0] }, Chest: { r: [0.4, 0, 0] }, UpperArmL: { r: [-0.4, 0, 0.3] }, UpperArmR: { r: [-0.4, 0, -0.3] }, LowerArmL: { r: [-0.2, 0, 0] }, LowerArmR: { r: [-0.2, 0, 0] } })],
  [0.9, G({ Hips: { p: [0, -0.3, 0.2] }, Spine: { r: [0.9, 0, 0] }, Chest: { r: [0.4, 0, 0] }, UpperArmL: { r: [-0.4, 0, 0.3] }, UpperArmR: { r: [-0.4, 0, -0.3] } })],
  [1.1, GUARD]]));
clip('spin', 0.7, false, keyed([
  [0, GUARD], [0.15, G({ Chest: { r: [0, 1.5, 0] }, UpperArmL: { r: [-0.2, 0, 1.45] }, UpperArmR: { r: [-0.2, 0, -1.45] }, LowerArmL: { r: [-0.1, 0, 0] }, LowerArmR: { r: [-0.1, 0, 0] } })],
  [0.55, G({ Chest: { r: [0, 1.5, 0] }, UpperArmL: { r: [-0.2, 0, 1.45] }, UpperArmR: { r: [-0.2, 0, -1.45] }, LowerArmL: { r: [-0.1, 0, 0] }, LowerArmR: { r: [-0.1, 0, 0] } })],
  [0.7, GUARD]]));
clip('block', 0.5, true, () => G({ Hips: { p: [0, -0.12, 0] }, UpperArmL: { r: [-1.2, 0.3, 0.1] }, LowerArmL: { r: [-2.3, 0, 0] }, UpperArmR: { r: [-1.2, -0.3, -0.1] }, LowerArmR: { r: [-2.3, 0, 0] }, Head: { r: [0.2, 0, 0] } }));

// ── pose to rotation tracks ─────────────────────────────────────────────
function quat(r) { // rest-relative euler, applied x then y then z
  const [x, y, z] = r;
  const cx = Math.cos(x / 2), sx = Math.sin(x / 2), cy = Math.cos(y / 2), sy = Math.sin(y / 2), cz = Math.cos(z / 2), sz = Math.sin(z / 2);
  // q = qz * qy * qx
  const qx = [sx, 0, 0, cx], qy = [0, sy, 0, cy], qz = [0, 0, sz, cz];
  const mul = (a, b) => [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0], a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
  return mul(qz, mul(qy, qx));
}

// ── the body ────────────────────────────────────────────────────────────
const hexRaw = (h) => [((h >> 16) & 255) / 255, ((h >> 8) & 255) / 255, (h & 255) / 255];
const hex = (h) => hexRaw(h).map((c) => Math.pow(c, 2.2));
const TEX = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'textures');
const ss = (a, b, x) => { const t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); };

/** weights for a limb segment: own bone, half to the child at the far joint and half to the parent at the near joint */
const limbW = (A, B, prev) => (t) => {
  const wB = t > 0.78 ? 0.5 * ss(0.78, 1, t) : 0, wP = prev != null && t < 0.22 ? 0.5 * (1 - ss(0, 0.22, t)) : 0;
  return [[A, 1 - wB - wP], [B, wB], [prev ?? A, wP]];
};

function build(spec) {
  const m = new SkinMesh();
  const W = spec.width ?? 1, AR = spec.arm ?? 1, LG = spec.leg ?? 1, bw = 0.75 + 0.25 * W;
  const B = (n) => world[BI[n]];
  const X = [1, 0, 0], Z = [0, 0, 1];
  // torso: lofted superellipse rings; weights follow the spine chain by height
  const torsoW = (k, P) => {
    const y = P[1];
    if (y < 0.97) return [[BI.Hips, 1]];
    if (y < 1.09) { const t = ss(0.97, 1.09, y); return [[BI.Hips, 1 - t], [BI.Spine, t]]; }
    if (y < 1.29) { const t = ss(1.09, 1.29, y); return [[BI.Spine, 1 - t], [BI.Chest, t]]; }
    return [[BI.Chest, 1]];
  };
  const PROF = [[0.80, 0.03, 0.03, 0], [0.84, 0.12, 0.085, 0], [0.9, 0.165, 0.115, 0], [0.97, 0.178, 0.122, 0], [1.04, 0.165, 0.112, 0], [1.12, 0.15, 0.1, 0],
    [1.2, 0.158, 0.106, 0.004], [1.29, 0.19, 0.122, 0.01], [1.38, 0.212, 0.126, 0.0], [1.45, 0.2, 0.105, -0.005], [1.49, 0.12, 0.085, -0.008], [1.51, 0.06, 0.06, -0.01]];
  const ring = (p, k = 1, kz = 1) => ({ c: [0, p[0], p[3]], U: X, V: Z, ru: p[1] * bw * k, rv: p[2] * (0.85 + 0.15 * W) * kz, e: 2.5 });
  const rowsBetween = (y0, y1, k = 1) => PROF.filter((p) => p[0] >= y0 && p[0] <= y1).map((p) => ring(p, k, k));
  m.loft('pants', rowsBetween(0.8, 1.05), { n: 24, bone: BI.Hips, weights: torsoW, tile: 0.5 });
  const hem = rowsBetween(1.04, 1.51, 1.0); hem[0] = ring(PROF[4], 1.045, 1.06); // the hem flares a little
  m.loft('shirt', hem, { n: 24, weights: torsoW, tile: 0.5 });
  if (spec.belt) m.loft('belt', [ring([1.0, 0.18, 0.125, 0], 1.0), ring([1.06, 0.17, 0.115, 0], 1.0)].map((r) => ({ ...r, ru: r.ru * 1.04, rv: r.rv * 1.06 })), { n: 24, bone: BI.Hips, tile: 0.3 });
  if (spec.belt) m.ellipsoid('buckle', [0, 1.03, 0.128 * (0.85 + 0.15 * W)], 0.03, 0.025, 0.012, { bone: BI.Hips, steps: 4, n: 8 });
  if (spec.vest) m.loft('vest', PROF.filter((p) => p[0] >= 1.1 && p[0] <= 1.47).map((p) => ring(p, 1.04, 1.07)), { n: 24, weights: torsoW, tile: 0.5 });
  // neck and head
  const nk = B('Neck'), hd = B('Head'), hc = [0, hd[1] + 0.12, 0];
  m.capsule('skin', [0, nk[1] - 0.03, -0.005], [0, hd[1] + 0.035, 0.0], 0.056, 0.05, { n: 12, bone: BI.Neck, caps: 3 });
  const headRings = [];
  for (let k = 0; k <= 12; k++) {
    const t = -1 + (2 * k) / 12, s = Math.sqrt(Math.max(0, 1 - t * t)), jaw = 0.8 + 0.2 * ss(-0.9, 0.2, t);
    headRings.push({ c: [0, hc[1] + 0.136 * t, 0.004 + (t < -0.3 ? 0.014 : 0)], U: X, V: Z, ru: Math.max(0.003, 0.113 * s * jaw), rv: Math.max(0.003, 0.125 * s * (t < -0.2 ? 0.92 : 1)), e: 2.35 });
  }
  m.loft('face', headRings, { n: 28, bone: BI.Head, uvFn: (p) => (p[2] < 0 ? [0.03, 0.03] : [0.5 + p[0] / 0.27, 0.5 - (p[1] - hc[1]) / 0.31]) });
  const hb = { bone: BI.Head, steps: 6, n: 10 };
  m.ellipsoid('skin', [0, hc[1] - 0.012, 0.125], 0.017, 0.034, 0.028, hb); // nose
  m.ellipsoid('skin', [0, hc[1] - 0.04, 0.133], 0.022, 0.014, 0.02, hb);
  for (const sd of [1, -1]) m.ellipsoid('skin', [sd * 0.115, hc[1] - 0.005, -0.005], 0.012, 0.032, 0.022, hb); // ears
  m.ellipsoid('skin', [0, hc[1] + 0.036, 0.112], 0.088, 0.011, 0.02, { ...hb, e: 2 }); // brow ridge
  if (spec.shades) m.ellipsoid('shades', [0, hc[1] + 0.012, 0.118], 0.1, 0.024, 0.02, { bone: BI.Head, steps: 6, n: 14 });
  const hr = (k) => ({ n: 24, bone: BI.Head, ...k });
  const shell = (t0, k = 1.05) => { const r = []; for (let i = 0; i <= 8; i++) { const t = t0 + (1 - t0) * (i / 8), s = Math.sqrt(Math.max(0, 1 - t * t)); r.push({ c: [0, hc[1] + 0.136 * t, -0.006], U: X, V: Z, ru: Math.max(0.003, 0.113 * s * k), rv: Math.max(0.003, 0.125 * s * k), e: 2.35 }); } return r; };
  if (spec.hair === 'cap') { m.loft('hair', shell(0.42, 1.045), hr({})); m.ellipsoid('hair', [0, hc[1] + 0.0, -0.06], 0.108, 0.1, 0.07, { bone: BI.Head, steps: 6, n: 14 }); }
  else if (spec.hair === 'mohawk') m.capsule('hair', [0, hc[1] + 0.12, 0.085], [0, hc[1] + 0.03, -0.14], 0.032, 0.04, { bone: BI.Head, ratio: 0.55, n: 10, caps: 3, e: 2 });
  else if (spec.hair === 'long') { m.loft('hair', shell(0.3, 1.05), hr({})); m.capsule('hair', [0, hc[1] + 0.02, -0.085], [0, hc[1] - 0.3, -0.105], 0.05, 0.045, { bone: BI.Head, ratio: 2.1, n: 14, caps: 3, weights: (t) => [[BI.Head, 1 - 0.45 * t], [BI.Neck, 0.45 * t]] }); }
  else if (spec.hair === 'hat') {
    m.loft('hat', [0.06, 0.1, 0.17, 0.21].map((y, i) => ({ c: [0, hc[1] + y, -0.004], U: X, V: Z, ru: i === 3 ? 0.1 : 0.128, rv: i === 3 ? 0.11 : 0.138, e: 3 })), hr({}));
    m.ellipsoid('hat', [0, hc[1] + 0.068, 0.05], 0.17, 0.011, 0.2, { bone: BI.Head, steps: 4, n: 20, e: 2.6 });
  } else if (spec.hair === 'helmet') { m.loft('helmet', shell(0.1, 1.2), hr({ n: 24 })); m.ellipsoid('helmet', [0, hc[1] + 0.06, 0.12], 0.1, 0.012, 0.05, { bone: BI.Head, steps: 4, n: 10 }); }
  if (spec.headband) m.loft('band', [-0.016, 0.016].map((y) => ({ c: [0, hc[1] + 0.075 + y, 0], U: X, V: Z, ru: 0.119, rv: 0.131, e: 2.35 })), hr({ n: 28, tile: 0.3 }));
  if (spec.mask) { const r = []; for (let i = 0; i <= 5; i++) { const t = -1 + 0.9 * (i / 5), s = Math.sqrt(1 - t * t), jaw = 0.8 + 0.2 * ss(-0.9, 0.2, t); r.push({ c: [0, hc[1] + 0.136 * t, 0.008], U: X, V: Z, ru: Math.max(0.003, 0.113 * s * jaw * 1.07), rv: Math.max(0.003, 0.125 * s * 1.07), e: 2.35 }); } m.loft('mask', r, hr({})); }
  // pads
  if (spec.pads) for (const sd of [1, -1]) m.ellipsoid('pad', [sd * (0.205 * bw + 0.07), 1.47, 0], 0.095, 0.055, 0.1, { bone: BI.Chest, steps: 6, n: 14 });
  // arms
  const armR = 0.05 * AR * (0.8 + 0.2 * W);
  for (const sd of ['L', 'R']) {
    const u = B('UpperArm' + sd), l = B('LowerArm' + sd), h = B('Hand' + sd), U = BI['UpperArm' + sd], Lw = BI['LowerArm' + sd];
    const sleeved = !!spec.sleeves;
    m.ellipsoid(spec.vest || sleeved || true ? 'shirt' : 'skin', [u[0], u[1] - 0.01, 0], armR * 1.35, armR * 1.3, armR * 1.3, { weights: () => [[BI.Chest, 0.5], [U, 0.5]], steps: 6, n: 12 });
    m.capsule(sleeved ? 'shirt' : 'skin', u, l, armR * 1.12, armR * 0.92, { weights: limbW(U, Lw, BI.Chest), n: 14, caps: 3 });
    if (!sleeved) { const e = [u[0], u[1] + (l[1] - u[1]) * 0.5, u[2]]; m.capsule('shirt', u, e, armR * 1.22, armR * 1.17, { weights: limbW(U, Lw, BI.Chest), n: 14, caps: 3 }); }
    m.capsule(sleeved ? 'shirt' : 'skin', l, [h[0], h[1] + 0.03, h[2]], armR * 0.92, armR * 0.72, { weights: limbW(Lw, BI['Hand' + sd], U), n: 14, caps: 3 });
    const g = spec.gloves ? 1.3 : 1;
    m.ellipsoid(spec.gloves ? 'glove' : 'skin', [h[0], h[1] - 0.035, h[2] + 0.01], armR * 1.25 * g, armR * 1.45 * g, armR * 1.3 * g, { bone: BI['Hand' + sd], steps: 6, n: 12 });
    if (spec.gloves) m.capsule('glove', [h[0], h[1] + 0.045, h[2]], [h[0], h[1] - 0.005, h[2]], armR * 0.95, armR * 1.15, { bone: BI['Hand' + sd], n: 12, caps: 2 });
  }
  // legs
  const legR = 0.078 * LG * (0.8 + 0.2 * W);
  for (const sd of ['L', 'R']) {
    const u = B('UpperLeg' + sd), l = B('LowerLeg' + sd), f = B('Foot' + sd), Ub = BI['UpperLeg' + sd], Lb = BI['LowerLeg' + sd];
    m.capsule('pants', [u[0], u[1] + 0.02, u[2]], l, legR, legR * 0.74, { weights: limbW(Ub, Lb, BI.Hips), n: 16, caps: 3 });
    m.capsule(spec.shorts ? 'skin' : 'pants', l, [f[0], f[1] + 0.07, f[2]], legR * 0.74, legR * 0.56, { weights: limbW(Lb, BI['Foot' + sd], Ub), n: 16, caps: 3 });
    // shoe: a loft along z, heel to toe
    const sh = [[-0.1, 0.03, 0.022, 0], [-0.085, 0.05, 0.05, 0], [-0.03, 0.056, 0.06, 0], [0.05, 0.058, 0.044, 0], [0.13, 0.056, 0.035, 0], [0.19, 0.04, 0.026, 0], [0.222, 0.016, 0.012, 0]];
    const wsh = 0.8 + 0.2 * W;
    m.loft('shoe', sh.map(([z, w, hh]) => ({ c: [f[0], hh, f[2] + z], U: X, V: [0, 1, 0], ru: w * wsh * (1 + 0.06 * (LG - 1)), rv: hh, e: 2.4 })), { n: 16, bone: BI['Foot' + sd], tile: 0.3 });
    m.loft('sole', [[-0.098, 0.052], [0.2, 0.05]].map(([z, w]) => ({ c: [f[0], 0.012, f[2] + z], U: X, V: [0, 1, 0], ru: w * wsh * 1.05, rv: 0.012, e: 3 })), { n: 12, bone: BI['Foot' + sd], tile: 0.3 });
  }
  return m;
}

/** the face texture each character wears, and the materials a character is made of */
function writeFace(spec) {
  const skin = hexRaw(spec.colors.skin);
  const hair = hexRaw(spec.colors.hair);
  writeSet(TEX, 'face_' + spec.name, face({ name: spec.name, skin, eyes: spec.eyes ? hexRaw(spec.eyes) : [0.28, 0.17, 0.09], brow: hair.map((c) => c * 0.5), stubble: spec.stubble, scar: spec.scar, frown: spec.frown, war: spec.war ? hexRaw(spec.war) : null, seed: spec.name.length }), 1.5);
}
function materials(glb, spec) {
  const c = Object.fromEntries(Object.entries(spec.colors).map(([k, v]) => [k, hex(v)]));
  const d = TEX, P = (n, set, o) => [n, glb.pbr(n, d, set, o)];
  return Object.fromEntries([
    P('skin', 'fab_skin', { tint: c.skin, rough: 1, normal: 0.8 }),
    P('face', 'face_' + spec.name, { clamp: true, rough: 1, normal: 1 }),
    P('shirt', spec.knit ? 'fab_knit' : 'fab_cloth', { tint: c.shirt, rough: 1 }),
    P('pants', 'fab_denim', { tint: c.pants, rough: 1 }),
    P('shoe', 'fab_leather', { tint: c.shoes, rough: 0.9 }), P('sole', 'fab_leather', { tint: [0.04, 0.04, 0.04], rough: 0.9 }),
    P('belt', 'fab_leather', { tint: c.belt || c.accent, rough: 1 }), P('buckle', 'paint_metal', { tint: [0.9, 0.75, 0.3], rough: 0.6 }),
    P('vest', 'fab_leather', { tint: c.accent, rough: 1 }), P('glove', 'fab_leather', { tint: c.accent, rough: 0.9 }),
    P('hair', 'fab_hair', { tint: c.hair, rough: 1 }), P('hat', 'fab_cloth', { tint: c.hair, rough: 1 }), P('band', 'fab_cloth', { tint: c.accent, rough: 1 }),
    P('mask', 'fab_cloth', { tint: c.accent, rough: 1 }), P('pad', 'fab_leather', { tint: c.accent, rough: 0.8 }),
    P('helmet', 'paint_metal', { tint: c.hair, rough: 0.6 }), P('shades', 'fab_leather', { tint: [0.02, 0.02, 0.025], rough: 0.25, metal: 0 }),
  ]);
}

function skeletonNodes(glb) {
  const idx = BONES.map((b, i) => glb.node({ name: b[0], translation: b[2] }));
  BONES.forEach((b, i) => { if (b[1] >= 0) (glb.nodes[idx[b[1]]].children ||= []).push(idx[i]); });
  return idx;
}

function emit(spec) {
  const glb = new Glb();
  writeFace(spec);
  const mats = materials(glb, spec);
  const g = build(spec);
  const mesh = glb.mesh(spec.name, g.prims(mats));
  const jointNodes = skeletonNodes(glb);
  // inverse bind: translate(-world)
  const ibm = new Float32Array(BONES.length * 16);
  world.forEach((w, i) => { ibm.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -w[0], -w[1], -w[2], 1], i * 16); });
  const ibmAcc = glb.acc(ibm, 'MAT4', 5126, 0);
  glb.skins.push({ name: 'Armature', inverseBindMatrices: ibmAcc, joints: jointNodes, skeleton: jointNodes[0] });
  const meshNode = glb.node({ name: 'Body', mesh, skin: 0 });
  // the root hip node scales the whole body (height and build)
  const s = spec.scale ?? 1;
  glb.nodes[jointNodes[0]].translation = [0, BONES[0][2][1] * s, 0];
  glb.nodes[jointNodes[0]].scale = [s, s, s];
  glb.sceneNodes.push(meshNode, jointNodes[0]);
  // the skin's rest has scale s on the hips: the inverse binds already carry unit scale, so rest-scale the mesh by wrapping it
  // (the inverse bind of Hips keeps the bind pose at scale 1; multiply the model by s through the mesh node instead)
  for (const c of CLIPS) {
    const n = Math.max(2, Math.round(c.len * 30) + 1);
    const times = Array.from({ length: n }, (_, i) => (i / (n - 1)) * c.len);
    const chans = [];
    const poses = times.map((t) => c.pose(t));
    for (const b of BONES) {
      const rot = [], pos = [];
      poses.forEach((p, i) => {
        let r = (p[b[0]] && p[b[0]].r) || [0, 0, 0];
        if (c.loop && i === n - 1) r = (poses[0][b[0]] && poses[0][b[0]].r) || [0, 0, 0];
        rot.push(...quat(r));
        let o = (p[b[0]] && p[b[0]].p) || [0, 0, 0];
        if (c.loop && i === n - 1) o = (poses[0][b[0]] && poses[0][b[0]].p) || [0, 0, 0];
        pos.push(b[2][0] * (b[0] === 'Hips' ? 1 : 1) + o[0], (b[0] === 'Hips' ? b[2][1] * s : b[2][1]) + o[1] * (b[0] === 'Hips' ? s : 1), b[2][2] + o[2]);
      });
      const node = jointNodes[BI[b[0]]];
      if (rot.some((v, i) => Math.abs(v - [0, 0, 0, 1][i % 4]) > 1e-6)) chans.push({ node, path: 'rotation', times, values: rot });
      if (b[0] === 'Hips') chans.push({ node, path: 'translation', times, values: pos });
    }
    glb.anim(c.name, chans);
  }
  glb.write(join(OUT, spec.name + '.glb'));
  console.log(spec.name, 'ok');
}

// fighters and enemies
const SPECS = [
  { name: 'rook', stubble: true, scale: 1.08, width: 1.2, arm: 1.25, leg: 1.1, hair: 'cap', gloves: true, belt: true, sleeves: false, colors: { skin: 0xd9a066, shirt: 0xc8c2b0, pants: 0x2f4f8a, shoes: 0x3b2a20, hair: 0x2a1d14, accent: 0xb02a2a, belt: 0x201a14 } },
  { name: 'mika', scale: 0.98, width: 0.9, hair: 'long', headband: true, sleeves: false, colors: { skin: 0xf0c8a0, shirt: 0xe6a02a, pants: 0x2a2a38, shoes: 0xe8e8e8, hair: 0x8a2d1a, accent: 0xd03040 } },
  { name: 'jet', scale: 1.0, width: 1.0, hair: 'mohawk', shades: true, shorts: true, colors: { skin: 0x8c5a3c, shirt: 0x3aa07a, pants: 0x202830, shoes: 0xf0d030, hair: 0xe0e0e8, accent: 0xf0d030 } },
  { name: 'thug', stubble: true, frown: true, scale: 1.0, width: 1.0, hair: 'cap', colors: { skin: 0xc89870, shirt: 0x7a3a3a, pants: 0x3a3a44, shoes: 0x1c1c1c, hair: 0x1a1410, accent: 0x303030 } },
  { name: 'punk', war: 0xb02060, frown: true, scale: 0.96, width: 0.9, hair: 'mohawk', pads: true, colors: { skin: 0xe0b890, shirt: 0x2a2a30, pants: 0x4a2a5a, shoes: 0x101010, hair: 0x30d0a0, accent: 0xa0a0b0 } },
  { name: 'runner', knit: true, scale: 0.94, width: 0.8, hair: 'hat', sleeves: true, colors: { skin: 0xb88a60, shirt: 0x3a6aa8, pants: 0xc8c8c8, shoes: 0xd04030, hair: 0x2a2a60, accent: 0xd04030 } },
  { name: 'knifer', frown: true, scale: 1.0, width: 0.95, mask: true, hair: 'cap', vest: true, colors: { skin: 0xc89870, shirt: 0x303030, pants: 0x1c2a1c, shoes: 0x101010, hair: 0x101010, accent: 0x505850 } },
  { name: 'bruiser', stubble: true, scar: true, frown: true, scale: 1.28, width: 1.5, arm: 1.5, leg: 1.2, hair: 'cap', belt: true, vest: true, gloves: true, colors: { skin: 0xb8805a, shirt: 0x5a5a5a, pants: 0x5a4a2a, shoes: 0x201810, hair: 0x181008, accent: 0x8a2a1a, belt: 0x141010 } },
  { name: 'foreman', stubble: true, frown: true, scale: 1.4, width: 1.55, arm: 1.5, leg: 1.2, hair: 'helmet', vest: true, belt: true, pads: true, gloves: true, colors: { skin: 0xa8704a, shirt: 0xd08020, pants: 0x30343a, shoes: 0x14100c, hair: 0xf0c020, accent: 0xe0b020, belt: 0x141010 } },
  { name: 'matron', frown: true, scale: 1.22, width: 1.25, hair: 'long', pads: true, colors: { skin: 0xe8c0a0, shirt: 0x7a2a6a, pants: 0x1a1a2a, shoes: 0x600a30, hair: 0xc8c8d8, accent: 0xe0d060 }, eyes: 0xe02060 },
  { name: 'ringmaster', scale: 1.15, width: 1.1, hair: 'hat', vest: true, belt: true, colors: { skin: 0xe0b890, shirt: 0xb81a2a, pants: 0x1a1a1a, shoes: 0x0a0a0a, hair: 0x1a1a1a, accent: 0xe8c840 } },
  { name: 'colossus', scar: true, frown: true, scale: 1.7, width: 1.6, arm: 1.6, leg: 1.3, hair: 'helmet', pads: true, vest: true, gloves: true, belt: true, colors: { skin: 0x7a8a8a, shirt: 0x2a3a4a, pants: 0x20282e, shoes: 0x0e1014, hair: 0x6a7a8a, accent: 0x40c0d0, belt: 0x101418 }, eyes: 0x40e0ff },
];
for (const s of SPECS) emit(s);
