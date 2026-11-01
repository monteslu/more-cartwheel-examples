// Following the buoys brings you to the ramps. For each ramp, its distance from the straight path between the buoys either side of it: a buoy stands 8.5 m off the
// line and a ramp is 9 m in half width, so a ramp within 12 m of that path is under a rider who is riding between the two. It points the way the path goes too.
// Control: the layout released with clusters and shortcuts well off the line (tests/jetski-ramp-path-control.json) must fail on every course.
//   node tests/jetski-ramp-path.mjs
import { readFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCourse, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), BOUND = 12, COS = 0.8, CONTROL = JSON.parse(readFileSync(join(ROOT, 'tests/jetski-ramp-path-control.json'), 'utf8'));
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
/** the ramps off the path, the worst distance */
function measure(c, ramps) {
  const on = c.buoys, off = [];
  let worst = 0;
  for (const r of ramps) {
    // the buoys before and after along the course, by the nearest pair on the map: a ramp is judged by where it stands, not by what it says about itself
    let best = Infinity, cos = 0;
    for (let i = 0; i < on.length; i++) {
      const A = on[i], B = on[(i + 1) % on.length], dx = B[0] - A[0], dz = B[1] - A[1], l2 = dx * dx + dz * dz, t = Math.max(0, Math.min(1, ((r[0] - A[0]) * dx + (r[1] - A[1]) * dz) / l2)), d = Math.hypot(r[0] - A[0] - dx * t, r[1] - A[1] - dz * t);
      if (d < best) best = d, cos = (Math.sin(r[2]) * dx + Math.cos(r[2]) * dz) / Math.sqrt(l2);
    }
    if (best > worst) worst = best;
    if (best > BOUND || cos < COS) off.push(`${r[0].toFixed(0)},${r[1].toFixed(0)} ${best.toFixed(0)} m, cos ${cos.toFixed(2)}`);
  }
  return { off, worst };
}
for (const id of COURSE_IDS) {
  const c = readCourse(id), m = measure(c, c.ramps), k = measure(c, CONTROL[id]);
  check(`${id}: ${c.ramps.length} ramps on the path`, m.off.length === 0 && c.ramps.length >= 6, `${m.off.length} off it, furthest ${m.worst.toFixed(1)} m, bound ${BOUND} m${m.off.length ? ': ' + m.off.join('; ') : ''}`);
  check(`${id} control: the released layout fails`, k.off.length > 0, `${k.off.length} of ${CONTROL[id].length} off it, furthest ${k.worst.toFixed(1)} m`);
}
process.exit(fail ? 1 : 0);
