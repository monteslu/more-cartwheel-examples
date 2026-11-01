// fighter has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): every stage, all four fighters, the select and result screens, training, and from the title by real input.
//   node tests/fighter-quality.mjs
import { sweep, mash } from './lib/quality-sweep.mjs';
const match = (st, a, b) => ({ name: `stage ${st} ${a} v ${b}`, app: 'fighter_auto', frames: 800, cmds: [`@60 match ${st} ${a} ${b}`], reach: { 'game.panel': 1, 'game.stage': st, ticks: (v) => v > 100, 'sim.def': (v, s) => s['sim.def'][0] === a && s['sim.def'][1] === b }, show: ['ticks', 'game.panel', 'game.stage', 'game.phase', 'sim.def'] });
sweep({
  game: 'fighter', tables: ['fighter'],
  scenes: [
    match(0, 0, 1), match(1, 2, 3), match(2, 0, 3), match(0, 2, 1), match(1, 3, 0), match(2, 1, 2),
    ...[[0, 1, 0], [2, 3, 1]].map(([a, b, st]) => ({ name: `select screen ${a} ${b} stage ${st}`, app: 'fighter_auto', frames: 300, cmds: [`@60 screen select ${a} ${b} ${st}`], reach: { 'game.panel': 0, 'game.stage': st }, show: ['ticks', 'game.panel', 'game.stage'] })),
    { name: 'result screen', app: 'fighter_auto', frames: 300, cmds: ['@60 screen result 0'], reach: { 'game.panel': 2 }, show: ['ticks', 'game.panel'] },
    { name: 'training', app: 'fighter_train', frames: 400, reach: { 'game.panel': 1, ticks: (v) => v > 0 }, show: ['ticks', 'game.panel'] },
    { name: 'title to play by input', app: 'fighter', frames: 900, input: mash(240, 700, 30, ['start']), reach: { 'game.panel': 1, ticks: (v) => v > 0 }, show: ['ticks', 'game.panel', 'shell.screen'] },
  ],
  control: { state: 'physical+map+nmap+ao+rmap+mmap+skin+senv+sheen+l0001+xcluster_csm', scene: 'stage 0 0 v 1' },
});
