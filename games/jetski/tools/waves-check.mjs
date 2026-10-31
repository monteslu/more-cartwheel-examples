// Do the waves come in sets? The crests at one spot over ten minutes, from the same wave list as src/sim.c (rubric JS3).
const W = (a) => [[200, 46, 1.5 * a], [216, 37, 1.05 * a], [150, 14, 0.1 + 0.2 * a], [252, 7, 0.05 + 0.05 * a]];
for (const a of [0.42, 0.7, 1]) {
  const om = W(a).map(([d, l]) => Math.sqrt(9.81 * 2 * Math.PI / l));
  const crests = [];
  let p1 = 0, p2 = 0;
  for (let t = 0; t < 600; t += 1 / 30) {
    let h = 0;
    W(a).forEach(([d, l, A], i) => { const k = 2 * Math.PI / l, r = (d * Math.PI) / 180; h += A * Math.sin(k * (Math.cos(r) * 120 + Math.sin(r) * -40) - om[i] * t); });
    if (p1 > p2 && p1 > h && p1 > 0.3 * a) crests.push(p1);
    p2 = p1; p1 = h;
  }
  crests.sort((x, y) => x - y);
  const med = crests[crests.length >> 1], top = crests[crests.length - 1], p90 = crests[Math.floor(crests.length * 0.9)];
  console.log(`swell ${a}: ${crests.length} crests, median ${med.toFixed(2)} m, top tenth ${p90.toFixed(2)} m, largest ${top.toFixed(2)} m, largest/median ${(top / med).toFixed(2)}`);
}
