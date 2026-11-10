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
// The game keeps what the check counts (hud.label_hits: frames the distance's box met a word's; hud.pop_n, hud.pops and
// hud.pop_tick: the words up, put up, and the tick of the latest; p0.air_time), so every ride is a few ranged requests:
// a held pad and a stretch of frames in one input sequence, waits that look every 6 frames, and pictures every 40
// frames while they are kept. The request count is printed.
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.JETSKI_SESSION || 'jetski-words', OUT = join(ROOT, 'out/hud-words');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc'))), BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'], POP_ONLY = process.argv.includes('--pop-only');
mkdirSync(OUT, { recursive: true });
let calls = 0;
const tool = async (name, args) => {
  calls++;
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': process.env.CW_ROMDEV_AGENT || 'jetski-words' }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
// every exit tears this session's romdev host down: a host left loaded holds the server's memory
async function bye(code) {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
}
const cmd = (text) => tool('wasm', { op: 'command', text }), step = (frames) => tool('frame', { op: 'step', frames });
/* ranges, not loops: each [pad, frames] held for its frames, the whole list in one request */
const hold = (list) => tool('input', { op: 'sequence', steps: list.filter(([, n]) => n > 0).map(([p, frames]) => ({ input: { ports: [p] }, frames })) });
const pad = (p) => hold([[p, 1]]);
const press = (b) => hold([[{ [b]: true }, 3], [{}, 3]]);
/** the named fields, in one request */
const rdAll = async (names) => {
  const st = await tool('wasm', { op: 'debugState' });
  const o = {};
  for (const f of st.fields || []) if (names.includes(f.name)) o[f.name] = +(f.values ?? f.value);
  return o;
};
/** the gas held until the craft has been in the air 0.2 s (a look every 6 frames); the frames it took, or -1 */
async function untilAir(p, cap) {
  for (let f = 0; f < cap; f += 6) {
    if ((await rdAll(['p0.air_time']))['p0.air_time'] > 0.2) return f;
    await hold([[p, 6]]);
  }
  return -1;
}
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
// up a ramp to a finished trick, and the frame its word is up: the gas to the flight, the trick (west with the stick
// back for 6 ticks, the stick held to 30), then the gas until the trick's word is up, looking every 6 frames
async function trick(lv, ramp) {
  await pad({}); await cmd(`course ${lv}`); await cmd('no_dq 1'); await cmd('swell 0.5'); await cmd('race'); await step(4); await cmd(`ramp ${ramp} 50`);
  const air = await untilAir({ south: true }, 700);
  if (air < 0) { await pad({}); return false; }
  await hold([[{ south: true, west: true, axes: { lx: 0, ly: 1 } }, 6], [{ south: true, axes: { lx: 0, ly: 1 } }, 24]]);
  for (let f = air + 30; f < 700; f += 6) {
    const r = await rdAll(['hud.pop_n', 'p0.tricks']);
    if (r['hud.pop_n'] > 0 && r['p0.tricks'] > 0) { await pad({}); return true; }
    await hold([[{ south: true }, 6]]);
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
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
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
      // n frames with the pad p held: one request, or 40-frame stretches while pictures are kept (every 40 frames of the
      // layout now, up to 40 in all; the old layout's first 6 frames on which its distance met a word)
      const watch = async (what, n, p) => {
        const before = (await rdAll(['hud.label_hits']))['hud.label_hits'];
        for (let f = 0; f < n;) {
          const chunk = (old === 0 && shots < 40) || (old === 1 && oldShots < 6) ? Math.min(40, n - f) : n - f;
          const h0 = old === 1 && oldShots < 6 ? (await rdAll(['hud.label_hits']))['hud.label_hits'] : 0;
          if (old === 0 && shots < 40) { await hold([[p, chunk - 2]]); await tool('frame', { op: 'stepAndShot', frames: 2, path: join(OUT, `${backend}-label-${id}-${what}-${f}.png`) }); shots++; } else await hold([[p, chunk]]);
          if (old === 1 && oldShots < 6 && (await rdAll(['hud.label_hits']))['hud.label_hits'] > h0) { await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-label-old-${id}-${what}-${f}.png`) }); oldShots++; }
          f += chunk, frames += chunk;
        }
        note(what, (await rdAll(['hud.label_hits']))['hud.label_hits'] - before);
      };
      const start = async (more) => { await pad({}); await cmd(`course ${lv}`); await cmd('no_dq 1'); await cmd('swell 0.5'); await cmd('race'); await step(4); if (more) await cmd(more); };
      // the countdown and GO, from the grid
      await start(); await watch('countdown', 300, {});
      // wipeouts with the next buoy ahead: a fall tail first, from places along the course
      for (const at of [150, 500, 900]) { await start(`go ${at}`); await step(4); await cmd('drop 1.2 14 15'); await watch(`wipeout-${at}`, 200, { south: true, axes: { lx: 0, ly: 1 } }); }
      // a big ramp, a kick and a tail first landing
      // the run-up to 0.2 s of air counts as part of the ride: its frames are watched too
      for (const r of rs.slice(0, 1)) for (const way of ['kick', 'tail']) {
        await start(`ramp ${r} 50`);
        const before = (await rdAll(['hud.label_hits']))['hud.label_hits'];
        const t = await untilAir({ south: true }, 560);
        const runUp = t < 0 ? 560 : t;
        // the kick: 6 frames of it as soon as the look sees the air, counted with the run-up
        if (t >= 0 && way === 'kick') await hold([[{ south: true, north: true }, 6]]);
        const kicked = t >= 0 && way === 'kick' ? 6 : 0;
        frames += runUp + kicked;
        note(`ramp${r}-${way}-run-up`, (await rdAll(['hud.label_hits']))['hud.label_hits'] - before);
        if (t < 0) continue;
        if (way === 'kick') await watch(`ramp${r}-${way}`, 560 - runUp - kicked, { south: true });
        else await watch(`ramp${r}-${way}`, 560 - runUp, { south: true, axes: { lx: 0, ly: 1 } });
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
console.log(`${calls} romdev requests`);
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
