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
// a numpad direction facing right to d-pad buttons
const dpadButtons = (d) => [...([1, 4, 7].includes(d) ? ['left'] : [3, 6, 9].includes(d) ? ['right'] : []),
  ...([7, 8, 9].includes(d) ? ['up'] : [1, 2, 3].includes(d) ? ['down'] : [])];
// keyboard, player 1: W A S D, J punch, K kick, L block
const KEY = { west: 'j', south: 'k', east: 'l', north: 'i' };
const keysFor = (d, b) => [...([1, 4, 7].includes(d) ? ['a'] : [3, 6, 9].includes(d) ? ['d'] : []),
  ...([7, 8, 9].includes(d) ? ['w'] : [1, 2, 3].includes(d) ? ['s'] : []), ...b.map((x) => KEY[x])];
class Script {
  // mode: 'stick' (the left stick at full tilt, the old scripts), 'dpad' or 'keys' for player 1
  constructor(mode = 'stick') { this.p = []; this.f = 1; this.mode = mode; }
  // hold direction d with buttons b for n frames; ax: raw stick axes for this hold (stick mode)
  hold(d, n, b = [], who = 0, ax = null) { this.p.push({ f: this.f, who, d, b, ax }); this.f += n; return this; }
  // the raw stick at (lx, ly) with buttons b for n frames
  stickAt(lx, ly, n, b = []) { return this.hold(5, n, b, 0, { lx, ly }); }
  wait(n) { return this.hold(5, n); }
  motion(dirs, btn, who = 0) { for (const c of dirs) this.hold(+c, 2, [], who); return this.hold(+dirs.at(-1), 2, [B[btn]], who); }
  build() {
    // merge per frame: the state of both pads at each change frame
    const frames = [...new Set(this.p.map((e) => e.f))].sort((a, b) => a - b);
    const cur = [{ d: 5, b: [] }, { d: 5, b: [] }];
    return frames.map((f) => {
      for (const e of this.p.filter((x) => x.f === f)) cur[e.who] = { d: e.d, b: e.b, ax: e.ax };
      const mk = (c) => ({ buttons: c.b, axes: stick(c.d) });
      const c0 = cur[0];
      const pad2 = mk(cur[1]);
      if (c0.ax) return { frame: f, buttons: c0.b, axes: c0.ax, pad2 };
      if (this.mode === 'dpad') return { frame: f, buttons: [...dpadButtons(c0.d), ...c0.b], axes: { lx: 0, ly: 0 }, pad2 };
      if (this.mode === 'keys') return { frame: f, buttons: [], axes: { lx: 0, ly: 0 }, keys: keysFor(c0.d, c0.b), pad2 };
      const s0 = mk(c0);
      return { frame: f, buttons: s0.buttons, axes: s0.axes, pad2 };
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

// F-PT3 movement gates, played the way a thumb plays: uneven tap lengths, a held walk that rolls onto its diagonals.
// fighter-dpad-walk: right held on the d-pad for 2 s, rolling onto up-right and down-right for 2 to 4 frames now and
// then; a rest; left held for 1 s with rolls onto up-left and down-left. The fighter walks every frame of it (no stall,
// no jump, no crouch, no dash). Control: the same rolls held 9 frames, long enough to be a forward jump and a crouch.
const walk = (s, roll) => {
  s.wait(intro);
  // right for 120 frames: 6, with rolls onto 9 and 3
  s.hold(6, 17).hold(9, roll ?? 3).hold(6, 19).hold(3, roll ?? 4).hold(6, 23).hold(9, roll ?? 2).hold(6, 14).hold(3, roll ?? 3).hold(6, 120 - 17 - 19 - 23 - 14 - (roll ? 4 * roll : 12));
  s.wait(40);
  // left for 60 frames
  s.hold(4, 13).hold(7, roll ?? 3).hold(4, 21).hold(1, roll ?? 4).hold(4, 60 - 34 - (roll ? 2 * roll : 7));
  return s.wait(60);
};
write('fighter-dpad-walk', walk(new Script('dpad')));
write('fighter-dpad-walk-ctl', walk(new Script('dpad'), 9));
// fighter-dpad-jumps: single up taps of 2 to 8 frames (each must jump, the same jump), up-forward pressed directly
// and rolled into (up then up-forward), up-back pressed directly, a walk back rolled onto up-back for 4 frames (the d-pad drift rule: still a walk) and for 9 (a jump back); then up + punch on the same frame, punch 2 frames after up and
// 2 frames before (each the high punch, no jump), up + kick; a held down (a crouch) and down + kick (the sweep);
// fast forward / back alternation (walking, no dash). Control: the same with the 4-frame tap left out.
const jumps = (s, skip) => {
  s.wait(intro);
  for (const n of [2, 3, 4, 5, 6, 8]) { if (n === skip) { s.wait(60); continue; } s.hold(8, n).wait(60 - n); }
  // forward and back jumps in turn, so the fighter never crosses the other one (that would swap forward and back)
  s.hold(9, 4).wait(56);
  s.hold(7, 5).wait(55);
  s.hold(8, 1).hold(9, 4).wait(55);
  s.hold(4, 3).hold(7, 4).wait(53);
  // a walk back rolled into up-back and held there (a jump back after the drift frames)
  s.hold(4, 3).hold(7, 9).wait(48);
  s.hold(8, 3, [B.P]).hold(8, 2).wait(40);
  s.hold(8, 2).hold(8, 3, [B.P]).wait(40);
  s.hold(5, 2, [B.P]).hold(8, 4, [B.P]).wait(40);
  s.hold(8, 4, [B.K]).wait(45);
  s.hold(2, 40).hold(2, 3, [B.K]).hold(2, 10).wait(40);
  for (let k = 0; k < 6; k++) s.hold(6, 4 + (k % 2)).hold(4, 4);
  return s.wait(40);
};
write('fighter-dpad-jumps', jumps(new Script('dpad')));
write('fighter-dpad-jumps-ctl', jumps(new Script('dpad'), 4));
// fighter-stick: the analog stick. Forward pushed 20 degrees high walks (inside the 50-degree cardinal sector), up
// pushed 14 degrees to the side jumps straight up, the stick at 45 degrees jumps forward, a stick resting at 0.25
// (inside the dead zone) does nothing, a held stick down crouches. Control: forward pushed 35 degrees high (a
// diagonal: after the drift frames it is a forward jump).
const stickRun = (s, walkDeg) => {
  s.wait(intro);
  const r = walkDeg * Math.PI / 180;
  s.stickAt(+Math.cos(r).toFixed(3), -Math.sin(r).toFixed(3), 50).wait(20);
  s.stickAt(0.25, -0.97, 4).wait(56);
  s.stickAt(0.71, -0.71, 4).wait(56);
  s.stickAt(0.2, 0.15, 40).wait(10);
  s.stickAt(0.1, 1, 30).wait(20);
  s.stickAt(-1, 0.3, 40);
  return s.wait(40);
};
write('fighter-stick', stickRun(new Script(), 20));
write('fighter-stick-ctl', stickRun(new Script(), 35));
// fighter-keys: the keyboard (player 1 is W A S D, J, K). D held with W rolled in for 3 frames walks; a W tap jumps;
// W + J together is the high punch; S held crouches. Control: W held 8 frames
// during the walk (a forward jump).
const keysRun = (s, roll) => {
  s.wait(intro);
  s.hold(6, 30).hold(9, roll).hold(6, 30).wait(20);
  s.hold(8, 3).wait(57);
  s.hold(8, 3, [B.P]).wait(45);
  s.hold(2, 30);
  return s.wait(40);
};
write('fighter-keys', keysRun(new Script('keys'), 3));
write('fighter-keys-ctl', keysRun(new Script('keys'), 8));
console.log('F-PT3 scripts written');
