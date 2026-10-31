// Proportion and head shape audit of the generated characters: reads each glb (the bind pose, as it ships) and measures the figure against
// real human proportions, per character. Exit 1 when any character leaves its bound: a balloon of a head, a tiny head on a long neck, a
// cartoon torso. Sources for the numbers are in findings/brawler.md (figure drawing canon: about 7.5 heads, elbow at the waist, hands at mid thigh;
// US adult tables: head breadth about 0.66 of head height, shoulders over the deltoids about 2.1 heads).
//   node tools/check-heads.mjs [--dir <models>] [--expect-fail] [name ...]
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
let DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'models'), EXPECT_FAIL = false;
const names = [];
for (let i = 0; i < args.length; i++) { if (args[i] === '--dir') DIR = args[++i]; else if (args[i] === '--expect-fail') EXPECT_FAIL = true; else names.push(args[i]); }
const ALL = ['rook', 'mika', 'jet', 'dolly', 'thug', 'punk', 'runner', 'knifer', 'bruiser', 'thrower', 'guard', 'wanker', 'creep', 'foreman', 'matron', 'ringmaster', 'colossus', 'arthur'];
const WOMEN = new Set(['mika', 'dolly', 'matron']), BIG = new Set(['bruiser', 'foreman', 'colossus', 'dolly', 'guard', 'wanker']);

