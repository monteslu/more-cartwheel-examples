// Writes tests/<name>.json input scripts (pad 0 for player 1, pad2 for player 2) for the gates.
// A script is a list of moves: [frame, {lx, ly, buttons}] held until the next entry.
//   node tools/make-gate-script.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'tests');
mkdirSync(OUT, { recursive: true });
const B = { L: 'west', M: 'north', H: 'east', T: 'south' };
// a numpad direction facing right to the stick
const stick = (d) => ({ lx: [1, 4, 7].includes(d) ? -1 : [3, 6, 9].includes(d) ? 1 : 0, ly: [7, 8, 9].includes(d) ? -1 : [1, 2, 3].includes(d) ? 1 : 0 });
class Script {
  constructor() { this.p = []; this.f = 1; }
  // hold direction d with buttons b for n frames
  hold(d, n, b = [], who = 0) { this.p.push({ f: this.f, who, d, b }); this.f += n; return this; }
  wait(n) { return this.hold(5, n); }
  motion(dirs, btn, who = 0) { for (const c of dirs) this.hold(+c, 2, [], who); return this.hold(+dirs.at(-1), 2, [B[btn]], who); }
  build() {
    // merge per frame: the state of both pads at each change frame
    const frames = [...new Set(this.p.map((e) => e.f))].sort((a, b) => a - b);
    const cur = [{ d: 5, b: [] }, { d: 5, b: [] }];
    return frames.map((f) => {
      for (const e of this.p.filter((x) => x.f === f)) cur[e.who] = { d: e.d, b: e.b };
      const mk = (c) => ({ buttons: c.b, axes: stick(c.d) });
      const s0 = mk(cur[0]);
      return { frame: f, buttons: s0.buttons, axes: s0.axes, pad2: mk(cur[1]) };
    });
  }
}
const write = (name, s) => writeFileSync(join(OUT, name + '.json'), JSON.stringify(s.build()));

// the match opens with an intro; fighting starts at frame ~110 (the shell is skipped by the autopilot build)
const intro = 130;
// a fireball, a jump, a chain: player 1 plays, player 2 stands
write('fighter-p1-moves', new Script().wait(intro).motion('236', 'L').wait(60).hold(6, 30).hold(5, 5).motion('623', 'H').wait(80).hold(5, 3, [B.L]).wait(8).hold(5, 3, [B.M]).wait(40));
// the control: the same script without the fireball motion
write('fighter-p1-moves-ctl', new Script().wait(intro).wait(60).hold(6, 30).hold(5, 5).wait(80).hold(5, 3, [B.L]).wait(8).hold(5, 3, [B.M]).wait(40));
console.log('scripts written to', OUT);
