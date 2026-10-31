// jetski's menus, by real input through the shell's actions: pad and keyboard, every page opened and backed out of, the race's
// pause with restart and quit, championship points and a time trial's best. The numbers come back from the game.
//   node tests/jetski-menu.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/menu'), TOOL = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
mkdirSync(OUT, { recursive: true });
const P_MAIN = 1, P_CONTROLS = 2, P_AUDIO = 3, P_PAUSE = 5, P_RESULTS = 6, P_NONE = 0;
const ATTRACT = 0, GRID = 1, ON = 2, DONE = 3;
let n = 0;
// tokens: a pad button, 'k:<key>' a key; each tapped for 3 frames with 3 released after; 'w<N>' waits N frames; '!cmd' is not a tap
function run(tokens, { cmds = '' } = {}) {
  const tag = 'm' + n++, input = join(OUT, tag + '.input.json'), state = join(OUT, tag + '.json');
  const script = [];
  let f = 20;
  for (const t of tokens) {
    if (t[0] === 'w') { f += +t.slice(1); continue; }
    script.push(t.startsWith('k:') ? { frame: f, keys: [t.slice(2)] } : { frame: f, buttons: [t] });
    script.push({ frame: f + 3, buttons: [], keys: [] });
    f += 6;
  }
  writeFileSync(input, JSON.stringify(script));
  const r = spawnSync('node', [TOOL, '--host', 'native', '--gpu', 'gl', '--app', 'jetski', '--frames', String(f + 6), '--input', input, '--state', state, '--shot', join(OUT, tag + '.png'), ...(cmds ? ['--start-cmds', cmds] : []), '--exe', process.env.JETSKI_EXE || join(ROOT, 'build/native/games/jetski/jetski')], {
    cwd: join(ROOT, 'deps/cartwheel-engine'), encoding: 'utf8', maxBuffer: 1 << 26,
    env: { ...process.env, CW_PROJECT_ROOT: ROOT, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_TEST_GPU: 'software', JETSKI_SMALL: '1', SDL_AUDIODRIVER: 'dummy' },
  });
  if (r.status !== 0) { console.error(r.stdout.slice(-400), r.stderr.slice(-600)); process.exit(3); }
  return JSON.parse(readFileSync(state, 'utf8'));
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const pg = (s) => `page ${s['menu.page']} row ${s['menu.row']} state ${s['race.state']} mode ${s['race.mode']}`;

const idle = run([]);
check('the game opens on the menu over the attract loop', idle['menu.page'] === P_MAIN && idle['race.state'] === ATTRACT, pg(idle));
// control: nothing pressed changes nothing
const pad = run(['south']);
check('pad: A starts the race', pad['menu.page'] === P_NONE && pad['race.state'] === GRID, pg(pad));
const key = run(['k:return']);
check('keyboard: Enter starts the race', key['menu.page'] === P_NONE && key['race.state'] === GRID, pg(key));
const rows = run(['down', 'down', 'down', 'down']);
check('down walks the rows, to Controls', rows['menu.row'] === 4, pg(rows));
const wrap = run(['up']);
check('up from the top wraps to the last row', wrap['menu.row'] === 6, pg(wrap));
const ctl = run(['down', 'down', 'down', 'down', 'south']);
check('Controls opens', ctl['menu.page'] === P_CONTROLS, pg(ctl));
check('Controls: back returns to the menu', run(['down', 'down', 'down', 'down', 'south', 'east'])['menu.page'] === P_MAIN, 'east');
check('Controls: Esc returns to the menu', run(['k:down', 'k:down', 'k:down', 'k:down', 'k:return', 'k:escape'])['menu.page'] === P_MAIN, 'escape');
const au = run(['down', 'down', 'down', 'down', 'down', 'south']);
check('Audio opens', au['menu.page'] === P_AUDIO, pg(au));
check('Audio: back returns', run(['down', 'down', 'down', 'down', 'down', 'south', 'east'])['menu.page'] === P_MAIN, 'east');
const tt = run(['down', 'right']);
check('Mode: right chooses time trial', tt['race.mode'] === 1, pg(tt));
const dif = run(['down', 'down', 'down', 'right']);
check('Difficulty: right changes it', dif['race.difficulty'] !== idle['race.difficulty'], `${idle['race.difficulty']} to ${dif['race.difficulty']}`);

// pause, on the shell's clock: the race holds still
const race = ['south', 'w300'];
const unpaused = run([...race, 'w30']), paused = run([...race, 'start', 'w30']);
check('Start pauses', paused['menu.page'] === P_PAUSE, pg(paused));
check('paused, the clock stands', paused['race.clock'] < unpaused['race.clock'] - 0.4, `${paused['race.clock'].toFixed(2)} s, running ${unpaused['race.clock'].toFixed(2)} s`);
const resume = run([...race, 'start', 'south']);
check('Resume goes on', resume['menu.page'] === P_NONE && resume['race.state'] !== ATTRACT, pg(resume));
check('Esc pauses and Esc goes back', run([...race, 'k:escape', 'k:escape'])['menu.page'] === P_NONE, 'escape twice');
const restart = run([...race, 'start', 'down', 'south']);
check('Restart race: the countdown again', restart['race.state'] === GRID && restart['race.clock'] < 0, pg(restart) + ` clock ${restart['race.clock'].toFixed(2)}`);
const pausedCtl = run([...race, 'start', 'down', 'down', 'south', 'w30']);
check('Controls from the pause: the race still stands', pausedCtl['menu.page'] === P_CONTROLS && pausedCtl['race.clock'] < unpaused['race.clock'] - 0.2, pg(pausedCtl));
const quit = run([...race, 'start', 'up', 'south']);
check('Quit to title', quit['menu.page'] === P_MAIN && quit['race.state'] === ATTRACT, pg(quit));

// the championship's points; control: no finish, no points
const unfinished = run(race);
const pts = (s) => s['cup.points'].reduce((a, b) => a + b, 0);
check('control: a race not finished pays nothing', pts(unfinished) === 0, `points ${unfinished['cup.points']}`);
const done = run(['w40'], { cmds: 'race;finish' });
check('finishing shows the results', done['menu.page'] === P_RESULTS && done['race.state'] === DONE, pg(done));
check('the championship pays every place', pts(done) === 26 && done['cup.race'] === 1, `points ${done['cup.points']} after ${done['cup.race']} race`);
// (past the countdown: a race that had not finished would be running by now, not on its grid)
const again = run(['w300', 'south'], { cmds: 'race;finish' });
check('from the results, A starts a new championship (one course so far)', pts(again) === 0 && again['race.state'] === GRID, pg(again) + ` points ${again['cup.points']}`);

// a time trial is the human alone, and keeps the best
const alone = run(['down', 'right', 'up', 'south', 'w300']);
check('time trial: the others stay home', alone['race.mode'] === 1 && alone['p1.dist'] < -1000, `p1.dist ${alone['p1.dist']}`);
check('control: in a championship they race', unpaused['p1.dist'] > -1000, `p1.dist ${unpaused['p1.dist']}`);
const best = run(['down', 'right', 'up', 'south', 'w80']);
check('control: no best before a finish', best['tt.best'] === 0, `best ${best['tt.best']}`);
const fin = run(['w40'], { cmds: 'mode 1;race;finish' });
check('a finished time trial sets the best and shows it', fin['tt.best'] > 0 && fin['tt.new_best'] === 1 && fin['menu.page'] === P_RESULTS, `best ${fin['tt.best']} new ${fin['tt.new_best']} ${pg(fin)}`);
check('a time trial pays no championship points', pts(fin) === 0, `points ${fin['cup.points']}`);
process.exit(fail ? 1 : 0);
