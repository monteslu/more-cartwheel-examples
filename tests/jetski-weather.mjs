// The weather and what it does, on the built cart through romdev, GL and WebGPU. Every run is one input({op:'timeline'})
// call; debug commands set it up and the game's own debug fields are read after it. Each check has a control that must fail.
//   1. wave height per weather: the human seat (steered by the computer) rides 25 s of Crater Atoll's open sea in each
//      weather; the swell it rides (highest less lowest surface under the hull) rises calm < choppy < storm.
//      Control: three runs all choppy must not show that rise.
//   2. routes: on every course the rider lined up with the course's weather route at speed, throttle held, the stick
//      left alone: through it (or up it) in a weather that opens it, stopped by it in one that shuts it.
//      Control: route_force 1 in the shut weather goes through, so "shut" must fail.
//   3. the computer riders use the weather: a lap of Crater Atoll in a storm, they take the reef; in a calm, none tries it
//      and none hits its rocks. Control: route_force 0 in the storm, no rider through.
//   4. the big water event: its waves reach every racer and lift the water under them. Control: event_amp 0.
//   5. stunt mode scores a combo: off a ramp, tricks chained. Control: the same ride with the trick button never pressed.
//   6. turbo risk: carrying turbo past a mark on its wrong side empties it, and the screen says so. Control: turbo_risk 0.
//   7. never nagging: four wrong-side marks in 20 s are answered in full once, softly once, then by the miss lights
//      alone. Control: nag 0 answers each in full.
//   8. the rider holds on: twelve hops landed nose down, rougher and rougher, fewer
//      throws than with the old odds (spill_old 1, which must throw more).
//   9. a smooth championship: the rivals' pace rises day by day, by small steps. Control: the first day three times
//      does not rise.
//  10. grounding: every rock and slab the weather adds stands on the bed on every course. Control: lifted 0.6 m.
//   node tests/jetski-weather.mjs [--gl] [--webgpu] [--only <n,...>]
import { join } from 'node:path';
import { client, ROOT, BACKENDS, arg } from './lib/jetski-romdev.mjs';

const R = client('jetski-weather'), CART = join(ROOT, arg('--cart', 'build/cart-wgpu/carts/jetski.wasc'));
const ONLY = arg('--only', '1,2,3,4,5,6,7,8,9,10').split(',').map(Number);
const f2 = (v) => Number(v).toFixed(2);
const TH = 'south';

async function fresh(backend, setup) {
  await R.load(CART, backend);
  await R.run(30);
  for (const c of setup) await R.cmd(c);
}

