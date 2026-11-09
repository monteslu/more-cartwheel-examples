// Body proportions of skinned character glb files, measured on the rest pose against human anatomy, so each
// character's build for its role is held by numbers and not by eye. Works on any glb whose skin uses the shared bone
// names (Hips, Spine, Chest, Neck, Head, UpperArmL/R, LowerArmL/R, HandL/R, UpperLegL/R, LowerLegL/R, FootL/R):
// the fighter's characters and crowd, and the brawler's.
//   node tools/body-ratios.mjs <file.glb | dir ...>           print the table
//   node tools/body-ratios.mjs --check <roles.json> <dir>     check every glb roles.json names against its role's
//                                                             limits (ROLES below); exit 1 on any miss
// Every triangle is given to the bone carrying most of its first vertex; widths and girths come from cutting the
// triangles with a horizontal plane (so they do not depend on where the mesh's rings fall). Measures:
//   heads       stature / head height (chin to crown; hair, hats and helmets left out). People: 7 to 8.
//   leg         visible inseam / stature: from the floor to where the two legs part (the pelvis's lowest point, or
//               lower where the thighs, trousers on, pass into each other below it). People: 0.44 to 0.48; thighs that
//               pass through each other below the crotch fuse into one column and the legs read short.
//   sh/hip      shoulder breadth (outside of the deltoids, clothes on) / hip breadth (outside of hips and thighs).
//               Men about 1.3 to 1.45, women 1.1 to 1.3; a power athlete 1.3 to 1.8.
//   arm/head    upper arm thickness (side to side, halfway down) / head breadth. A man about 0.65, an athlete 0.75 to
//               0.9; past 1.0 an arm reads as padding, not muscle.
//   chest/waist chest girth / waist girth (the narrowest girth between the hips and the chest, outer surface, clothes
//               on, belts and straps left out). Average man 1.12, athlete 1.2 to 1.4.
//   chest/seat  chest girth / seat girth (crotch to 12 cm above it, under any coat). A man 1.05 to 1.25.
//   belly       the belly's front past the chest's front (cm; positive: the belly sticks out further than the chest).
//   ease        outer garments (jacket, gi, coat): girth with them minus girth without, at chest, waist and seat (cm);
//               the check holds chest and waist (a coat's skirt hangs free over the seat).
//   thigh       the trouser leg's girth at mid thigh against the leg under it would be; reported as cm of girth.
import { readFileSync, readdirSync, existsSync, statSync } from 'node:fs';
import { join, basename } from 'node:path';

const CT = { 5120: 'Int8', 5121: 'Uint8', 5122: 'Int16', 5123: 'Uint16', 5125: 'Uint32', 5126: 'Float32' };
const SZ = { 5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4 };
const NC = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
function readGlb(file) {
  const b = readFileSync(file);
  if (b.readUInt32LE(0) !== 0x46546c67) throw new Error(file + ': not a glb');
  let off = 12, json = null, bin = null;
  while (off < b.length) {
    const n = b.readUInt32LE(off), t = b.readUInt32LE(off + 4), d = b.subarray(off + 8, off + 8 + n);
    if (t === 0x4e4f534a) json = JSON.parse(d.toString('utf8')); else if (t === 0x004e4942) bin = d;
    off += 8 + n;
  }
  const dv = new DataView(bin.buffer, bin.byteOffset, bin.length);
  const acc = (i) => {
    const a = json.accessors[i], v = json.bufferViews[a.bufferView], nc = NC[a.type], es = SZ[a.componentType], get = 'get' + CT[a.componentType];
    const start = (v.byteOffset ?? 0) + (a.byteOffset ?? 0), stride = v.byteStride || nc * es;
    const out = new Float64Array(a.count * nc);
    for (let k = 0; k < a.count; k++) for (let c = 0; c < nc; c++) out[k * nc + c] = dv[get](start + k * stride + c * es, true);
    return out;
  };
  return { json, acc };
}

