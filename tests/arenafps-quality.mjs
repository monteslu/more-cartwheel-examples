// arenafps has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): every arena, in split screen and alone, each mode, and from the title by real input.
//   node tests/arenafps-quality.mjs
import { sweep, mash } from './lib/quality-sweep.mjs';
const play = { 'game.state': 1, 'game.ticks': (v) => v > 100 };
const match = (arena, mode, bots, players) => ({
  name: `arena ${arena} mode ${mode} ${players}p`, app: 'arenafps_auto', frames: 1300, presets: ['verylow', 'low', 'ultra'], cmds: [`@60 match ${arena} ${mode} ${bots} ${players}`],
  reach: { ...play, 'game.arena': arena, 'game.mode': mode, 'game.players': players }, show: ['game.ticks', 'match.nf'],
});
sweep({
  game: 'arenafps', tables: ['arenafps'],
  scenes: [
    match(0, 0, 4, 4), match(1, 1, 4, 4), match(2, 2, 0, 4), match(0, 2, 0, 1), match(1, 0, 4, 1), match(2, 1, 4, 2),
    { name: 'title to play by input', app: 'arenafps', frames: 900, input: mash(240, 700, 30, ['start']), reach: { 'game.state': 1 }, show: ['game.ticks', 'game.arena', 'shell.screen'] },
  ],
  control: { state: 'standard+shadow+senv+l1001+xcluster_gi', scene: 'arena 0 mode 0 4p' },
});
