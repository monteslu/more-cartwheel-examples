// Everything placed by hand on a course stands on something: a building's whole footprint rests on the ground, and what stands in the sea reaches under
// the lowest the water goes. The scatter has its own check in the game (the grounding command); these 952 instances had none.
// Control: lifted 0.6 m, the same check must see it.
//   node tests/jetski-placed.mjs
import { readCourse, modelBounds, modelFoot, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
/** how far each point of an instance's foot hangs over the ground under it: the worst, at a lift */
const hang = (c, foot, p, lift) => {
  const [, x, z, yaw, scale] = p, cy = Math.cos(yaw), sy = Math.sin(yaw), y = c.ground(x, z) + lift;
  let worst = -Infinity;
  for (const [fx, fy, fz] of foot) worst = Math.max(worst, y + fy * scale - c.ground(x + fx * scale * cy + fz * scale * sy, z - fx * scale * sy + fz * scale * cy));
  return worst;
};
for (const id of COURSE_IDS) {
  const c = readCourse(id);
  if (!c.placed.length) { console.log(`--   ${id}: nothing placed`); continue; }
  const bounds = c.models.map(modelBounds);
  const low = Math.min(...c.tide) - c.waves.reduce((s, w) => s + w[2] + w[3] * c.swell[1], 0);
  for (let m = 0; m < c.models.length; m++) {
    const all = c.placed.filter((p) => p[0] === m);
    if (!all.length) continue;
    const b = bounds[m];
    if (all[0][7] > 0) { // stands in the water at sea level: its lowest point goes under the lowest water
      const reach = b.miny * Math.max(...all.map((p) => p[4]));
      check(`${c.id} ${c.models[m]}: under the lowest water`, reach < low, `${all.length} placed, lowest point ${reach.toFixed(2)} m, the open sea's lowest ${low.toFixed(2)} m`);
    } else {
      const foot = modelFoot(c.models[m]), g = all.map((p) => hang(c, foot, p, 0)), worst = Math.max(...g), n = g.filter((v) => v > 0.15).length;
      check(`${c.id} ${c.models[m]}: the whole footprint on the ground`, n === 0, `${all.length} placed on ${foot.length} foot points, ${n} hanging, worst ${worst.toFixed(2)} m` + (n ? ' at ' + all.filter((p, i) => g[i] > 0.15).slice(0, 4).map((p) => p[1].toFixed(0) + ',' + p[2].toFixed(0)).join(' ') : ''));
      const lifted = all.filter((p) => hang(c, foot, p, 0.6) > 0.15).length;
      check(`${c.id} ${c.models[m]} control: lifted 0.6 m, the check sees it`, lifted > 0, `${lifted} of ${all.length} hanging`);
    }
  }
}
process.exit(fail ? 1 : 0);
