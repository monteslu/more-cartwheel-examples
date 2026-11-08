// tuxracer has a program for everything it draws at every preset on both backends (tests/lib/quality-sweep.mjs): every course, sunny, cloudy and by night, and the title.
//   node tests/tuxracer-quality.mjs
import { sweep } from './lib/quality-sweep.mjs';
const COURSES = ['frozen_river', 'path_of_daggers', 'bunny_hill', 'twisty_slope', 'bumpy_ride', 'contrib/downhill_fear', 'contrib/ice_canyon', 'contrib/ingos_speedway', 'contrib/jools_big_mountain', 'contrib/nebula', 'contrib/penguins_cant_fly', 'contrib/sentinel_towers', 'contrib/twin_paths'];
const CONDS = ['sunny', 'cloudy', 'night'];
const race = (c, cond) => ({ name: `${c} ${cond}`, app: 'tuxracer', frames: 900, presets: ['verylow', 'low', 'ultra'], cmds: [`@60 race ${c} ${cond}`], reach: { 'race.time': (v) => v > 1 }, show: ['game.state', 'race.time', 'ticks'] });
sweep({
  game: 'tuxracer', tables: ['tuxracer'],
  scenes: [...COURSES.map((c, i) => race(c, CONDS[i % 3])), ...['bunny_hill', 'path_of_daggers'].flatMap((c) => CONDS.filter((d) => COURSES.indexOf(c) % 3 !== CONDS.indexOf(d)).map((d) => race(c, d))),
    // the ghost: a kept run drawn beside him in the next race (translucent: its own states)
    { name: 'bunny_hill ghost', app: 'tuxracer', frames: 1000, presets: ['verylow', 'low', 'ultra'], cmds: ['@60 race bunny_hill', '@600 ghost keep', '@602 race bunny_hill'], reach: { ghost: (v) => v[0] === 1 }, show: ['game.state', 'ghost', 'ticks'] }, { name: 'title', app: 'tuxracer', frames: 300, reach: { ticks: (v) => v >= 0 }, show: ['game.state', 'ticks'] }],
  control: { state: 'standard+instbig+senv+l0001+xterrainlow_csm_snow', scene: 'bunny_hill cloudy' },
});
