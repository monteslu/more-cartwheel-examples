// skyfighter: the sortie reads and pays off (games/skyfighter/src/feel.h), on the native build headless on software GL
// (no GPU). Each check has a control (a `sw` switch that brings that part back as it was) that must fail:
//   objective   past a balloon, the objective cue points at the next one ahead, not the nearest one behind
//               (control: sw objective_nearest 1, the cue behind the jet)
//   lock        a far bandit winking out of the lock cone 6 ticks in 20 still takes a tapped missile
//               (control: sw lock_flicker 1, no launch; a steady bandit with the switch launches, so it was lockable)
//   flash       four kills at once light one flash, rising at most a tenth of its peak a tick
//               (control: sw instant_flash 1, the light at full on its first tick)
//   damage      the jet smokes under 60 % airframe and burns under 30 %; a bandit hit and not killed smokes
//               (control: sw no_damage_states 1, none of it)
//   flights     the interceptors come two at once (control: sw lone_bandits 1, one at a time)
//   low pass    two seconds under 30 m over open water throws spray and pays a bonus (control: the same run at 60 m)
//   clouds      on Very Low (no volumetric fog) the cumulus are in the sky as billboards, in the picture where the
//               cloud is (control: sw no_cloud_cards 1, clear sky there); Medium keeps the volumes and no cards; a switch
//               from High to Very Low in flight brings the cards (control: sw cards_by_fog_state 1, by the stale fog state)
// Run: node tests/skyfighter-feel.mjs [--build build/native] [--jobs 3] [--only name,name]
import { run, pool, OUT } from './lib/skyfighter-native.mjs';
import { readImage } from './lib/skyfighter-pixels.mjs';
import { join } from 'node:path';
const only = (() => { const i = process.argv.indexOf('--only'); return i > 0 ? process.argv[i + 1].split(',') : null; })();
const PLACE = '@40 sw place -2250 1150 0'; /* Country Fair: 354 m past one balloon, the others ahead */
const legs = {
  objective: [
    ['new', { mission: 0, frames: 46, scale: 1, cmds: [PLACE] }],
    ['control', { mission: 0, frames: 46, scale: 1, cmds: ['@2 sw objective_nearest 1', PLACE] }],
  ],
  lock: [
    ['new', { mission: 3, frames: 200, scale: 1, cmds: ['@60 sw bandit_at 900', '@60 sw flicker 14 6'], input: [[70, 4], [73, 0]] }],
    ['control', { mission: 3, frames: 200, scale: 1, cmds: ['@2 sw lock_flicker 1', '@60 sw bandit_at 900', '@60 sw flicker 14 6'], input: [[70, 4], [73, 0]] }],
    ['steady', { mission: 3, frames: 200, scale: 1, cmds: ['@2 sw lock_flicker 1', '@60 sw bandit_at 900'], input: [[70, 4], [73, 0]] }],
  ],
  flash: [
    ['new', { mission: 3, frames: 120, quality: 'high', scale: 1, cmds: ['@60 sw blasts 4'] }],
    ['control', { mission: 3, frames: 120, quality: 'high', scale: 1, cmds: ['@2 sw instant_flash 1', '@60 sw blasts 4'] }],
  ],
  damage: [
    ['smoke', { mission: 3, frames: 100, scale: 1, cmds: ['@60 sw health 50'] }],
    ['fire', { mission: 3, frames: 100, scale: 1, cmds: ['@60 sw health 25'] }],
    ['bandit', { mission: 3, frames: 130, scale: 1, cmds: ['@60 sw bandit_at 380'], input: [[62, 0, 1], [70, 0, 0]] }],
    ['control', { mission: 3, frames: 130, scale: 1, cmds: ['@2 sw no_damage_states 1', '@60 sw health 25', '@60 sw bandit_at 380'], input: [[62, 0, 1], [70, 0, 0]] }],
  ],
  flights: [
    ['new', { mission: 3, frames: 420, scale: 1 }],
    ['control', { mission: 3, frames: 420, scale: 1, cmds: ['@2 sw lone_bandits 1'] }],
  ],
  lowpass: [
    ['new', { mission: 7, frames: 200, scale: 1, cmds: ['@40 sw place 600 2500 0 18'] }],
    ['control', { mission: 7, frames: 200, scale: 1, cmds: ['@40 sw place 600 2500 0 60'] }],
  ],
  clouds: [
    ['verylow', { mission: 3, frames: 70, quality: 'verylow', hideHud: true, cmds: ['@60 sw cloud_view 2'], shot: 'feel-clouds-verylow.png' }],
    ['control', { mission: 3, frames: 70, quality: 'verylow', hideHud: true, cmds: ['@2 sw no_cloud_cards 1', '@60 sw cloud_view 2'], shot: 'feel-clouds-control.png' }],
    ['medium', { mission: 3, frames: 70, quality: 'medium', hideHud: true, cmds: ['@60 sw cloud_view 2'], shot: 'feel-clouds-medium.png' }],
    ['switched', { mission: 3, frames: 70, quality: 'high', hideHud: true, cmds: ['@40 quality verylow', '@60 sw cloud_view 2'], shot: 'feel-clouds-switched.png' }],
    ['switched-control', { mission: 3, frames: 70, quality: 'high', hideHud: true, cmds: ['@2 sw cards_by_fog_state 1', '@40 quality verylow', '@60 sw cloud_view 2'], shot: 'feel-clouds-switched-control.png' }],
  ],
};
const names = Object.keys(legs).filter((n) => !only || only.includes(n));
const tasks = names.flatMap((n) => legs[n].map(([leg, o]) => () => run({ tag: `feel-${n}-${leg}`, ...o })));
const results = await pool(tasks);
const R = {}; let k = 0;
for (const n of names) { R[n] = {}; for (const [leg] of legs[n]) R[n][leg] = results[k++]; }
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
/* cloud pixels: in a box over the cloud's centre as drawn (the cloud stands on the horizon from its own height),
 * the share whose darkest channel is 15 levels over the same row at the frame's left edge (clear sky or sea) */
