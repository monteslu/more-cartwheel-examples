// skyfighter has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): the title and each of the eight missions, chosen by a click on its tile as a player does.
//   node tests/skyfighter-quality.mjs
import { sweep } from './lib/quality-sweep.mjs';
const tile = (i) => ({ x: 46 + (i % 4) * 220 + 100, y: 120 + (i >> 2) * 132 + 60 });
const mission = (i) => {
  const p = tile(i), input = [];
  for (const f of [240, 300, 360, 420]) input.push({ frame: f, mouse: { ...p, buttons: ['left'] } }, { frame: f + 3, mouse: { ...p, buttons: [] } });
  // guns held, a missile and flares now and then
  for (let f = 520; f < 1500; f += 60) input.push({ frame: f, buttons: ['west'], axes: { rt: 1, lt: f % 240 === 40 ? 1 : 0 } }, { frame: f + 3, buttons: [], axes: { rt: 1 } });
  return { name: `mission ${i}`, app: 'skyfighter', frames: 1500, presets: ['low', 'ultra'], input, reach: { 'mission.level': i, 'mission.ticks': (v) => v > 60 }, show: ['mission.state', 'mission.level', 'mission.ticks', 'mission.kills'] };
};
sweep({
  game: 'skyfighter', tables: ['skyfighter', 'cw_samples', 'terrain_demo'],
  scenes: [...[0, 1, 2, 3, 4, 5, 6, 7].map(mission), { name: 'crash', app: 'skyfighter', frames: 1200, input: [{ frame: 240, buttons: ['south'] }, { frame: 243 }, { frame: 300, buttons: ['down'] }], reach: { 'crash.age': (v) => v > 1 }, show: ['mission.state', 'mission.level', 'mission.ticks', 'crash.age'] }, { name: 'title', app: 'skyfighter', frames: 300, reach: { 'mission.state': 0 }, show: ['mission.state', 'mission.level'] }],
  control: { state: 'standard+map+rep+nmap+rmap+mmap+shadow+senv+ds+l1001', scene: 'mission 0' },
});
