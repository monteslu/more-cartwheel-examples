// Read out/t.txt (a jetski_auto trace: tools/run-cpu.sh with JETSKI_SMALL) and report what the race did per racer.
import { readFileSync } from 'node:fs';
const lines = readFileSync(process.argv[2] || 'out/t.txt', 'utf8').split('\n').filter((l) => l.startsWith('T ') && l.split(' | ').length >= 8);
const R = Array.from({ length: 6 }, () => ({ airs: [], cur: 0, maxSpeed: 0, sum: 0, n: 0, ground: 0, lap: -1, misses: 0, passes: 0, place: 0, maxY: 0, stuck: 0, lastD: null }));
let prevCrash = 0;
for (const l of lines) {
  const parts = l.split(' | ');
  for (let i = 0; i < 6; i++) {
    const v = parts[i + 1].split(' ').map(Number), r = R[i];
    const [x, y, z, surf, speed, air, lap, meter, misses, passes, place, grounded] = v;
    if (air) r.cur += 1 / 60; else { if (r.cur > 0.1) r.airs.push(r.cur); r.cur = 0; }
    r.maxSpeed = Math.max(r.maxSpeed, speed); r.sum += speed; r.n++;
    if (grounded > 0.2) r.ground++;
    if (speed < 3 && l.split(' ')[2] > 3) r.stuck++;
    r.lap = lap; r.misses = misses; r.passes = passes; r.place = place; r.meter = meter; r.maxY = Math.max(r.maxY, y - surf);
    r.x = x; r.z = z;
    if (Number.isNaN(x)) prevCrash++;
  }
}
const last = lines[lines.length - 1].split(' ');
console.log(`ticks ${last[1]} clock ${last[2]} swell ${lines[lines.length - 1].split(' | ')[7]}`);
R.forEach((r, i) => console.log(`#${i} place ${r.place} lap ${r.lap} meter ${r.meter} pass ${r.passes} miss ${r.misses} avg ${(r.sum / r.n).toFixed(1)} max ${r.maxSpeed.toFixed(1)} grounded ${r.ground} stuck ${r.stuck} airs ${r.airs.length} longest ${(Math.max(0, ...r.airs)).toFixed(2)} clear ${r.maxY.toFixed(2)} at ${r.x.toFixed(0)},${r.z.toFixed(0)}`));
if (prevCrash) console.log('NaN ticks', prevCrash);