/** triangles of every skinned mesh: three corners, the bone carrying most of the first corner, the material name */
function triangles(file) {
  const { json, acc } = readGlb(file);
  const skin = json.skins?.[0];
  if (!skin) throw new Error(file + ': no skin');
  const jname = skin.joints.map((n) => json.nodes[n].name);
  const T = [];
  for (const node of json.nodes) {
    if (node.mesh == null) continue;
    for (const pr of json.meshes[node.mesh].primitives) {
      const P = acc(pr.attributes.POSITION), J = acc(pr.attributes.JOINTS_0), W = acc(pr.attributes.WEIGHTS_0);
      const I = pr.indices != null ? acc(pr.indices) : Float64Array.from({ length: P.length / 3 }, (_, i) => i);
      const mat = json.materials?.[pr.material]?.name ?? '';
      const bone = (i) => { let bj = 0, bw = -1; for (let k = 0; k < 4; k++) if (W[i * 4 + k] > bw) { bw = W[i * 4 + k]; bj = J[i * 4 + k]; } return jname[bj]; };
      const hipsJ = jname.indexOf('Hips'), hipsW = (i) => { let w = 0; for (let k = 0; k < 4; k++) if (J[i * 4 + k] === hipsJ) w += W[i * 4 + k]; return w; };
      const pt = (i) => [P[i * 3], P[i * 3 + 1], P[i * 3 + 2]];
      for (let t = 0; t < I.length; t += 3) T.push({ a: pt(I[t]), b: pt(I[t + 1]), c: pt(I[t + 2]), bone: bone(I[t]), hips: Math.max(hipsW(I[t]), hipsW(I[t + 1]), hipsW(I[t + 2])), mat, mesh: node.name });
    }
  }
  return T;
}

const HEADWEAR = /hair|stubble|hat|helmet|band|shades|glow|mask|brow|lash|cap|pomade|hood|eye|teeth|cavity|mouth/i;
const ACCESSORY = /belt|buckle|metal|harness|pad|reflect|trim|band|wrap|glove|bracer|chain|holster|shield/i;
const TORSO = new Set(['Hips', 'Spine', 'Chest', 'Belly', 'CoatL', 'CoatR']);
const isArm = (b) => /^(UpperArm|LowerArm|Hand|Index|Middle|Ring|Pinky|Thumb|Fing)/.test(b);
const isLeg = (b) => /^(UpperLeg|LowerLeg|Foot)/.test(b);
const isHead = (b) => /^(Head|Jaw|Lid|Brow|Eye|Hair|Mouth|Lip|Cheek)/.test(b);

/** where the triangles cross the plane at height y: points [x, z] */
function cut(T, y) {
  const out = [];
  for (const t of T) {
    const p = [t.a, t.b, t.c];
    for (let i = 0; i < 3; i++) {
      const u = p[i], v = p[(i + 1) % 3];
      if ((u[1] - y) * (v[1] - y) < 0 || u[1] === y) {
        const k = u[1] === v[1] ? 0 : (y - u[1]) / (v[1] - u[1]);
        out.push([u[0] + (v[0] - u[0]) * k, u[2] + (v[2] - u[2]) * k]);
      }
    }
  }
  return out;
}
function hullPerim(pts) {
  if (pts.length < 3) return 0;
  pts = pts.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const cr = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  const lo = [], up = [];
  for (const p of pts) { while (lo.length >= 2 && cr(lo[lo.length - 2], lo[lo.length - 1], p) <= 0) lo.pop(); lo.push(p); }
  for (const p of pts.slice().reverse()) { while (up.length >= 2 && cr(up[up.length - 2], up[up.length - 1], p) <= 0) up.pop(); up.push(p); }
  const h = lo.slice(0, -1).concat(up.slice(0, -1));
  let s = 0; for (let i = 0; i < h.length; i++) { const a = h[i], b = h[(i + 1) % h.length]; s += Math.hypot(a[0] - b[0], a[1] - b[1]); }
  return s;
}
const spanX = (P) => P.length ? Math.max(...P.map((p) => p[0])) - Math.min(...P.map((p) => p[0])) : 0;
const ys = (y0, y1, dy = 0.004) => { const r = []; for (let y = y0; y <= y1; y += dy) r.push(y); return r; };
const yRange = (T) => { let lo = 1e9, hi = -1e9; for (const t of T) for (const p of [t.a, t.b, t.c]) { lo = Math.min(lo, p[1]); hi = Math.max(hi, p[1]); } return [lo, hi]; };

