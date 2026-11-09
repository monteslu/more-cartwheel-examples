// coopplatformer has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): started along the whole world (all five worlds), two and four players, every boss fought from its arena door to its end, and from the title by real input.
//   node tests/coopplatformer-quality.mjs
import { sweep, mash } from './lib/quality-sweep.mjs';
const at = (level, x, app = 'coopplatformer_auto') => ({
  name: `level ${level} x ${x} ${app === 'coopplatformer_auto' ? '2p' : '4p'}`, app, frames: 500, env: { COOP_WORLDS: '5', COOP_LEVEL: String(level), COOP_START_X: String(x), COOP_ABILITIES: '7', COOP_FRESH: '1' },
  reach: { 'g.tick': (v) => v > 100, 'g.state': 3, 'g.level': (v) => [].concat(v)[0] === level }, show: ['g.tick', 'g.checkpoint', 'g.state', 'g.flames'],
});
// a whole fight: the bots beat each boss within these frames (its death by about the frame less 200) and the run ends on the
// results card, before the next level. g.bossgate is [kind + 1, frames alive in view, hits it took, frames drawn since it died]:
// it stays when a level changes, so a run that went on past the fight can not look like one that never saw it, and the other way round.
const boss = (name, level, kind, hits, frames) => ({
  name: `boss ${name}`, app: 'coopplatformer_auto_boss', frames, env: { COOP_LEVEL: String(level), COOP_FRESH: '1' },
  reach: { 'g.bossgate': (_, s) => { const b = s['g.bossgate']; return b[0] === kind + 1 && b[1] > 200 && b[2] === hits && b[3] >= 60; }, 'g.level': (v) => v === level }, show: ['g.bossgate', 'g.tick'],
});
sweep({
  game: 'coopplatformer', tables: ['coopplatformer'],
  scenes: [
    at(0, 20), at(0, 300), at(1, 300), at(2, 200), at(3, 120), at(3, 400, 'coopplatformer_auto4'), at(4, 200),
    at(5, 100), at(5, 400), at(6, 300), at(7, 300), at(8, 300), at(9, 200, 'coopplatformer_auto4'),
    at(10, 100), at(10, 400), at(11, 300), at(12, 300), at(13, 300), at(14, 200, 'coopplatformer_auto4'),
    at(15, 100), at(15, 400), at(16, 300), at(17, 300), at(18, 300), at(19, 200, 'coopplatformer_auto4'),
    at(20, 100), at(20, 400), at(21, 300), at(22, 300), at(23, 300), at(24, 200, 'coopplatformer_auto4'),
    { name: 'bubbles 4p', app: 'coopplatformer_auto4', frames: 500, env: { COOP_TEST_BUBBLES: '1', COOP_FRESH: '1' }, reach: { 'g.tick': (v) => v > 100, 'g.bubbles': (v) => v > 0 }, show: ['g.tick', 'g.bubbles'] },
    { name: 'map', app: 'coopplatformer', frames: 700, env: { COOP_FRESH: '1' }, input: [...mash(240, 420, 30, ['start']), { frame: 480, buttons: ['back'] }, { frame: 486, buttons: [] }], reach: { 'g.state': 3, 'g.map_seen': 1 }, show: ['g.state', 'g.map_seen'] },
    boss('landlord', 4, 0, 5, 640), boss('tax golem', 9, 1, 6, 720), boss('lifeguard', 14, 2, 7, 900), boss('regional manager', 19, 3, 8, 920), boss('founder', 24, 4, 9, 1080), /* (longer since the bosses fight back, 2026-11-08) */
    // a level with no boss in it: the proof above must not pass here
    { ...boss('none', 3, 0, 5, 60), mustNotReach: true, presets: ['verylow', 'low'] },
    { name: 'title to play by input', app: 'coopplatformer', frames: 900, env: { COOP_FRESH: '1' }, input: mash(240, 700, 30, ['start']), reach: { 'g.state': 3 }, show: ['g.state'] },
  ],
  control: [
    { state: 'physical+map+vcol+rep+nmap+ao+rmap+shadow+l2801+s100', scene: 'level 0 x 20 2p' },
    { state: 'physical+map+rep+nmap+ao+emap+rmap+shadow+l2801+s100', scene: 'boss founder' },
  ],
});
