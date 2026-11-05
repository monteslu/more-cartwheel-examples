// hordeshooter has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): the eight floors at the boss wave, the crowd, the fall of the boss and the hatch, and from the title by real input.
//   node tests/hordeshooter-quality.mjs
import { sweep, mash } from './lib/quality-sweep.mjs';
const BOSS = [5, 10, 15, 20, 26, 32, 38, 44], FIRST = [1, 6, 11, 16, 21, 27, 33, 39];
const floor = (n) => ({
  name: `floor ${n + 1} boss wave`, app: 'hordeshooter_auto', frames: 900, presets: ['verylow', 'low', 'ultra'], env: { HORDE_WAVE: String(BOSS[n]), HORDE_PLAYERS: '3', HORDE_BOSS_WEAK: '1', HORDE_FLOOR_STAY: '1' },
  reach: { 'floor.level': (v) => v >= n, 'game.ticks': (v) => v > 100 }, show: ['game.ticks', 'floor.level', 'game.wave', 'game.enemies', 'game.boss', 'game.state'],
});
const crowd = (n) => ({
  name: `floor ${n + 1} crowd`, app: 'hordeshooter_auto', frames: 1100, presets: ['verylow', 'low'], env: { HORDE_WAVE: String(FIRST[n] + 2), HORDE_PLAYERS: '4', HORDE_FLOOR_STAY: '1' },
  reach: { 'floor.level': n, 'game.enemies': (v) => v > 0, 'game.ticks': (v) => v > 100 }, show: ['game.ticks', 'floor.level', 'game.wave', 'game.enemies', 'game.state'],
});
sweep({
  game: 'hordeshooter', tables: ['hordeshooter'],
  scenes: [
    ...BOSS.map((_, n) => floor(n)), ...BOSS.map((_, n) => crowd(n)),
    { name: 'next floor', app: 'hordeshooter_auto', frames: 3000, env: { HORDE_WAVE: '5', HORDE_PLAYERS: '3', HORDE_BOSS_WEAK: '1' }, reach: { 'floor.level': (v) => v >= 1 }, show: ['game.ticks', 'floor.level', 'game.wave', 'game.state'] },
    { name: 'power-ups', app: 'hordeshooter_auto', frames: 900, env: { HORDE_POWER_SHOW: '1', HORDE_WAVE: '6', HORDE_PLAYERS: '4' }, reach: { 'game.ticks': (v) => v > 100 }, show: ['game.ticks', 'floor.level'] },
    { name: 'title to play by input', app: 'hordeshooter', frames: 900, input: mash(240, 700, 30, ['start']), reach: { 'game.ticks': (v) => v > 0 }, show: ['game.ticks', 'floor.level', 'shell.screen'] },
  ],
  control: { state: 'standard+senv+fog2+l2001+xcluster', scene: 'floor 1 boss wave' },
});