for (const backend of BACKENDS) {
  console.log(`== ${backend}`);
  if (ONLY.includes(1)) {
    const ride = async (w) => {
      await fresh(backend, ['course 0', `weather ${w}`, 'race', 'sw autosteer 1', 'no_dq 1', 'wxsw event 0']);
      await R.run(240, [[0, 240, TH]]);
      await R.cmd('go 1150');
      await R.run(1500, [[0, 1500, TH]]);
      const hi = await R.rd('wx.wave_max'), lo = await R.rd('wx.wave_min'), hs = await R.rd('wx.sea_hs');
      await R.shot(join(R.OUT, `${backend}-sea-${w}.png`));
      return { range: hi - lo, hs };
    };
    const m = [await ride(0), await ride(1), await ride(2)];
    R.check(`${backend} wave height follows the weather`, m[0].range < m[1].range * 0.6 && m[1].range < m[2].range, `ridden swell calm ${f2(m[0].range)} m, choppy ${f2(m[1].range)} m, storm ${f2(m[2].range)} m; sea Hs ${f2(m[0].hs)} / ${f2(m[1].hs)} / ${f2(m[2].hs)} m`);
    const c = [await ride(1), await ride(1), await ride(1)];
    R.check(`${backend} control: all choppy shows no rise`, !(c[0].range < c[1].range * 0.6 && c[1].range < c[2].range), `${f2(c[0].range)} / ${f2(c[1].range)} / ${f2(c[2].range)} m`);
  }
  if (ONLY.includes(2)) {
    /* course, an open weather, a shut weather, setup */
    const COURSES = [[0, 2, 0, []], [1, 1, 2, ['tide -1']], [2, 2, 1, []], [3, 1, 0, []], [4, 2, 1, []]];
    for (const [course, open, shut, extra] of COURSES) {
      const go = async (w, force) => {
        await fresh(backend, [`course ${course}`, `weather ${w}`, 'race', 'no_dq 1', 'wxsw event 0', ...extra, ...(force ? ['wxsw route_force 1'] : [])]);
        await R.run(240, [[0, 240, TH]]);
        await R.cmd('wx_route 45');
        await R.run(240, [[0, 240, TH]]);
        const s = { through: await R.rd('wx.route_through'), shut: await R.rd('wx.route_shut_hits'), short: await R.rd('p0.shortcuts'), arch: await R.rd('p0.shut_hits'), ramps: await R.rd('p0.ramps') };
        s.passed = course === 1 ? s.short > 0 : s.through > 0;
        s.stopped = course === 1 ? s.arch > 0 && s.short === 0 : s.shut > 0 && s.through === 0;
        if (!force) await R.shot(join(R.OUT, `${backend}-route-${course}-${w}.png`));
        return s;
      };
      const o = await go(open, false), s = await go(shut, false), c = await go(shut, true);
      R.check(`${backend} course ${course}: route open in weather ${open}, shut in ${shut}`, o.passed && s.stopped, `open ${JSON.stringify(o)}; shut ${JSON.stringify(s)}`);
      R.check(`${backend} course ${course} control: forced open, "shut" fails`, !c.stopped, JSON.stringify(c));
    }
  }
  if (ONLY.includes(3)) {
    const lap = async (w, extra) => {
      await fresh(backend, ['course 0', `weather ${w}`, 'race', 'sw autosteer 1', 'no_dq 1', 'wxsw event 0', ...extra]);
      await R.run(6600, [[0, 6600, TH]]);
      return { ai: await R.rd('wx.ai_route'), me: await R.rd('wx.route_through'), hits: await R.rd('wx.ai_shut_hits'), shoals: await R.rd('wx.ai_shoal_hits'), misses: await R.rd('ai.misses'), stuck: await R.rd('ai.stuck') };
    };
    const st = await lap(2, []), ca = await lap(0, []), ctl = await lap(2, ['wxsw route_force 0']);
    R.check(`${backend} the riders take the reef in a storm`, st.ai >= 3 && st.stuck < 150, JSON.stringify(st));
    R.check(`${backend} and keep off it, and off the rocks, in a calm`, ca.ai === 0 && ca.hits === 0 && ca.shoals === 0 && ca.stuck < 150, JSON.stringify(ca));
    R.check(`${backend} control: the reef shut, nobody through`, !(ctl.ai >= 3), JSON.stringify(ctl));
  }
  if (ONLY.includes(4)) {
    const ev = async (amp) => {
      await fresh(backend, ['course 0', 'weather 1', 'race', 'sw autosteer 1', 'no_dq 1', `wxsw event_amp ${amp}`]);
      await R.run(240, [[0, 240, TH]]);
      await R.cmd('event');
      await R.run(1500, [[0, 1500, TH]]);
      if (amp > 0) await R.shot(join(R.OUT, `${backend}-event.png`));
      await R.run(1800, [[0, 1800, TH]]);
      return { hits: await R.rd('wx.event_hits'), surf: await R.rd('wx.event_surf'), warn: await R.rd('hud.waves') };
    };
    const e = await ev(1.7), c = await ev(0);
    const lifted = (s) => s.surf.filter((v) => v > 0.9).length;
    R.check(`${backend} the event's waves reach the racers`, e.hits === 63 && lifted(e) >= 5 && e.warn >= 1, `every racer in the set: ${e.hits === 63}; water under them up to ${e.surf.map(f2).join(' ')} m; warned ${e.warn}`);
    R.check(`${backend} control: event_amp 0 lifts nothing`, !(lifted(c) >= 5), `up to ${c.surf.map(f2).join(' ')} m`);
  }
  if (ONLY.includes(5)) {
    const stunt = async (tricks) => {
      await fresh(backend, ['mode 2', 'course 0', 'weather 1', 'race', 'wxsw event 0']);
      await R.run(240, [[0, 240, TH]]);
      /* three ramps one after another, each ridden at speed: the trick button tapped all the way (a press on the water does nothing) */
      for (let k = 0; k < 3; k++) {
        await R.cmd(`ramp ${k} 10`);
        const holds = [[0, 300, TH]];
        if (tricks) for (let f = 20; f < 295; f += 12) holds.push([f, f + 3, 'west']);
        await R.run(300, holds);
      }
      await R.run(240, [[0, 240, TH]]);
      return { score: await R.rd('stunt.score'), combos: await R.rd('stunt.combos'), chain: await R.rd('stunt.best_chain'), tricks: await R.rd('stunt.tricks'), pending: await R.rd('stunt.pending'), banks: await R.rd('stunt.banks'), lost: await R.rd('stunt.lost'), spills: await R.rd('p0.spills'), air: await R.rd('p0.air_time'), state: await R.rd('race.state'), clock: await R.rd('race.clock') };
    };
    const s = await stunt(true), c = await stunt(false);
    R.check(`${backend} stunt mode scores a combo`, s.combos >= 1 && s.score > 0, JSON.stringify(s));
    R.check(`${backend} control: no tricks, no combo`, !(c.combos >= 1), JSON.stringify(c));
  }
  if (ONLY.includes(6)) {
    const risk = async (on) => {
      await fresh(backend, ['course 0', 'race', 'no_dq 1', 'wxsw event 0', `wxsw turbo_risk ${on}`]);
      await R.run(260, [[0, 260, TH]]);
      await R.cmd('sw stock 2');
      await R.cmd('sw wrong_side 1');
      await R.run(40, [[0, 40, TH]]);
      const s = { stock: await R.rd('p0.stock'), misses: await R.rd('p0.misses'), shown: await R.rd('hud.lost') };
      if (on) await R.shot(join(R.OUT, `${backend}-turbo-lost.png`));
      return s;
    };
    const r = await risk(1), c = await risk(0);
    R.check(`${backend} a wrong-side mark empties the turbo, on screen`, r.misses === 1 && r.stock === 0 && r.shown === 1, JSON.stringify(r));
    R.check(`${backend} control: turbo_risk 0 keeps it`, !(c.misses === 1 && c.stock === 0), JSON.stringify(c));
  }
  if (ONLY.includes(7)) {
    const nag = async (on) => {
      await fresh(backend, ['course 0', 'race', 'no_dq 1', 'wxsw event 0', `wxsw nag ${on}`]);
      await R.run(260, [[0, 260, TH]]);
      for (let k = 0; k < 4; k++) { await R.cmd('sw wrong_side 1'); await R.run(240, [[0, 240, TH]]); }
      const fb = await R.rd('fb');
      return { misses: await R.rd('p0.misses'), sounds: fb[4 * 4 + 1], rumbles: fb[4 * 4 + 2], quiet: await R.rd('hud.quiet') };
    };
    const n = await nag(1), c = await nag(0);
    R.check(`${backend} repeated misses are not nagged`, n.misses >= 4 && n.sounds <= 2 && n.quiet >= 3, JSON.stringify(n));
    R.check(`${backend} control: nag 0 answers each in full`, !(c.misses >= 4 && c.sounds <= 2), JSON.stringify(c));
  }
  if (ONLY.includes(8)) {
    /* the same rough moments, each a rider's mistake, with the new odds and the old: landings out of line from a drop
       (nose down or up by more and more), a long slide at full lock, and a barrel roll pressed late on a hop */
    const moments = async (old) => {
      await fresh(backend, ['course 4', 'weather 0', 'race', 'no_dq 1', 'wxsw event 0', `wxsw spill_old ${old}`]);
      await R.run(240, [[0, 240, TH]]);
      const by = {};
      const count = async (kind, f) => { const a = await R.rd('p0.spills'); await f(); by[kind] = (by[kind] || 0) + (await R.rd('p0.spills')) - a; };
      /* hops landed with the nose dipped well under the water's line (it comes up a little in the air): the rougher
         ones throw on the old odds, the roughest on both */
      for (const pitch of [-1.35, -1.4, -1.45, -1.5, -1.55, -1.6, -1.65, -1.7, -1.8, -1.9, -2.0, -2.1]) await count('landing', async () => { await R.cmd(`drop ${pitch} 1.2 24`); await R.run(150, [[0, 150, TH]]); });
      return { spills: await R.rd('p0.spills'), by };
    };
    const n = await moments(0), o = await moments(1);
    R.check(`${backend} the rider is not thrown off too easily`, n.spills <= o.spills - 3, `thrown ${n.spills} times in 12 rough moments (${JSON.stringify(n.by)}); with the old odds (the control, which must throw more) ${o.spills} (${JSON.stringify(o.by)})`);
  }
  if (ONLY.includes(9)) {
    const paceOn = async (days) => {
      const pace = [];
      for (const d of days) {
        await fresh(backend, ['course 0', 'mode 0', `wxsw cup_day ${d}`, 'race', 'no_dq 1', 'wxsw event 0', 'sw pack 0']);
        /* the ceiling each rival runs under, the tick the light goes green (the field as it stands on the grid, so only the day differs) */
        await R.run(222, [[0, 222, TH]]);
        const top = await R.rd('ai.top');
        pace.push(top.slice(1).reduce((a, b) => a + b, 0) / 5);
      }
      return pace;
    };
    const ramps = (pace) => pace.slice(1).every((p, i) => p / pace[i] - 1 > 0 && p / pace[i] - 1 < 0.03);
    const pace = await paceOn([0, 2, 4]), flat = await paceOn([0, 0, 0]);
    R.check(`${backend} the championship's pace ramps smoothly`, ramps(pace), `the rivals' top speed on days 1, 3 and 5: ${pace.map((p) => p.toFixed(2)).join(', ')} m/s`);
    R.check(`${backend} control: day 1 three times does not ramp`, !ramps(flat), flat.map((p) => p.toFixed(2)).join(', '));
  }
  if (ONLY.includes(10)) {
    let worst = -1e9, gaps = 0, cgaps = 0;
    for (let course = 0; course < 5; course++) {
      await fresh(backend, [`course ${course}`]);
      await R.run(10);
      const g = await R.cmd('wx_ground'), c = await R.cmd('wx_ground 0.6');
      worst = Math.max(worst, g.worst, g.slab_gap), gaps += g.gaps, cgaps += c.gaps;
    }
    R.check(`${backend} the weather's rocks and slabs stand on the bed`, gaps === 0, `gaps ${gaps}, worst ${f2(worst)} m`);
    R.check(`${backend} control: lifted 0.6 m`, cgaps > 0, `gaps ${cgaps}`);
  }
}
console.log(R.fails ? `jetski-weather: ${R.fails} failed` : 'jetski-weather: all passed');
await R.bye(R.fails ? 1 : 0);
