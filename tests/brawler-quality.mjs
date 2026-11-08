// Street Toughs has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): the waves and the boss of every stage, bodies down, every item, the creep's flash on every stage, and from the title by real input.
//   node tests/brawler-quality.mjs
import { sweep, mash } from './lib/quality-sweep.mjs';
const stage = (s, wave, extra = []) => ({
  name: `stage ${s} ${wave === 4 ? 'boss' : `wave ${wave}`}`, app: 'brawler_auto', frames: 700, cmds: ['players 4', `stage ${s}`, `wave ${wave}`, ...extra],
  reach: { 'game.stage': s, 'game.wave': (v) => v >= wave, 'game.ticks': (v) => v > 100, ...(extra.some((c) => /knockdown/.test(c)) ? { 'rag.made': (v) => v > 0 } : {}) }, show: ['game.ticks', 'game.wave', 'game.boss_hp', 'game.enemies', 'rag.made'],
});
const props = ['item 0 0.8 0.5', 'item 1 -0.8 0.5', 'item 2 1.4 0.2', 'item 3 -1.4 0.2', 'item 4 0 -0.6'];
// the creep on every stage, his coat thrown open on its new bones and again as he is knocked down
const creep = (s) => ({
  name: `stage ${s} creep`, app: 'brawler_auto', frames: 700, cmds: ['players 4', `stage ${s}`, 'wave 1', 'spawn 16 0.6 0.3', 'spawn 16 -0.6 -0.3', '@200 flash all', '@320 flash all', '@450 knockdown'],
  reach: { 'game.stage': s, 'game.ticks': (v) => v > 100, 'game.creep': (v, st) => st['game.creep'][0] >= 2 && st['game.creep'][1] >= 2 }, show: ['game.ticks', 'game.creep', 'game.enemies', 'rag.made'],
});
sweep({
  game: 'brawler', tables: ['brawler'],
  scenes: [
    stage(0, 0, props), stage(1, 2, ['spawn 14', 'spawn 15', 'spawn 16', ...props]), stage(2, 1, props), stage(3, 3, props),
    stage(0, 4, ['@400 knockdown']), stage(1, 4, ['@400 knockdown']), stage(2, 4, ['@400 knockdown']), stage(3, 4, ['@400 knockdown']),
    creep(0), creep(1), creep(2), creep(3),
    { name: 'title to play by input', app: 'brawler', frames: 900, input: [...mash(240, 330, 60, ['start']), ...mash(400, 700, 30, ['south'])] /* title, join, then ready on the character select */, reach: { 'game.ticks': (v) => v > 100 }, show: ['game.ticks', 'game.stage', 'shell.screen'] },
  ],
  control: { state: 'physical+map+rep+nmap+ao+rmap+mmap+skin+sheen+l2001+xcluster_csm', scene: 'stage 0 creep' },
});
