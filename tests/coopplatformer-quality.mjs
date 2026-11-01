// coopplatformer has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): started along the whole world (meadow, caves, ruins, volcano and its boss), two and four players, and from the title by real input.
//   node tests/coopplatformer-quality.mjs
import { sweep, mash } from './lib/quality-sweep.mjs';
const at = (level, x, least, app = 'coopplatformer_auto') => ({
  name: `level ${level} x ${x} ${app === 'coopplatformer_auto' ? '2p' : '4p'}`, app, frames: 500, env: { COOP_LEVEL: String(level), COOP_START_X: String(x), COOP_ABILITIES: '7', COOP_FRESH: '1' },
  reach: { 'g.tick': (v) => v > 100, 'g.state': 3, 'g.checkpoint': (v) => v >= least }, show: ['g.tick', 'g.checkpoint', 'g.state', 'g.flames'],
});
sweep({
  game: 'coopplatformer', tables: ['coopplatformer'],
  scenes: [
    at(0, 20, 0), at(0, 300, 3), at(1, 300, 3), at(2, 200, 2), at(3, 120, 2), at(3, 400, 3, 'coopplatformer_auto4'), at(5, 60, 1), at(5, 110, 2), at(5, 160, 3), at(5, 215, 4), at(5, 245, 7),
    { name: 'bubbles 4p', app: 'coopplatformer_auto4', frames: 500, env: { COOP_TEST_BUBBLES: '1', COOP_FRESH: '1' }, reach: { 'g.tick': (v) => v > 100, 'g.bubbles': (v) => v > 0 }, show: ['g.tick', 'g.bubbles'] },
    { name: 'map', app: 'coopplatformer', frames: 700, env: { COOP_FRESH: '1' }, input: [...mash(240, 420, 30, ['start']), { frame: 480, buttons: ['back'] }, { frame: 486, buttons: [] }], reach: { 'g.state': 3, 'g.map_seen': 1 }, show: ['g.state', 'g.map_seen'] },
    { name: 'boss', app: 'coopplatformer_auto_boss', frames: 2400, reach: { 'g.tick': (v) => v > 100, 'g.boss_hp': (v, s) => v > 0 || s['g.won'] > 0 }, show: ['g.tick', 'g.boss_hp', 'g.won'] },
    { name: 'title to play by input', app: 'coopplatformer', frames: 900, env: { COOP_FRESH: '1' }, input: mash(240, 700, 30, ['start']), reach: { 'g.state': 3 }, show: ['g.state'] },
  ],
  control: { state: 'physical+map+vcol+rep+nmap+ao+rmap+shadow+l2801+s100', scene: 'level 0 x 20 2p' },
});
