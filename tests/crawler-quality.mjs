// crawler has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): a
// crowded floor of every tileset, a floor's end and the next, every charm held, and the menus by real input.
//   node tests/crawler-quality.mjs
import { sweep, mash } from './lib/quality-sweep.mjs';
const TILESETS = [5, 25, 45, 65, 85, 105];
const crowd = (f) => ({
  name: `floor ${f} crowd`, app: 'crawler_auto', frames: 900, presets: ['verylow', 'low', 'ultra'], env: { CRAWLER_FLOOR: String(f), CRAWLER_PLAYERS: '4', CRAWLER_CROWD: '600' },
  reach: { 'game.floor': f, 'game.enemies': (v) => v > 100, 'game.ticks': (v) => v > 100 }, show: ['game.ticks', 'game.floor', 'game.enemies', 'game.state'],
});
const charms = [...Array(16).keys()].map((c) => ({
  name: `charm ${c}`, app: 'crawler_auto', frames: 700, presets: ['verylow', 'low'], env: { CRAWLER_FLOOR: '30', CRAWLER_PLAYERS: '4', CRAWLER_CHARM: String(c), CRAWLER_CROWD: '200' },
  reach: { 'game.ticks': (v) => v > 100 }, show: ['game.ticks', 'game.floor'],
}));
sweep({
  game: 'crawler', tables: ['crawler'],
  scenes: [
    ...TILESETS.map(crowd), ...charms,
    { name: 'down the stairs', app: 'crawler_auto', frames: 3600, env: { CRAWLER_FLOOR: '1', CRAWLER_PLAYERS: '2' }, reach: { 'game.floors_done': (v) => v >= 1 }, show: ['game.ticks', 'game.floor', 'game.floors_done'] },
    { name: 'title to play by input', app: 'crawler', frames: 1200, input: [...mash(240, 600, 30, ['start']), ...mash(620, 1100, 20, ['south'])], reach: { 'game.ticks': (v) => v > 0 }, show: ['game.ticks', 'game.floor', 'shell.screen'] },
  ],
  control: { state: 'lambert+vcol+instbig+fog2+icol+l2001+xcluster', scene: 'floor 5 crowd' },
});
