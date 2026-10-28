// Writes tests/<name>.json input scripts (pad 0 for player 1, pad2 for player 2) for the gates.
// A script is a list of moves: [frame, {lx, ly, buttons}] held until the next entry.
//   node tools/make-gate-script.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'tests');
mkdirSync(OUT, { recursive: true });
// pad buttons: X punch, A kick, B block (Y is punch + kick, the throw)
const B = { P: 'west', K: 'south', G: 'east', T: 'north' };
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
// player 1 plays the new controls against a standing player 2: a fireball (236 + punch), walk in, a high punch
// (up + punch), a low sweep (down + kick), a punch into kick chain, a throw (punch + kick), then player 2 holds a
// low guard (down + block) against a high kick that passes over it and a low punch it blocks
const moves = (s) => s.wait(intro).motion('236', 'P').wait(60).hold(6, 34).hold(5, 4)
  .hold(8, 3, [B.P]).wait(30).hold(2, 3, [B.K]).wait(70).hold(6, 20).hold(5, 3, [B.P]).wait(6).hold(5, 3, [B.K]).wait(50)
  .hold(6, 12).hold(5, 3, [B.P, B.K]).wait(80);
write('fighter-p1-moves', moves(new Script()));
// the control: the same script without the fireball motion (no projectile, the fight goes differently)
write('fighter-p1-moves-ctl', new Script().wait(intro).wait(66).hold(6, 34).hold(5, 4)
  .hold(8, 3, [B.P]).wait(30).hold(2, 3, [B.K]).wait(70).hold(6, 20).hold(5, 3, [B.P]).wait(6).hold(5, 3, [B.K]).wait(50)
  .hold(6, 12).hold(5, 3, [B.P, B.K]).wait(80));
// guard: player 2 holds a low guard (down + block) while player 1 throws a high kick (it passes over), a low punch
// (blocked low) and a mid kick (blocked); then player 2 lets go and the same mid kick lands
{
  const s = new Script().wait(intro).hold(6, 40).hold(5, 4);
  s.p.push({ f: s.f, who: 1, d: 2, b: [B.G] });
  s.hold(8, 3, [B.K]).wait(50).hold(2, 3, [B.P]).wait(40).hold(5, 3, [B.K]).wait(50);
  s.p.push({ f: s.f, who: 1, d: 5, b: [] });
  s.hold(6, 10).hold(5, 3, [B.K]).wait(60);
  write('fighter-guard', s);
  // the control: player 2 never guards
  const c = new Script().wait(intro).hold(6, 40).hold(5, 4).hold(8, 3, [B.K]).wait(50).hold(2, 3, [B.P]).wait(40).hold(5, 3, [B.K]).wait(50)
    .hold(6, 10).hold(5, 3, [B.K]).wait(60);
  write('fighter-guard-ctl', c);
}
console.log('scripts written to', OUT);

// the select screen, scrolled fast: player 1 taps right then left (one tap every 10 frames, 8 each way, three times),
// player 2 the other way; the frame-time trace of this run is the select-screen stutter check (tools/frame-trace.mjs)
{
  const s = new Script().wait(90);
  for (let k = 0; k < 3; k++) for (const d of [6, 4]) for (let i = 0; i < 8; i++) s.hold(d, 2, [], 0).hold(5, 8);
  write('fighter-select-scroll', s);
}
