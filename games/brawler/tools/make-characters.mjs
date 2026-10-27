// Generates every fighter and enemy as a skinned glb (one shared skeleton, the
// same clip library, vertex colours) into assets/models/. Run:
//   node tools/make-characters.mjs
// Facing is +Z in the model. Rotation values below are offsets from the rest
// pose (arms and legs hang along -Y): -x swings a limb forward, +z on the left
// side (+x) lifts it out sideways.
import { mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb, Geo } from './gltf-writer.mjs';

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
const hex = (h) => [((h >> 16) & 255) / 255, ((h >> 8) & 255) / 255, (h & 255) / 255].map((c) => Math.pow(c, 2.2));
const shade = (c, k) => c.map((x) => Math.min(1, x * k));

function build(spec) {
  const g = new Geo();
  const S_ = spec.scale ?? 1, W = spec.width ?? 1;
  const B = (n) => world[BI[n]];
  const cl = Object.fromEntries(Object.entries(spec.colors).map(([k, v]) => [k, hex(v)]));
  const rel = (bone, p) => [p[0] - world[BI[bone]][0], p[1] - world[BI[bone]][1], p[2] - world[BI[bone]][2]]; // keep in model space; skin handles it
  // all geometry is authored in model space at rest; the inverse bind matrices place it in each bone
  const hipC = B('Hips');
  // pelvis, torso
  g.frustum([0, hipC[1] - 0.02, 0], 0.17 * W, 0.11 * W, 0.17 * W, 0.11 * W, 0.2, cl.pants, BI.Hips);
  g.frustum([0, B('Spine')[1] + 0.1, 0], 0.17 * W, 0.1 * W, 0.2 * W, 0.115 * W, 0.22, cl.shirt, BI.Spine);
  g.frustum([0, B('Chest')[1] + 0.12, 0], 0.2 * W, 0.115 * W, 0.24 * W, 0.12 * W, 0.26, shade(cl.shirt, 1.1), BI.Chest);
  if (spec.belt) g.frustum([0, hipC[1] + 0.1, 0], 0.175 * W, 0.105 * W, 0.175 * W, 0.105 * W, 0.05, cl.belt || cl.accent, BI.Hips);
  if (spec.vest) g.frustum([0, B('Chest')[1] + 0.12, 0.0], 0.21 * W, 0.125 * W, 0.25 * W, 0.13 * W, 0.24, cl.accent, BI.Chest);
  if (spec.pads) for (const sd of [1, -1]) g.blob([sd * 0.27 * W, B('Chest')[1] + 0.24, 0], 0.1 * W, 0.07, 0.1 * W, cl.accent, BI.Chest);
  // head
  g.limb(B('Neck'), [0, B('Neck')[1] + 0.09, 0], 0.06, 0.055, cl.skin, BI.Neck);
  const hc = [0, B('Head')[1] + 0.12, 0.0];
  g.blob(hc, 0.115, 0.135, 0.125, cl.skin, BI.Head, 9, 6);
  g.box([0, hc[1] - 0.02, 0.125], 0.04, 0.045, 0.04, shade(cl.skin, 0.95), BI.Head); // nose
  for (const sd of [1, -1]) g.box([sd * 0.045, hc[1] + 0.025, 0.115], 0.035, 0.025, 0.02, spec.eyes ? hex(spec.eyes) : [0.02, 0.02, 0.02], BI.Head);
  if (spec.hair === 'cap') g.blob([0, hc[1] + 0.04, -0.01], 0.125, 0.1, 0.135, cl.hair, BI.Head, 9, 4);
  else if (spec.hair === 'mohawk') g.frustum([0, hc[1] + 0.11, -0.01], 0.02, 0.11, 0.02, 0.09, 0.09, cl.hair, BI.Head);
  else if (spec.hair === 'long') { g.blob([0, hc[1] + 0.03, -0.01], 0.125, 0.11, 0.135, cl.hair, BI.Head, 9, 4); g.frustum([0, hc[1] - 0.08, -0.1], 0.1, 0.03, 0.09, 0.03, 0.26, cl.hair, BI.Head); }
  else if (spec.hair === 'hat') { g.frustum([0, hc[1] + 0.1, 0], 0.13, 0.13, 0.12, 0.12, 0.09, cl.hair, BI.Head); g.frustum([0, hc[1] + 0.06, 0.06], 0.16, 0.2, 0.16, 0.2, 0.02, cl.hair, BI.Head); }
  else if (spec.hair === 'helmet') g.blob([0, hc[1] + 0.02, 0], 0.14, 0.14, 0.145, cl.hair, BI.Head, 9, 5);
  if (spec.headband) g.frustum([0, hc[1] + 0.07, 0], 0.122, 0.132, 0.122, 0.132, 0.03, cl.accent, BI.Head);
  if (spec.shades) g.box([0, hc[1] + 0.03, 0.125], 0.19, 0.04, 0.03, [0.01, 0.01, 0.01], BI.Head);
  if (spec.mask) g.box([0, hc[1] - 0.06, 0.1], 0.2, 0.11, 0.07, cl.accent, BI.Head);
  // arms
  const armR = 0.05 * W * (spec.arm ?? 1);
  for (const sd of ['L', 'R']) {
    const u = B('UpperArm' + sd), l = B('LowerArm' + sd), h = B('Hand' + sd);
    g.blob(u, armR * 1.3, armR * 1.3, armR * 1.3, spec.sleeves ? cl.shirt : cl.skin, BI['UpperArm' + sd]);
    g.limb(u, l, armR * 1.15, armR, spec.sleeves ? cl.shirt : cl.skin, BI['UpperArm' + sd]);
    g.blob(l, armR * 1.05, armR * 1.05, armR * 1.05, cl.skin, BI['LowerArm' + sd]);
    g.limb(l, h, armR, armR * 0.8, cl.skin, BI['LowerArm' + sd]);
    g.blob([h[0], h[1] - 0.03, h[2]], armR * 1.5 * (spec.gloves ? 1.35 : 1), armR * 1.6 * (spec.gloves ? 1.25 : 1), armR * 1.4 * (spec.gloves ? 1.3 : 1), spec.gloves ? cl.accent : cl.skin, BI['Hand' + sd]);
  }
  // legs
  const legR = 0.075 * W * (spec.leg ?? 1);
  for (const sd of ['L', 'R']) {
    const u = B('UpperLeg' + sd), l = B('LowerLeg' + sd), f = B('Foot' + sd);
    g.limb(u, l, legR, legR * 0.82, cl.pants, BI['UpperLeg' + sd]);
    g.blob(l, legR * 0.85, legR * 0.85, legR * 0.85, cl.pants, BI['LowerLeg' + sd]);
    g.limb(l, [f[0], f[1] + 0.03, f[2]], legR * 0.8, legR * 0.6, spec.shorts ? cl.skin : cl.pants, BI['LowerLeg' + sd]);
    g.box([f[0], f[1] - 0.005, f[2] + 0.06], 0.11 * W, 0.08, 0.26, cl.shoes, BI['Foot' + sd]);
  }
  return g;
}