function readGlb(path) {
  const b = readFileSync(path), jl = b.readUInt32LE(12), json = JSON.parse(b.subarray(20, 20 + jl).toString()), bin = b.subarray(20 + jl + 8);
  const pos = (acc) => { const A = json.accessors[acc], V = json.bufferViews[A.bufferView], off = (V.byteOffset ?? 0) + (A.byteOffset ?? 0), st = V.byteStride ?? 12, out = []; for (let i = 0; i < A.count; i++) out.push([bin.readFloatLE(off + i * st), bin.readFloatLE(off + i * st + 4), bin.readFloatLE(off + i * st + 8)]); return out; };
  return { json, pos };
}
/** the measurements of one model, in its own (unscaled) units */
function measure(path) {
  const { json, pos } = readGlb(path), nodeBy = (n) => json.nodes.find((x) => x.name === n);
  const meshNode = (n) => nodeBy(n) && json.meshes[nodeBy(n).mesh];
  const prims = (mesh) => mesh.primitives.map((p) => ({ mat: json.materials[p.material].name, P: pos(p.attributes.POSITION) }));
  const face = prims(meshNode('Face')).filter((p) => /face/.test(p.mat)).flatMap((p) => p.P);
  const body = prims(meshNode('Body'));
  // the joints' bind pose
  const world = {}, par = new Map();
  json.nodes.forEach((n, i) => (n.children ?? []).forEach((c) => par.set(c, i)));
  const wy = (i) => { let v = [0, 0, 0]; for (let k = i; k !== undefined; k = par.get(k)) { const t = json.nodes[k].translation ?? [0, 0, 0], s = json.nodes[k].scale?.[0] ?? 1, isRoot = par.get(k) === undefined; const tt = isRoot ? [t[0] / s, t[1] / s, t[2] / s] : t; v = [v[0] + tt[0], v[1] + tt[1], v[2] + tt[2]]; } return v; };
  json.nodes.forEach((n, i) => { if (n.name) world[n.name] = wy(i); });
  const ys = face.map((p) => p[1]), chin = Math.min(...ys), crown = Math.max(...ys), H = crown - chin, tOf = (y) => ((y - chin) / H) * 2 - 1, band = (a, b) => face.filter((p) => tOf(p[1]) >= a && tOf(p[1]) <= b);
  const wide = (a, b) => 2 * Math.max(...band(a, b).map((p) => Math.abs(p[0])));
  const headW = wide(0.3, 0.65), jawW = wide(-0.62, -0.5), chinW = wide(-0.92, -0.86), _u = 0;
  const zz = band(0.1, 0.6).map((p) => p[2]), headD = Math.max(...zz) - Math.min(...zz);
  // the chin against the mouth: the mouth line is the doubled row near t -0.58; the chin must stand under it and not behind it
  const mouthZ = Math.max(...band(-0.66, -0.5).filter((p) => Math.abs(p[0]) < 0.02).map((p) => p[2])), chinZ = Math.max(...band(-0.95, -0.8).filter((p) => Math.abs(p[0]) < 0.02).map((p) => p[2]));
  const all = body.flatMap((p) => p.P), foot = Math.min(...all.map((p) => p[1]));
  const stature = crown - foot;
  // the neck: skin between the chin and the collar; the collar is the highest garment at the throat, or the notch where the neck starts on bare skin
  const garment = body.filter((p) => /shirt|outer|top|mask/.test(p.mat) && !/mask/.test(p.mat)).flatMap((p) => p.P).filter((p) => Math.abs(p[0]) < 0.05 && p[2] > 0.0 && p[1] < chin);
  const notch = world.Neck[1];
  const collar = Math.max(notch, garment.length ? Math.max(...garment.map((p) => p[1])) : -1e9);
  const neckVis = chin - collar;
  const skin = body.filter((p) => /skin/.test(p.mat)).flatMap((p) => p.P);
  const yn = chin - Math.min(0.02, Math.max(0.005, neckVis * 0.3)); // up under the jaw, above the flare into the shoulders
  const neckBand = skin.filter((p) => Math.abs(p[1] - yn) < 0.012 && Math.abs(p[0]) < headW);
  const neckW = neckBand.length ? 2 * Math.max(...neckBand.map((p) => Math.abs(p[0]))) : 0;
  // shoulders over the deltoids, garments included, armour and pads not; arms hang straight in the bind pose
  const ySh = world.UpperArmL[1], cloth = body.filter((p) => !/pad|metal|buckle|helmet|spike|glow/.test(p.mat)).flatMap((p) => p.P);
  const shoulders = 2 * Math.max(...cloth.filter((p) => p[1] > ySh - 0.09 && p[1] < ySh + 0.05).map((p) => Math.abs(p[0])));
  // hands: fingertips, the elbow against the waist (the narrowest point of the torso, about the Spine joint), mid thigh
  const yWr = world.HandL[1], handV = body.filter((p) => /skin|glove/.test(p.mat)).flatMap((p) => p.P).filter((p) => Math.abs(p[0] - world.HandL[0]) < 0.09 && p[1] < yWr + 0.002 && p[1] > yWr - 0.3);
  const tip = Math.min(...handV.map((p) => p[1])), midThigh = (world.UpperLegL[1] + world.LowerLegL[1]) / 2, waist = world.Spine[1] + 0.03, elbow = world.LowerArmL[1];
  return { heads: stature / H, H, wOverH: headW / H, dOverH: headD / H, jawTaper: jawW / headW, chinTaper: chinW / headW, chinOk: mouthZ - chinZ, neckVis: neckVis / H, neckW: neckW / headW, shoulders: shoulders / H, tipThigh: (tip - midThigh) / H, elbowWaist: (elbow - waist) / H, hand: (yWr - tip) / H };
}
/** bounds: real people, a band wide enough for builds and sexes */
function bounds(n) {
  return {
    heads: [7, 8], wOverH: [0.64, 0.8], dOverH: [0.72, 0.96], jawTaper: [0.62, 0.95], chinTaper: [0.24, 0.62], chinOk: [-0.012, 0.03],
    neckVis: [0.01, BIG.has(n) ? 0.3 : 0.36], neckW: [0.64, BIG.has(n) ? 1.05 : 0.9], shoulders: [WOMEN.has(n) && n !== 'dolly' ? 1.6 : 1.85, n === 'colossus' ? 2.75 : BIG.has(n) ? 2.65 : WOMEN.has(n) ? 2.2 : 2.5],
    tipThigh: [-0.6, 0.45], elbowWaist: [-0.25, 0.45], hand: [0.62, 1.0],
  };
}
let bad = 0;
const KEYS = ['heads', 'wOverH', 'dOverH', 'jawTaper', 'chinTaper', 'chinOk', 'neckVis', 'neckW', 'shoulders', 'tipThigh', 'elbowWaist', 'hand'];
console.log(['name'.padEnd(11), ...KEYS.map((k) => k.padStart(10))].join(' '));
for (const n of names.length ? names : ALL) {
  const m = measure(join(DIR, n + '.glb')), B = bounds(n), off = [];
  for (const k of KEYS) if (!(m[k] >= B[k][0] && m[k] <= B[k][1])) off.push(`${k} ${m[k].toFixed(3)} not in ${B[k].join('..')}`);
  console.log([n.padEnd(11), ...KEYS.map((k) => m[k].toFixed(3).padStart(10))].join(' ') + (off.length ? '  BAD' : ''));
  if (off.length) { bad++; console.log('  ' + off.join('; ')); }
}
console.log(bad ? `${bad} characters out of human proportion` : 'all in human proportion');
if (EXPECT_FAIL) { console.log(bad ? 'ok   control fails as it must' : 'CONTROL PASSED (a failure)'); process.exit(bad ? 0 : 1); }
process.exit(bad ? 1 : 0);