export function measure(file) {
  const T = triangles(file);
  const floor = yRange(T)[0];
  const head = T.filter((t) => isHead(t.bone) && !HEADWEAR.test(t.mat));
  const crown = yRange(head)[1];
  // the chin: the lowest point of the head bones' skin near the midline, in front of the head's centre
  const front = head.filter((t) => Math.abs(t.a[0]) < 0.03 && t.a[2] > 0);
  const chin = yRange(front)[0];
  const stature = crown - floor, headH = crown - chin;
  // head breadth: the widest head skin from 60 % of the head's height (above the ears' tops) to near the crown
  let headW = 0; for (const y of ys(chin + headH * 0.6, crown - headH * 0.12)) headW = Math.max(headW, spanX(cut(head, y)));
  // crotch: the pelvis's lowest point near the midline, and the lowest height where the trouser legs still touch
  // (the pelvis: anything the Hips bone carries a share of, which takes in the crotch of the trousers; the legs: thigh
  // geometry the Hips bone does not move at all)
  const pelvis = T.filter((t) => (t.bone === 'Hips' || t.hips > 0.05) && !ACCESSORY.test(t.mat) && !/outer/.test(t.mat));
  const legs = T.filter((t) => /^UpperLeg/.test(t.bone) && t.hips < 0.02 && !ACCESSORY.test(t.mat) && !/outer/.test(t.mat));
  const [, yHipTop] = yRange(pelvis);
  let pelvisLow = 1e9; for (const t of pelvis) for (const p of [t.a, t.b, t.c]) if (Math.abs(p[0]) < 0.03 && p[1] > floor + 0.3 * stature) pelvisLow = Math.min(pelvisLow, p[1]);
  // the legs fuse where a thigh passes more than 5 mm through the midline into the other one (thighs that only press
  // together, flattened against each other, are two legs; thighs that pass through each other are one column)
  const legL = legs.filter((t) => /L$/.test(t.bone)), legR = legs.filter((t) => /R$/.test(t.bone));
  let touch = 0;
  for (const y of ys(floor + 0.25 * stature, pelvisLow)) { if (cut(legL, y).some((p) => p[0] < -0.005) || cut(legR, y).some((p) => p[0] > 0.005)) { touch = y; break; } }
  const crotch = touch ? Math.min(pelvisLow, touch) : pelvisLow;
  const pelvisTop = pelvisLow; // the seat and hips are measured up from the pelvis, not from where the legs part
  // arms: the shoulder top, the elbow
  const range = (n) => yRange(T.filter((t) => t.bone === n));
  const [, ySh] = range('UpperArmL'), [, yEl] = range('LowerArmL');
  // bideltoid: the widest span of torso and upper arms from 16 cm under the shoulder top up to it (pads and metal out)
  const shoulder = T.filter((t) => (TORSO.has(t.bone) || /^UpperArm/.test(t.bone)) && !ACCESSORY.test(t.mat));
  let shW = 0; for (const y of ys(ySh - 0.16, ySh)) shW = Math.max(shW, spanX(cut(shoulder, y)));
  // hip breadth: torso and thighs from the crotch to 18 cm above it, arms, coat skirts and belts out
  const hips = T.filter((t) => (TORSO.has(t.bone) || isLeg(t.bone)) && !ACCESSORY.test(t.mat) && !/outer/.test(t.mat));
  let hipW = 0; for (const y of ys(pelvisTop, pelvisTop + 0.18)) hipW = Math.max(hipW, spanX(cut(hips, y)));
  // upper arm: the left arm's span halfway from 6 cm under the shoulder top to the elbow
  const armL = T.filter((t) => /^(UpperArmL|LowerArmL)$/.test(t.bone) && !ACCESSORY.test(t.mat));
  const armW = spanX(cut(armL, (ySh - 0.06 + yEl) / 2));
  // girths: the outline of the torso's cut (clothes on, straps and belts out)
  const torso = T.filter((t) => TORSO.has(t.bone) && !ACCESSORY.test(t.mat));
  const girth = (y, f = null) => hullPerim(cut(f ? torso.filter(f) : torso, y));
  // at the body's own landmark heights (fractions of stature, as people measure): the chest at the nipples (0.70 to
  // 0.74), the natural waist (0.58 to 0.66; the narrowest girth there), the seat (crotch to 12 cm above it)
  const at = (k) => floor + k * stature;
  let chest = 0, yChest = 0; for (const y of ys(at(0.7), at(0.74))) { const g = girth(y); if (g > chest) { chest = g; yChest = y; } }
  let waist = 1e9, yWaist = 0; for (const y of ys(at(0.58), at(0.66))) { const g = girth(y); if (g > 0 && g < waist) { waist = g; yWaist = y; } }
  let seat = 0; for (const y of ys(pelvisTop, pelvisTop + 0.12)) seat = Math.max(seat, girth(y, (t) => !/outer/.test(t.mat)));
  const chestW = spanX(cut(torso, yChest)), waistW = spanX(cut(torso, yWaist));
  // the belly's front past the chest's front, near the midline
  const frontAt = (y) => { const P = cut(torso, y).filter((p) => Math.abs(p[0]) < 0.06); return P.length ? Math.max(...P.map((p) => p[1])) : -1; };
  let bellyF = -1; for (const y of ys(pelvisTop + 0.1, yChest - 0.1)) bellyF = Math.max(bellyF, frontAt(y));
  const belly = bellyF - frontAt(yChest);
  // ease of outer garments
  const hasOuter = torso.some((t) => /outer/.test(t.mat));
  const inner = (t) => !/outer/.test(t.mat);
  const ease = hasOuter ? [yChest, yWaist, pelvisTop + 0.08].map((y) => { const w = girth(y, inner); return w > 0 ? girth(y) - w : 0; }) : null;
  // a trouser leg's girth at mid thigh (the left leg; points right of the midline only, so a touching leg is cut off)
  const yThigh = pelvisTop - (pelvisTop - floor) * 0.25;
  const thigh = hullPerim(cut(T.filter((t) => /^UpperLegL$/.test(t.bone) && !ACCESSORY.test(t.mat) && !/outer/.test(t.mat)), yThigh).filter((p) => p[0] > 0));
  return {
    name: basename(file, '.glb'), stature, heads: stature / headH, headH, headW, crotch: crotch - floor, touch: touch ? touch - floor : 0, pelvisLow: pelvisLow - floor,
    leg: (crotch - floor) / stature, shHip: shW / hipW, shW, hipW, armHead: armW / headW, armW,
    chestWaist: chest / waist, chest, waist, seat, chestSeat: chest / seat, chestW, waistW, yChest, yWaist, belly, ease, thigh,
  };
}

