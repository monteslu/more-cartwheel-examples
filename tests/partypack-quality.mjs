// partypack has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): every party game, the hub and the board, and from the title by real input.
//   node tests/partypack-quality.mjs
import { sweep, mash } from './lib/quality-sweep.mjs';
const GAMES = { hurdle: 0, echo: 1, beatbox: 2, quiz: 3, blast: 4, raft: 5, stack: 6, loop: 7, sumo: 8, coins: 9, tiles: 10 };
const S_RESULT = 8, S_FINAL = 9; // main.c: S_MENU, S_PICK, S_TROPHY, S_BOARD, S_INTRO, S_COUNT, S_PLAY, S_FINISH, S_RESULT, S_FINAL
// a whole round, to its results: Blast Ring needs one more light late in a round than in its first 15 seconds
const game = (g) => ({ name: g, app: 'partypack_auto', frames: 4500, presets: ['verylow', 'low'], env: { PP_GAME: g }, reach: { 'game.seen': (v) => (v >> S_RESULT) & 1, 'game.mg_seen': (v) => v === 1 << GAMES[g] }, show: ['game.seen', 'game.mg_seen', 'game.state', 'game.round'] });
sweep({
  game: 'partypack', tables: ['partypack'],
  scenes: [
    ...Object.keys(GAMES).map(game),
    { name: 'tournament to the final', app: 'partypack_auto', frames: 20000, env: {}, presets: ['verylow', 'low'], reach: { 'game.seen': (v) => (v >> S_FINAL) & 1 }, show: ['game.seen', 'game.mg_seen', 'game.state', 'game.round', 'game.mg'] },
    { name: 'hub by input', app: 'partypack', frames: 420, input: mash(240, 330, 30, ['start']), reach: { 'game.state': (v) => v < 4 }, show: ['game.state', 'shell.screen'] },
    { name: 'title to the board by input', app: 'partypack', frames: 900, input: mash(240, 700, 30, ['start']), reach: { 'game.state': (v) => v >= 3 }, show: ['game.state', 'game.round', 'game.mg', 'shell.screen'] },
  ],
  control: { state: 'standard+shadow+senv+l1800+s100', scene: 'blast' },
});
