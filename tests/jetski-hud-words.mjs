// Words on the race screen, on the cart through romdev, GL and WebGPU, by the pad.
//   A new race starts clean: a trick's word is up when the race is restarted from the pause menu, or a new one is started, and on the first frames of the new race no word is up.
//   Control: pop_keep 1 (the reset skipped) must leave the word up; the same check on a build without the reset fails the same way.
//   The next buoy's distance never stands under a word: on every course the countdown, a big ramp with a trick and with a tail-first wipeout, and wipeouts along the course with the next buoy ahead, counting frames its box meets a word's box.
//   Control: label_old 1 (the distance where it stood before) must meet a word in the same rides.
//   node tests/jetski-hud-words.mjs [--gl] [--webgpu] [--cart <path>] [--pop-only]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readCourse, COURSE_IDS } from '../games/jetski/tools/course-read.mjs';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.JETSKI_SESSION || 'jetski-words', OUT = join(ROOT, 'out/hud-words');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc'))), BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'], POP_ONLY = process.argv.includes('--pop-only');
mkdirSync(OUT, { recursive: true });
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); process.exit(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (text) => tool('wasm', { op: 'command', text }), step = (frames) => tool('frame', { op: 'step', frames }), pad = (p) => tool('input', { op: 'set', ports: [p] });
const press = async (b) => { await pad({ [b]: true }); await step(3); await pad({}); await step(3); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
// up a ramp to a finished trick, and the frame its word is up
async function trick(lv, ramp) {
  await pad({}); await cmd(`course ${lv}`); await cmd('no_dq 1'); await cmd('swell 0.5'); await cmd('race'); await step(4); await cmd(`ramp ${ramp} 50`);
  for (let f = 0, t = -1; f < 700; f++) {
    const air = +(await rd('p0.air_time'));
    if (t < 0 && air > 0.2) t = f;
    await pad({ south: true, ...(t >= 0 && f - t < 6 ? { west: true, axes: { lx: 0, ly: 1 } } : t >= 0 && f - t < 30 ? { axes: { lx: 0, ly: 1 } } : {}) });
    await step(1);
    if (+(await rd('hud.pop_n')) > 0 && +(await rd('p0.tricks')) > 0) { await pad({}); return true; }
  }
  await pad({});
  return false;
}
async function popWords(backend, tag, lv, ramp, pre) {
  if (pre) await cmd(pre);
  const up = await trick(lv, ramp), before = +(await rd('hud.pop_n'));
  await press('start'); await press('down'); await press('south');
  const state = await rd('race.state'), clock = +(await rd('race.clock')), after = +(await rd('hud.pop_n'));
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-restart.png`) });
  const up2 = await trick(lv, ramp), before2 = +(await rd('hud.pop_n'));
  await cmd('race');
  await step(1);
  const after2 = +(await rd('hud.pop_n'));
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-race.png`) });
  return { up, before, state, clock, after, up2, before2, after2 };
}
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); process.exit(3); }
  await step(20);
  const lv0 = COURSE_IDS.indexOf('atoll'), big0 = readCourse('atoll').ramps.findIndex((r) => r[5] > 0);
  const on = await popWords(backend, 'fixed', lv0, big0);
  check('a trick word is up before the restart', on.up && on.before > 0 && on.up2 && on.before2 > 0, `${on.before} and ${on.before2} words`);
  check('restart from the pause menu: the countdown, no word over it', on.clock < 0 && on.after === 0, `clock ${on.clock.toFixed(2)}, ${on.after} words`);
  check('a new race: no word over it', on.after2 === 0, `${on.after2} words`);
  const ctl = await popWords(backend, 'control', lv0, big0, 'sw pop_keep 1');
  await cmd('sw pop_keep 0');
  check('control: pop_keep 1, the word carries over', ctl.after > 0 && ctl.after2 > 0, `${ctl.after} and ${ctl.after2} words`);
  if (POP_ONLY) continue;
  await pad({});
  // the distance against the words
  const count = async (old) => {
    await cmd(`sw label_old ${old}`);
    let hits = 0, frames = 0, shots = 0, oldShots = 0;
    for (const id of COURSE_IDS) {
      const c = readCourse(id), lv = COURSE_IDS.indexOf(id), rs = c.ramps.map((r, i) => (r[5] > 0 ? i : -1)).filter((i) => i >= 0);
      if (!rs.length) { const q = c.ramps.findIndex((r) => r[4] < 1); if (q >= 0) rs.push(q); else rs.push(0); }
      const note = (what, d) => { hits += d; if (old) console.log(`     ${id} ${what}: ${d} frames`); };
      const watch = async (what, n, each) => {
        const before = +(await rd('hud.label_hits'));
        for (let f = 0; f < n; f += 2) {
          if (each) await each(f);
          const h0 = +(await rd('hud.label_hits'));
          if (old === 0 && (f % 40 === 0) && shots < 40) { await tool('frame', { op: 'stepAndShot', frames: 2, path: join(OUT, `${backend}-label-${id}-${what}-${f}.png`) }); shots++; } else await step(2);
          if (old === 1 && +(await rd('hud.label_hits')) > h0 && oldShots < 6) { await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-label-old-${id}-${what}-${f}.png`) }); oldShots++; }
          frames += 2;
        }
        note(what, +(await rd('hud.label_hits')) - before);
      };
      const start = async (more) => { await pad({}); await cmd(`course ${lv}`); await cmd('no_dq 1'); await cmd('swell 0.5'); await cmd('race'); await step(4); if (more) await cmd(more); };
      // the countdown and GO, from the grid
      await start(); await watch('countdown', 300);
      // wipeouts with the next buoy ahead: a fall tail first, from places along the course
      for (const at of [150, 500, 900]) { await start(`go ${at}`); await step(4); await cmd('drop 1.2 14 15'); await watch(`wipeout-${at}`, 200, () => pad({ south: true, axes: { lx: 0, ly: 1 } })); }
      // a big ramp, a kick and a tail first landing
      for (const r of rs.slice(0, 1)) for (const way of ['kick', 'tail']) {
        await start(`ramp ${r} 50`);
        let t = -1;
        await watch(`ramp${r}-${way}`, 560, async (f) => {
          const air = +(await rd('p0.air_time')); if (t < 0 && air > 0.2) t = f;
          await pad({ south: true, ...(t >= 0 && way === 'tail' ? { axes: { lx: 0, ly: 1 } } : {}), ...(way === 'kick' && t >= 0 && f - t < 6 ? { north: true } : {}) });
        });
      }
    }
    await pad({}); await cmd('sw label_old 0');
    return { hits, frames };
  };
  const was = await count(1), now = await count(0);
  check('the distance stands clear of every word', now.hits === 0, `${now.hits} frames of ${now.frames}`);
  check('control: label_old 1 meets a word in the same rides', was.hits > 0, `${was.hits} frames of ${was.frames}`);
}
await pad({});
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