const cm = (x) => (x * 100).toFixed(1);
export function row(m) {
  return `${m.name.padEnd(11)} ${cm(m.stature).padStart(6)} ${m.heads.toFixed(2).padStart(5)} ${m.leg.toFixed(3).padStart(6)} (${cm(m.pelvisLow)}/${cm(m.touch)}) ${m.shHip.toFixed(2).padStart(5)} (${cm(m.shW)}/${cm(m.hipW)}) ${m.armHead.toFixed(2).padStart(5)} (${cm(m.armW)}/${cm(m.headW)}) ${m.chestWaist.toFixed(2).padStart(5)} (${cm(m.chest)}/${cm(m.waist)}) ${m.chestSeat.toFixed(2)} w${(m.chestW/m.waistW).toFixed(2)} ${cm(m.belly).padStart(5)} ${cm(m.thigh).padStart(5)}${m.ease ? '  ' + m.ease.map(cm).join('/') : ''}`;
}
export const HEADER = 'name        stature heads    leg (pelvis/legs touch) sh/hip (cm)      arm/head (cm)      chest/waist (girth cm) chest/seat widths belly thigh  ease chest/waist/seat';

// role limits [min, max]: how each kind of character must read
// (power: a big, muscular build; athlete and athleteF: lean, men's and women's frames; everyday: a fit bystander.
// Heads from 6.6: these heads are drawn about 7 % large, as games draw them; ease to 25 cm: a jacket or gi over a shirt)
export const ROLES = {
  power: { heads: [6.9, 8.4], leg: [0.44, 0.5], shHip: [1.3, 1.9], armHead: [0.72, 1.0], chestWaist: [1.3, 1.8], chestSeat: [1.0, 1.6], belly: [-0.5, 0.0], ease: [0, 0.25] },
  athlete: { heads: [6.6, 8.4], leg: [0.43, 0.5], shHip: [1.2, 1.75], armHead: [0.55, 0.92], chestWaist: [1.15, 1.6], chestSeat: [0.98, 1.5], belly: [-0.5, 0.02], ease: [0, 0.25] },
  athleteF: { heads: [6.6, 8.4], leg: [0.43, 0.51], shHip: [1.0, 1.45], armHead: [0.45, 0.92], chestWaist: [1.08, 1.6], chestSeat: [0.9, 1.3], belly: [-0.5, 0.0], ease: [0, 0.25] },
  everyday: { heads: [6.6, 8.4], leg: [0.43, 0.5], shHip: [1.12, 1.7], armHead: [0.5, 0.92], chestWaist: [1.1, 1.5], chestSeat: [0.95, 1.4], belly: [-0.5, 0.01], ease: [0, 0.25] },
  // a character drawn as a comic silhouette on purpose (its belly ahead of its chest is the design)
  comic: { heads: [6.6, 8.4], leg: [0.42, 0.5], shHip: [1.0, 1.7], armHead: [0.45, 1.0], chestWaist: [0.85, 1.4], chestSeat: [0.85, 1.4], belly: [-0.5, 0.2], ease: [0, 0.3] },
};
export function check(m, role) {
  const miss = [];
  for (const [k, [lo, hi]] of Object.entries(ROLES[role])) for (const v of k === 'ease' ? (m.ease ?? []).slice(0, 2) : [m[k]]) if (!(v >= lo && v <= hi)) miss.push(`${k} ${v.toFixed(3)} not in [${lo}, ${hi}]`);
  return miss;
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const args = process.argv.slice(2);
  if (args[0] === '--check') {
    const roles = JSON.parse(readFileSync(args[1], 'utf8')), dir = args[2];
    let bad = 0;
    console.log(HEADER);
    for (const [name, role] of Object.entries(roles)) {
      if (name.startsWith('_')) continue;
      const f = join(dir, name + '.glb');
      if (!existsSync(f)) { console.log(`${name}: missing ${f}`); bad++; continue; }
      const m = measure(f), miss = check(m, role);
      console.log(`${row(m)}  [${role}] ${miss.length ? 'FAIL ' + miss.join('; ') : 'ok'}`);
      if (miss.length) bad++;
    }
    console.log(bad ? `${bad} character(s) out of their role's proportions` : 'all characters within their roles');
    process.exit(bad ? 1 : 0);
  }
  const files = args.flatMap((a) => statSync(a).isDirectory() ? readdirSync(a).filter((n) => n.endsWith('.glb')).map((n) => join(a, n)) : [a]);
  console.log(HEADER);
  for (const f of files) { try { console.log(row(measure(f))); } catch (e) { console.log(`${basename(f)}: ${e.message}`); } }
}
