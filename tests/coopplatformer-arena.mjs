// coopplatformer: every boss arena, with the fight pushed up against the wall at its end. Bots that run for the wall and let the
// boss come to them (COOP_BOT_NO_BOSS) make the worst of each of these:
//   g.endwall   how much of the view's width is end wall (thousandths): some, so the end can be seen; never over 12 percent
//   g.bossclip  frames a boss's arm is in a wall, in any pose it was drawn in
//   g.labelgate frames a boss's or a player's words are on a player's marker (in a real fight, since the words come with the hits)
// Each has a control that must fail, in every arena: the old camera (COOP_ENDWALL_BUG), the old boss width (COOP_BOSSCLIP_BUG), the
// old labels (COOP_LABEL_BUG). The results card is checked in the picture: its side margins hold no words, so every row of
// them must be one colour from one side of the screen to the other, on both backends, whatever stands behind it. The control
// (COOP_CARD_BUG) shows the level through the card.
//   node tests/coopplatformer-arena.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const { readImage } = await import(join(ENGINE, 'tools/lib/png.mjs'));
const OUT = join(ROOT, 'out', 'arena-gate');
mkdirSync(OUT, { recursive: true });
const ARENAS = [[4, 'the Landlord'], [9, 'the Tax Golem'], [14, 'the Lifeguard'], [19, 'the Regional Manager'], [24, 'the Founder']];
let bad = 0;
const check = (ok, line) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${line}`); if (!ok) bad++; };

function run(env, frames, gpu = 'gl') {
  const state = join(OUT, 's.json'), shot = join(OUT, 's.png');
  rmSync(state, { force: true });
  const r = spawnSync('node', [join(ENGINE, 'tools/run-cart.mjs'), '--host', 'native', '--gpu', gpu, '--app', 'coopplatformer_auto_boss', '--frames', String(frames), '--state', state, '--shot', shot, '--allow-skipped'],
    { cwd: ENGINE, encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, COOP_FRESH: '1', ...env } });
  if (r.status !== 0) throw new Error(`run failed: ${(r.stderr || r.stdout).slice(-400)}`);
  return { s: JSON.parse(readFileSync(state, 'utf8')), shot };
}
const WALL = { COOP_BOT_NO_BOSS: '1' };
for (const [level, name] of ARENAS) {
  const env = { COOP_LEVEL: String(level), ...WALL };
  for (const players of [2, 4]) {
    if (players === 4 && level !== 24) continue; // the widest view, on the biggest boss
    const { s } = run({ ...env, COOP_PLAYERS: String(players) }, 1900);
    const [wf, wm, wo] = s['g.endwall'], [cf, cn, cd] = s['g.bossclip'];
    check(wf > 600 && wm > 0 && wm <= 120 && wo === 0, `${name}, ${players}p: end wall at most ${(wm / 10).toFixed(1)} percent of the view over ${wf} frames`);
    check(cf > 600 && cn === 0, `${name}, ${players}p: no arm in a wall in ${cf} frames (deepest ${cd} mm)`);
  }
  const w = run({ ...env, COOP_ENDWALL_BUG: '1' }, 1900).s['g.endwall'];
  check(w[2] > 0 && w[1] > 300, `control, ${name}: the old camera shows ${(w[1] / 10).toFixed(1)} percent end wall, over the limit on ${w[2]} frames`);
  const c = run({ ...env, COOP_BOSSCLIP_BUG: '1' }, 1900).s['g.bossclip'];
  check(c[1] > 0, `control, ${name}: the old width puts an arm in the wall on ${c[1]} frames, ${c[2]} mm deep`);
  // the words come with the hits: a real fight, to its end
  const fight = { COOP_LEVEL: String(level) };
  const f = run(fight, 1000).s;
  const [bf, bn, pf, pn] = f['g.labelgate'];
  check(bf > 100 && bn === 0 && pn === 0, `${name}, a fight: ${bf} frames of the boss's words and ${pf} of a player's, none on a marker (moved ${f['g.labelmove']} times)`);
  const l = run({ ...fight, COOP_LABEL_BUG: '1' }, 1000).s['g.labelgate'];
  check(l[1] > 0, `control, ${name}: the old label sits on a marker on ${l[1]} frames of the boss's words`);
}
/** the largest spread of any colour channel along any row of the card's margins (the outer 3 percent each side) */
function cardSpread(png) {
  const { w, h, rgba } = readImage(png);
  let worst = 0;
  const m = Math.floor(w * 0.03);
  for (let y = Math.ceil(h * 0.29); y < Math.floor(h * 0.72); y++) {
    const lo = [255, 255, 255], hi = [0, 0, 0];
    for (const x0 of [0, w - m]) for (let x = x0; x < x0 + m; x++) for (let c = 0; c < 3; c++) {
      const v = rgba[(y * w + x) * 4 + c];
      if (v < lo[c]) lo[c] = v;
      if (v > hi[c]) hi[c] = v;
    }
    for (let c = 0; c < 3; c++) worst = Math.max(worst, hi[c] - lo[c]);
  }
  return worst;
}
for (const gpu of ['gl', 'webgpu']) for (const [level, name] of [[4, 'the Landlord'], [14, 'the Lifeguard']]) {
  const env = { COOP_LEVEL: String(level) };
  const a = run(env, 720, gpu);
  check(a.s['g.won'] === 1 && cardSpread(a.shot) <= 1, `${gpu}, ${name}: the card is solid (spread ${cardSpread(a.shot)} across its margins)`);
  const b = run({ ...env, COOP_CARD_BUG: '1' }, 720, gpu);
  check(cardSpread(b.shot) > 1, `control ${gpu}, ${name}: the old card shows the level through it (spread ${cardSpread(b.shot)})`);
}
rmSync(OUT, { recursive: true, force: true });
console.log(bad ? `FAIL coopplatformer-arena: ${bad}` : 'ok   coopplatformer-arena');
process.exit(bad ? 1 : 0);
