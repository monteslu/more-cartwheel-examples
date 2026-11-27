// The podium after a championship race, on the cart through romdev, GL and WebGPU, on every course: over the line the
// first three are parked on the judges' pier's tiers (1st in the middle and highest) with their arms up, their names
// and places over them, and the pad alone goes on from it to the results and to the next day's forecast.
// Grounding, as drawn: each parked hull's lowest point within 3 cm of its tier's top, every pile's foot in the bed,
// the deck's underside over the water the whole time, the camera over open water. Control (must fail): sw
// podium_lift 0.4 stands the craft 0.4 m over their tiers.
//   node tests/jetski-podium.mjs [--gl] [--webgpu] [--cart <path>] [--courses 0,1,2,3,4]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/podium');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const COURSES = arg('--courses', '0,1,2,3,4').split(',').map(Number);
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': process.env.JETSKI_SESSION || 'jetski-podium', 'x-romdev-agent': 'jetski-podium' };
const bye = async (code) => { try { await fetch(ROMDEV_URL + '/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {} process.exit(code); };
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
const tool = async (name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text(); let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (text) => tool('wasm', { op: 'command', text });
const press = (button) => tool('input', { op: 'sequence', steps: [{ input: { ports: [{ [button]: true }] }, frames: 3 }, { input: { ports: [{}] }, frames: 6 }] });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(2);
const P_RESULTS = 6, P_FORECAST = 7, P_PODIUM = 8;

/* a championship race on this course to the line (the human first, or in a place), held on the podium for a few seconds */
async function podium(backend, course, { place = 1, pre = [], craft = 0, shot = true } = {}) {
  for (const c of [...pre, `craft ${craft}`, 'mode 0', `course ${course}`, 'no_dq 1', 'race']) await cmd(c);
  await tool('frame', { op: 'step', frames: 200 });
  await cmd(`finish ${place}`);
  await tool('frame', { op: 'step', frames: 320 }); // the finish shows for 4.5 s, then the podium
  const page = +(await rd('menu.page'));
  await tool('frame', { op: 'step', frames: 300 }); // five seconds on the podium, measured every frame
  const path = join(OUT, `${backend}-c${course}${craft ? '-stand' : ''}${pre.length ? '-ctl' : ''}.png`);
  if (shot) await tool('frame', { op: 'stepAndShot', frames: 1, path });
  else await tool('frame', { op: 'step', frames: 1 });
  const s = { page, on: +(await rd('podium.on')), meas: (await rd('podium.meas')).map(Number), who: (await rd('podium.who')).map(Number), order: (await rd('race.order')).map(Number), label: (await rd('podium.label')).map(Number), pose: (await rd('p0.finish_pose')).map(Number), site: (await rd('podium.site')).map(Number), why: (await rd('podium.why')).map(Number) };
  return s;
}
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 120 });
  for (let k = 0; k < 120 && +(await rd('cw.loading')) !== 0; k++) await tool('frame', { op: 'step', frames: 30 });
  for (const course of COURSES) {
    const s = await podium(backend, course);
    const [gap, pile, wet, camLow, camLand, n] = s.meas;
    const tag = `${backend} course ${course}`;
    check(`${tag}: the race ends on the podium, the first three on it in order`, s.page === P_PODIUM && s.on === 1 && s.who.join() === s.order.slice(0, 3).join(), `page ${s.page}, on ${s.on}, tiers ${s.who}, order ${s.order}, places turned down ${s.why}`);
    check(`${tag}: each parked hull on its tier (within 3 cm)`, Math.abs(gap) <= 0.03 && n >= 250, `worst ${f2(gap)} m over ${n} frames`);
    check(`${tag}: every pile's foot in the bed, the deck's underside dry`, pile <= 0 && wet < 0, `pile foot ${f2(pile)} m over the bed, water ${f2(wet)} m against the underside`);
    check(`${tag}: the camera over open water`, camLow >= 1.0 && camLand === 0, `lowest ${f2(camLow)} m over the water, ${camLand} frames over land`);
    const W = 1920, Hh = 1080, inside = [0, 1, 2].every((k) => s.label[k * 2] > 0 && s.label[k * 2] < W && s.label[k * 2 + 1] > 0 && s.label[k * 2 + 1] < Hh);
    check(`${tag}: each place's name over its rider, in the picture`, inside && s.label[0] > Math.min(s.label[2], s.label[4]) && s.label[0] < Math.max(s.label[2], s.label[4]), `labels ${s.label.map((v) => v.toFixed(0))}`);
    check(`${tag}: the winner has their arms up`, s.pose[2] > 0.05 && s.pose[3] > 0.99, `glove ${f2(s.pose[2])} m over the head, pose ${f2(s.pose[3])}`);
    if (course === COURSES[0]) {
      /* the pad alone: accept on the podium goes to the results (the podium behind them), accept again to the forecast */
      await press('south'); await tool('frame', { op: 'step', frames: 10 });
      const r = +(await rd('menu.page')), on = +(await rd('podium.on'));
      await press('south'); await tool('frame', { op: 'step', frames: 10 });
      const f = +(await rd('menu.page')), off = +(await rd('podium.on'));
      check(`${tag}: the pad goes on: podium, results, forecast`, r === P_RESULTS && on === 1 && f === P_FORECAST && off === 0, `after one press page ${r} (podium ${on}), after two ${f} (podium ${off})`);
      await press('east'); await tool('frame', { op: 'step', frames: 10 });
    }
  }
  /* a slower finish: third over the line, the winner a rival */
  const third = await podium(backend, COURSES[0], { place: 3, shot: false });
  check(`${backend}: third over the line, on the third tier`, third.who[2] === 0 && third.who[0] !== 0 && Math.abs(third.meas[0]) <= 0.03, `tiers ${third.who}, gap ${f2(third.meas[0])}`);
  /* the stand-up on its tier too */
  const stand = await podium(backend, COURSES[0], { craft: 1 });
  check(`${backend}: the stand-up parked on its tier`, Math.abs(stand.meas[0]) <= 0.03 && stand.on === 1, `gap ${f2(stand.meas[0])}`);
  /* control: the craft lifted 0.4 m off their tiers must fail the grounding bound */
  const ctl = await podium(backend, COURSES[0], { pre: ['sw podium_lift 0.4'] });
  await cmd('sw podium_lift 0');
  check(`${backend}: control, sw podium_lift 0.4 fails the 3 cm bound`, Math.abs(ctl.meas[0]) > 0.03, `gap ${f2(ctl.meas[0])}`);
  /* control: sw podium 0, straight to the results */
  const none = await podium(backend, COURSES[0], { pre: ['sw podium 0'], shot: false });
  await cmd('sw podium 1');
  check(`${backend}: control, sw podium 0 goes straight to the results`, none.page === P_RESULTS && none.on === 0, `page ${none.page}`);
  await tool('host', { op: 'shutdown' });
}
await bye(fail ? 1 : 0);