function skeletonNodes(glb) {
  const idx = BONES.map((b, i) => glb.node({ name: b[0], translation: b[2] }));
  BONES.forEach((b, i) => { if (b[1] >= 0) (glb.nodes[idx[b[1]]].children ||= []).push(idx[i]); });
  return idx;
}

function emit(spec) {
  const glb = new Glb();
  const mat = glb.material(spec.name + '_mat', 0.75, 0);
  const g = build(spec);
  const mesh = glb.mesh(spec.name, g.geometry, mat);
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
  { name: 'rook', scale: 1.08, width: 1.2, arm: 1.25, leg: 1.1, hair: 'cap', gloves: true, belt: true, sleeves: false, colors: { skin: 0xd9a066, shirt: 0xc8c2b0, pants: 0x2f4f8a, shoes: 0x3b2a20, hair: 0x2a1d14, accent: 0xb02a2a, belt: 0x201a14 } },
  { name: 'mika', scale: 0.98, width: 0.9, hair: 'long', headband: true, sleeves: false, colors: { skin: 0xf0c8a0, shirt: 0xe6a02a, pants: 0x2a2a38, shoes: 0xe8e8e8, hair: 0x8a2d1a, accent: 0xd03040 } },
  { name: 'jet', scale: 1.0, width: 1.0, hair: 'mohawk', shades: true, shorts: true, colors: { skin: 0x8c5a3c, shirt: 0x3aa07a, pants: 0x202830, shoes: 0xf0d030, hair: 0xe0e0e8, accent: 0xf0d030 } },
  { name: 'thug', scale: 1.0, width: 1.0, hair: 'cap', colors: { skin: 0xc89870, shirt: 0x7a3a3a, pants: 0x3a3a44, shoes: 0x1c1c1c, hair: 0x1a1410, accent: 0x303030 } },
  { name: 'punk', scale: 0.96, width: 0.9, hair: 'mohawk', pads: true, colors: { skin: 0xe0b890, shirt: 0x2a2a30, pants: 0x4a2a5a, shoes: 0x101010, hair: 0x30d0a0, accent: 0xa0a0b0 } },
  { name: 'runner', scale: 0.94, width: 0.8, hair: 'hat', sleeves: true, colors: { skin: 0xb88a60, shirt: 0x3a6aa8, pants: 0xc8c8c8, shoes: 0xd04030, hair: 0x2a2a60, accent: 0xd04030 } },
  { name: 'knifer', scale: 1.0, width: 0.95, mask: true, hair: 'cap', vest: true, colors: { skin: 0xc89870, shirt: 0x303030, pants: 0x1c2a1c, shoes: 0x101010, hair: 0x101010, accent: 0x505850 } },
  { name: 'bruiser', scale: 1.28, width: 1.5, arm: 1.5, leg: 1.2, hair: 'cap', belt: true, vest: true, gloves: true, colors: { skin: 0xb8805a, shirt: 0x5a5a5a, pants: 0x5a4a2a, shoes: 0x201810, hair: 0x181008, accent: 0x8a2a1a, belt: 0x141010 } },
  { name: 'foreman', scale: 1.4, width: 1.55, arm: 1.5, leg: 1.2, hair: 'helmet', vest: true, belt: true, pads: true, gloves: true, colors: { skin: 0xa8704a, shirt: 0xd08020, pants: 0x30343a, shoes: 0x14100c, hair: 0xf0c020, accent: 0xe0b020, belt: 0x141010 } },
  { name: 'matron', scale: 1.22, width: 1.25, hair: 'long', pads: true, colors: { skin: 0xe8c0a0, shirt: 0x7a2a6a, pants: 0x1a1a2a, shoes: 0x600a30, hair: 0xc8c8d8, accent: 0xe0d060 }, eyes: 0xe02060 },
  { name: 'ringmaster', scale: 1.15, width: 1.1, hair: 'hat', vest: true, belt: true, colors: { skin: 0xe0b890, shirt: 0xb81a2a, pants: 0x1a1a1a, shoes: 0x0a0a0a, hair: 0x1a1a1a, accent: 0xe8c840 } },
  { name: 'colossus', scale: 1.7, width: 1.6, arm: 1.6, leg: 1.3, hair: 'helmet', pads: true, vest: true, gloves: true, belt: true, colors: { skin: 0x7a8a8a, shirt: 0x2a3a4a, pants: 0x20282e, shoes: 0x0e1014, hair: 0x6a7a8a, accent: 0x40c0d0, belt: 0x101418 }, eyes: 0x40e0ff },
];
for (const s of SPECS) emit(s);
