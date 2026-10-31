// Does the swell fold? The surface is x + sum(Q A D cos), y = sum(A sin): it folds where the map from rest position to surface
// turns over, the Jacobian of its horizontal part reaching 0. Measured by finite differences over the open sea and through
// time, at each difficulty's largest sea, from the scene's real wave list and the engine's own Q (cw__waves_prepare). A steeper
// control (Q scaled 2.5 times) must fold, or the measure proves nothing.
const W = (a) => [[200, 46, 1.5 * a, 0.7], [216, 37, 1.05 * a, 0.7], [150, 14, 0.14 + 0.22 * a, 0.65], [252, 7, 0.05 + 0.05 * a, 0.5]];
const SEA = { easy: 0.8, normal: 1, hard: 1.18 };
function surface(ws, x0, z0, t, scale) {
  let x = x0, y = 0, z = z0;
  for (const w of ws) {
    const th = w.k * (w.dx * x0 + w.dz * z0) - w.om * t, s = Math.sin(th), c = Math.cos(th), qa = w.q * w.a * scale;
    x += qa * w.dx * c, y += w.a * s, z += qa * w.dz * c;
  }
  return [x, y, z];
}
function sweep(name, a, scale) {
  const ws = W(a).map(([d, l, A, st]) => {
    const r = (d * Math.PI) / 180, k = (2 * Math.PI) / l;
    return { dx: Math.cos(r), dz: Math.sin(r), k, a: A, om: Math.sqrt(9.81 * k), q: st / (k * A * 4) };
  });
  const e = 0.05;
  let minJ = 9, folded = 0, n = 0, steepest = 0, top = -9;
  for (let t = 0; t < 120; t += 0.37) for (let x0 = -300; x0 < 300; x0 += 2.3) for (let z0 = -300; z0 < 300; z0 += 17.1) {
    const p = surface(ws, x0, z0, t, scale), u = surface(ws, x0 + e, z0, t, scale), v = surface(ws, x0, z0 + e, t, scale);
    const a11 = (u[0] - p[0]) / e, a12 = (v[0] - p[0]) / e, a21 = (u[2] - p[2]) / e, a22 = (v[2] - p[2]) / e;
    const J = a11 * a22 - a12 * a21;
    if (J < minJ) minJ = J;
    if (J <= 0) folded++;
    const hx = u[1] - p[1], hz = v[1] - p[1], run = Math.hypot(u[0] - p[0], u[2] - p[2]), sl = Math.hypot(hx, hz) / Math.max(run, 1e-6);
    if (sl > steepest) steepest = sl;
    if (p[1] > top) top = p[1];
    n++;
  }
  console.log(`${name.padEnd(20)} min Jacobian ${minJ.toFixed(3)}  folded ${folded}/${n}  steepest slope ${(Math.atan(steepest) * 180 / Math.PI).toFixed(0)} deg  highest crest ${top.toFixed(2)} m`);
  return folded;
}
let bad = 0;
for (const [d, s] of Object.entries(SEA)) bad += sweep(d, s, 1);
const ctl = sweep('control: Q x 2.5', 1.18, 2.5);
console.log(bad === 0 && ctl > 0 ? 'no fold in the real sea; the control folds' : 'FAIL or the control did not fold');
process.exit(bad === 0 && ctl > 0 ? 0 : 1);