function cloudShare(r, shot) {
  const img = readImage(join(OUT, shot)), [x, y] = r['feel.values'].slice(7, 9).map((v) => Math.round(v * 2)), d = img.data;
  let bright = 0, count = 0;
  for (let py = y - 160; py <= y + 60; py += 2) for (let px = x - 320; px <= x + 320; px += 2) {
    if (px < 0 || py < 0 || px >= img.width || py >= img.height) continue;
    const i = (py * img.width + px) * 4, ref = (py * img.width + 150) * 4;
    count++; if (Math.min(d[i], d[i + 1], d[i + 2]) > Math.min(d[ref], d[ref + 1], d[ref + 2]) + 15) bright++;
  }
  return { share: bright / Math.max(1, count), at: [x, y] };
}
for (const n of names) {
  const r = R[n];
  if (n === 'objective') {
    const b = r.new['feel.values'][2], c = r.control['feel.values'][2];
    check('objective ahead', Math.abs(b) < 60, `bearing ${b.toFixed(0)} deg, ${r.new['feel.values'][4].toFixed(0)} m`);
    check('objective control (nearest, behind)', Math.abs(c) > 120, `bearing ${c.toFixed(0)} deg, ${r.control['feel.values'][4].toFixed(0)} m`);
  } else if (n === 'lock') {
    check('lock through a flicker', r.new['missile.launches'] >= 1, `${r.new['missile.launches']} launched, lock ${r.new['target.lock'].toFixed(2)}`);
    check('lock control (no grace)', r.control['missile.launches'] === 0, `${r.control['missile.launches']} launched`);
    check('lock control steady (the bandit is lockable)', r.steady['missile.launches'] >= 1, `${r.steady['missile.launches']} launched`);
  } else if (n === 'flash') {
    check('flash swells', r.new['feel.counts'][0] === 1 && r.new['feel.values'][1] <= 0.1, `${r.new['feel.counts'][0]} rise for 4 kills, largest tick ${r.new['feel.values'][1].toFixed(3)} of peak`);
    check('flash control (instant)', r.control['feel.values'][1] > 0.5, `largest tick ${r.control['feel.values'][1].toFixed(3)} of peak`);
  } else if (n === 'damage') {
    check('damage smoke under 60%', r.smoke['feel.counts'][1] === 1, `state ${r.smoke['feel.counts'][1]} at ${r.smoke['mission.health']}`);
    check('damage fire under 30%', r.fire['feel.counts'][1] === 2, `state ${r.fire['feel.counts'][1]} at ${r.fire['mission.health']}`);
    check('bandit smokes after a hit', r.bandit['feel.counts'][7] >= 1 && r.bandit['mission.hits'] >= 1, `${r.bandit['mission.hits']} hits, ${r.bandit['feel.counts'][7]} smoking`);
    check('damage control (none)', r.control['feel.counts'][1] === 0 && r.control['feel.counts'][7] === 0 && r.control['mission.hits'] >= 1, `state ${r.control['feel.counts'][1]}, ${r.control['feel.counts'][7]} smoking after ${r.control['mission.hits']} hits`);
  } else if (n === 'flights') {
    check('bandits in pairs', r.new['feel.counts'][5] >= 2, `${r.new['feel.counts'][5]} at once, flight of ${r.new['feel.counts'][4]}`);
    check('flights control (one at a time)', r.control['feel.counts'][5] === 1, `${r.control['feel.counts'][5]} at once`);
  } else if (n === 'lowpass') {
    check('low pass spray and bonus', r.new['feel.counts'][6] === 1, `${r.new['feel.counts'][6]} passes at ${r.new['jet.altitude'].toFixed(0)} m`);
    check('low pass control (60 m)', r.control['feel.counts'][6] === 0, `${r.control['feel.counts'][6]} passes at ${r.control['jet.altitude'].toFixed(0)} m`);
  } else if (n === 'clouds') {
    const v = cloudShare(r.verylow, 'feel-clouds-verylow.png'), c = cloudShare(r.control, 'feel-clouds-control.png'), m = cloudShare(r.medium, 'feel-clouds-medium.png');
    check('clouds on Very Low', r.verylow['feel.counts'][2] === 1 && v.share > 0.10, `cards ${r.verylow['feel.counts'][2]}, ${(v.share * 100).toFixed(0)}% cloud pixels at ${v.at.map((a) => a.toFixed(0))}`);
    check('clouds control (no cards on Very Low)', c.share < 0.05, `${(c.share * 100).toFixed(0)}% cloud pixels`);
    const sw = cloudShare(r.switched, 'feel-clouds-switched.png'), swc = cloudShare(r['switched-control'], 'feel-clouds-switched-control.png');
    check('clouds after switching High to Very Low in flight', r.switched['feel.counts'][2] === 1 && sw.share > 0.10, `cards ${r.switched['feel.counts'][2]}, ${(sw.share * 100).toFixed(0)}% cloud pixels`);
    check('clouds switched control (cards by the stale fog state)', swc.share < 0.05, `cards ${r['switched-control']['feel.counts'][2]}, ${(swc.share * 100).toFixed(0)}% cloud pixels`);
    check('clouds on Medium (volumes, no cards)', r.medium['feel.counts'][2] === 0 && r.medium['cloud.active'] === 1 && m.share > 0.10, `cards ${r.medium['feel.counts'][2]}, fog ${r.medium['cloud.active']}, ${(m.share * 100).toFixed(0)}% cloud pixels`);
  }
}
if (fail) { console.error(`FAIL: ${fail} check(s)`); process.exit(1); }
console.log('ok   skyfighter-feel');
