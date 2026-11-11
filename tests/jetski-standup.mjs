// The two craft, on the cart through romdev, GL and WebGPU. Every racer rides the type the menu picks: a sit-down
// runabout or a stand-up (internal notes on the real ones: why the numbers are what they are).
//   handling: from rest on open, calm water, the throttle held 8 s, then 5 s at full lock leaning back into the carve, then a press of the trick
//     button and of the kick button on the water at speed. Each run is ONE input timeline request; the game keeps the
//     measures (debug field trial: top speed, the time to plane, the turn's radius and speed). The stand-up must be
//     slower on the straight, plane later, turn tighter, and do a submarine and a pole stand, which the sit-down cannot.
//     Control: the same timeline with the stand-up asked for but the craft left a sit-down must fail every difference.
//   grounding and the camera: half a minute of racing on the biggest sea, the computer steering: no boot ever posed off
//     the tray or the hood, no glove off its grip, the hull's middle on the water while it rides it, and a chase camera
//     that never shakes. Controls: the standing hips lifted 0.4 m (boots off the tray), the hull held by its spring alone
//     (under the surface on big faces), and a camera that shakes (sw cam_jitter 1).
//   rumble: the stand-up's tricks on the water answer on the pad (feedback counts), and nothing flashes: no new light.
// Frames from the race camera are kept in out/jetski-standup to look at.
//   node tests/jetski-standup.mjs [--gl] [--webgpu] [--cart <path>] [--baseline <main's jetski_auto.wasc>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, readFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/jetski-standup');
const SESSION = process.env.JETSKI_SESSION || 'jetski-standup';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/jetski.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const OPEN = 4; /* Gale Bank: open sea */
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': 'jetski-standup' };
async function bye(code) {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
}
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
let calls = 0;
const tool = async (name, args) => {
  calls++;
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const state = async () => { const st = await tool('wasm', { op: 'debugState' }); const o = {}; for (const f of st.fields || []) o[f.name] = f.values ?? f.value; return o; };
const cmd = (text) => tool('wasm', { op: 'command', text });
const step = (frames) => tool('frame', { op: 'step', frames });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
/* a deterministic load: the same frames are the same ticks on every run (the measures are the same bits run to run) */
const load = async (backend) => {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', deterministicSeed: 1, deterministicStepMs: 1000 / 60, ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  return (await step(20)).frameCount;
};
/* the whole handling run as one timeline, from the frame after the setup commands */
async function handling(backend, craft) {
  await load(backend);
  await cmd(`course ${OPEN}`);
  await step(30); /* the land is built on its first updates: the open water is looked for after */
  if (craft >= 0) await cmd(`craft ${craft}`);
  console.log('  trial at', JSON.stringify(await cmd('trial 0.05')));
  const f = (await step(1)).frameCount + 1;
  const w = (a, b, buttons) => ({ frame: f + a, until: f + b, buttons });
  await tool('input', { op: 'timeline', timeline: [
    w(0, 1380, ['south']),           /* throttle all the way */
    w(480, 780, ['right', 'down']),  /* full lock leaning back, a carve, 5 s */
    w(840, 843, ['west']),           /* the trick button, on the water (again in case a swell had it in the air) */
    w(880, 883, ['west']),
    w(990, 993, ['north']),          /* the kick button, on the water, likewise */
    w(1030, 1033, ['north']),
    w(1070, 1073, ['north']),
  ] });
  const s = await state();
  const t = s.trial.map(Number);
  const fb = s.fb.map(Number); /* [kind][popups, sounds, rumbles, bursts]: EV_TRICK is kind 2 */
  return { craft: +s['race.craft'], top: t[0], plane: t[6], radius: t[4], turnV: t[5], subs: +s['p0.subs'], poles: +s['p0.poles'], spills: +s['p0.spills'], trickRumble: fb[2 * 4 + 2] };
}
/* half a minute of racing on the biggest sea, the computer steering, the race camera's frames kept */
async function ride(backend, craft, sw = []) {
  await load(backend);
  await cmd(`course ${OPEN}`);
  await cmd(`craft ${craft}`);
  await cmd('no_dq 1'); await cmd('swell 1'); await cmd('sw autosteer 1');
  for (const s of sw) await cmd(s);
  await cmd('race');
  await step(240);
  await cmd('sw ground_clear 1'); await cmd('sw cam_acc_clear 1');
  const f = (await step(1)).frameCount + 1;
  await tool('input', { op: 'timeline', timeline: [{ frame: f, until: f + 1800, buttons: ['south'] }] });
  const tag = `${backend}-${craft ? 'stand' : 'sit'}${sw.length ? '-control' : ''}`;
  if (!sw.length) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${tag}.png`) });
  const s = await state();
  return { foot: +s['ground.foot_gap'], hands: +s['ground.hands_gap'], frames: +s['ground.pose_frames'], hi: +s['ground.hull_hi'], lo: +s['ground.hull_lo'], ticks: +s['ground.hull_ticks'], cam: +s['cam.acc_max'], flips: +s['cam.flips'], camTicks: +s['cam.ticks'], spills: +s['p0.spills'], aiSpills: +s['ai.spills'], aiStuck: +s['ai.stuck'] };
}
const f2 = (x) => x.toFixed(2);
/* the look from the race camera (stand.pose and stand.screen, measured by the game every frame): Crater Atoll from the grid,
   11 s of throttle, then a hard turn. The rider low and compact, the hull long ahead of the feet, the pole and the craft on
   the screen past the rider */
async function look(backend, sw = []) {
  await load(backend);
  await cmd('craft 1'); await cmd('no_dq 1'); await cmd('swell 0.6');
  for (const s of sw) await cmd(s);
  await cmd('race');
  await step(230); await cmd('sw screen_clear 1');
  const f = (await step(1)).frameCount + 1;
  await tool('input', { op: 'timeline', timeline: [{ frame: f, until: f + 330, buttons: ['south'] }] });
  const a = await state();
  const tag = `${backend}-look${sw.length ? '-control' : ''}`;
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${tag}-straight.png`) });
  const g = (await step(1)).frameCount + 1;
  await tool('input', { op: 'timeline', timeline: [{ frame: g, until: g + 40, buttons: ['south', 'right'] }] });
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${tag}-turn.png`) });
  const b = await state();
  const p = a['stand.pose'].map(Number), sc = a['stand.screen'].map(Number), pt = b['stand.pose'].map(Number), st = b['stand.screen'].map(Number);
  return { head: p[0], knee: p[1], lean: p[2], ahead: p[3], ratioMin: sc[0], poleMin: sc[1], frames: sc[2], turnHead: pt[0], turnKnee: pt[1], turnLean: pt[2], turnRatio: st[3], turnPole: st[4], pxPerM: sc[5], platePx: sc[6] };
}
/* fairly upright: knees a little bent, the torso a little forward; the hull ahead of the feet; the pole seen */
const lookOk = (r) => r.knee >= 130 && r.knee <= 162 && r.lean >= 12 && r.lean <= 30 && r.ahead >= 1.4 && r.poleMin >= 0.28;
/* the hull from its model file: how high the hood stands over the tray floor just ahead of the shins, how deep the side is
   from chine to gunwale at the stern, how thick the pole is. The control is the first, blade-like hull */
function hullOf(file) {
  const b = readFileSync(file), jl = b.readUInt32LE(12), j = JSON.parse(b.slice(20, 20 + jl)), bin = b.slice(20 + jl + 8);
  const nodeOf = (name) => j.nodes.find((n) => n.name === name);
  const verts = (name) => { const n = nodeOf(name), t = n.translation || [0, 0, 0], out = [];
    for (const p of j.meshes[n.mesh].primitives) { const a = j.accessors[p.attributes.POSITION], v = j.bufferViews[a.bufferView], f = new Float32Array(bin.buffer, bin.byteOffset + v.byteOffset + (a.byteOffset || 0), a.count * 3);
      for (let i = 0; i < a.count; i++) out.push([f[i * 3] + t[0], f[i * 3 + 1] + t[1], f[i * 3 + 2] + t[2], j.materials[p.material].name]); }
    return out; };
  const hull = verts('Hull'), pole = verts('Pole');
  const floor = Math.min(...hull.filter((v) => v[3] === 'mat').map((v) => v[1]));
  const hood = Math.max(...hull.filter((v) => v[2] > 0 && v[2] < 0.6 && Math.abs(v[0]) < 0.15).map((v) => v[1]));
  const stern = hull.filter((v) => v[2] > -0.85 && v[2] < -0.7), wide = Math.max(...stern.map((v) => Math.abs(v[0])));
  const side = stern.filter((v) => Math.abs(v[0]) > wide * 0.88 && v[3] !== 'rubber').map((v) => v[1]);
  const poleXs = pole.filter((v) => v[3] === 'deck').map((v) => v[0]);
  return { hood: hood - floor, side: Math.max(...side) - Math.min(...side), pole: Math.max(...poleXs) - Math.min(...poleXs) };
}
const hullOk = (h) => h.hood >= 0.45 && h.side >= 0.3;
const results = {};
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const sit = await handling(backend, 0), stand = await handling(backend, 1), ctl = await handling(backend, -1);
  results[backend] = { sit, stand };
  const row = (r) => `top ${f2(r.top)} m/s, planes in ${f2(r.plane)} s, full-lock radius ${f2(r.radius)} m at ${f2(r.turnV)} m/s, ${r.subs} submarines, ${r.poles} pole stands, ${r.spills} spills`;
  console.log(`sit-down: ${row(sit)}`);
  console.log(`stand-up: ${row(stand)}`);
  console.log(`control (stand-up asked, craft not set): ${row(ctl)}`);
  const differ = (a, b) => ({
    slower: b.top < a.top * 0.92,
    later: b.plane > a.plane + 0.3,
    tighter: b.radius < a.radius * 0.8,
    tricks: b.subs >= 1 && b.poles >= 1 && a.subs === 0 && a.poles === 0,
  });
  const d = differ(sit, stand), dc = differ(sit, ctl);
  check('the stand-up is slower on the straight', d.slower, `${f2(stand.top)} against ${f2(sit.top)} m/s`);
  check('the stand-up planes later (only at speed)', d.later, `${f2(stand.plane)} s against ${f2(sit.plane)} s`);
  check('the stand-up turns far tighter', d.tighter, `${f2(stand.radius)} m against ${f2(sit.radius)} m`);
  check('a submarine and a pole stand: the stand-up only', d.tricks, `stand-up ${stand.subs} and ${stand.poles}, sit-down ${sit.subs} and ${sit.poles}`);
  check('no wipeout in the handling run', sit.spills === 0 && stand.spills === 0, `${sit.spills} and ${stand.spills}`);
  check('the stand-up\'s tricks rumble the pad', stand.trickRumble >= 2, `${stand.trickRumble} trick rumbles`);
  check('control: a run that never set the craft shows none of the differences', !dc.slower && !dc.later && !dc.tighter && !dc.tricks, JSON.stringify(dc));
  const lk = await look(backend), lc = await look(backend, ['sw stand_old 1']), ld = await look(backend, ['sw stand_old 2']);
  const lrow = (r) => `head ${f2(r.head)} m over the tray, front knee ${r.knee.toFixed(0)} deg, torso ${r.lean.toFixed(0)} deg from upright, hull ${f2(r.ahead)} m ahead of the front foot; on the straight (${r.frames} frames) craft/rider screen area at least ${f2(r.ratioMin)}, pole seen at least ${(r.poleMin * 100).toFixed(0)} %; in the hard turn head ${f2(r.turnHead)}, knee ${r.turnKnee.toFixed(0)}, torso ${r.turnLean.toFixed(0)}, craft/rider ${f2(r.turnRatio)}, pole ${(r.turnPole * 100).toFixed(0)} %`;
  console.log(`look: ${lrow(lk)}`);
  console.log(`old upright pose (control): ${lrow(lc)}`);
  check('a stand-up rider from the race camera: knees a little bent (130 to 162 deg), torso 12 to 30 deg forward, 1.4 m of hull ahead of the feet, the bars always seen beside the hands (2 of the 7 points of pole and grips)', lookOk(lk), lrow(lk));
  check('the number plate\'s numeral at least 24 px tall from the race camera (1080p)', lk.platePx >= 24, `${lk.platePx.toFixed(1)} px`);
  check('control: stiff and dead upright fails it', !lookOk(lc), `knee ${lc.knee.toFixed(0)}, torso ${lc.lean.toFixed(0)}`);
  check('control: a deep crouch fails it', !lookOk(ld), `knee ${ld.knee.toFixed(0)}, torso ${ld.lean.toFixed(0)}`);
  const hl = hullOf(join(ROOT, 'games/jetski/assets/models/standup.glb')), hb = hullOf(join(ROOT, 'tests/jetski-standup-blade.glb'));
  const polePx = hl.pole * lk.pxPerM;
  check('the hull: a hood standing 0.45 m over the tray floor, a side 0.3 m deep, a pole 4 px thick or more from the race camera', hullOk(hl) && polePx >= 4, `hood ${f2(hl.hood)} m, side ${f2(hl.side)} m, pole ${f2(hl.pole)} m = ${polePx.toFixed(1)} px`);
  check('control: the first, blade-like hull fails it', !hullOk(hb), `hood ${f2(hb.hood)} m, side ${f2(hb.side)} m`);
  const rs = await ride(backend, 1), rsit = await ride(backend, 0);
  console.log(`stand-up ride: ${JSON.stringify(rs)}`);
  console.log(`sit-down ride: ${JSON.stringify(rsit)}`);
  check('boots on the tray, gloves on the grips (every racer, every frame)', rs.frames > 1000 && rs.foot < 0.01 && rs.hands < 0.01, `worst boot ${(rs.foot * 100).toFixed(2)} cm, glove ${(rs.hands * 100).toFixed(2)} cm over ${rs.frames} frames`);
  check('the hull on the water while it rides it', rs.ticks > 1000 && rs.hi < 0.3 && rs.lo > -0.47, `middle ${f2(rs.lo)} to ${f2(rs.hi)} m against the surface over ${rs.ticks} racer ticks`);
  /* a shake is the camera thrown one way and back the next tick, tick after tick: under 3 % of ticks (a landing does it once or twice), about as often as the sit-down's */
  const shake = (r) => r.flips / Math.max(1, r.camTicks);
  check('no shake: the chase camera never shudders', rs.camTicks > 1500 && shake(rs) < 0.03 && rs.flips <= rsit.flips * 2 + 10, `${rs.flips} reversals in ${rs.camTicks} ticks (sit-down ${rsit.flips}), worst acceleration ${rs.cam.toFixed(0)} m/s^2 (sit-down ${rsit.cam.toFixed(0)})`);
  check('the field rides it: no rival stuck', rs.aiStuck < 120, `${rs.aiStuck} ticks stuck at most, ${rs.aiSpills} rival spills`);
  const cf = await ride(backend, 1, ['sw stand_hips 0.4']);
  check('control: hips lifted 0.4 m take the boots off the tray', cf.foot > 0.05, `worst boot ${(cf.foot * 100).toFixed(1)} cm`);
  const ch = await ride(backend, 1, ['ride_top 0']);
  check('control: a hull held by its spring alone goes under on big faces', ch.lo <= -0.47, `deepest ${f2(ch.lo)} m`);
  const cc = await ride(backend, 1, ['sw cam_jitter 1']);
  check('control: a camera that shakes fails the bound', shake(cc) >= 0.03, `${cc.flips} reversals in ${cc.camTicks} ticks`);
}
if (BACKENDS.length === 2) {
  /* (the two runs start a few frames apart on the waves, so their numbers are close, not the same bits) */
  const a = results.gl.stand, b = results.webgpu.stand;
  check('GL and WebGPU ride the same stand-up', Math.abs(a.top - b.top) < 0.5 && Math.abs(a.radius - b.radius) < 0.5 && Math.abs(a.plane - b.plane) < 0.2, `top ${f2(a.top)} / ${f2(b.top)}, radius ${f2(a.radius)} / ${f2(b.radius)}, plane ${f2(a.plane)} / ${f2(b.plane)}`);
}
/* --baseline <jetski_auto.wasc built from main>: the sit-down is the old craft to the bit. The computer race 1800 frames
   from power-on on both carts, deterministic loads, the sim's hash compared; control: the new cart with every racer on the
   stand-up must differ */
const BASE = arg('--baseline', '');
if (BASE) {
  const auto = join(dirname(CART), 'jetski_auto.wasc'), h = {};
  for (const [name, cart, pre] of [['main', resolve(BASE), ''], ['sit-down', auto, ''], ['stand-up', auto, 'craft 1']]) {
    await tool('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', deterministicSeed: 1, deterministicStepMs: 1000 / 60 });
    await step(2);
    if (pre) await cmd(pre);
    await step(1798);
    const s = await state();
    h[name] = `${s['race.hash']} at tick ${s['race.tick']}`;
  }
  check('the sit-down is the old craft to the bit', h.main === h['sit-down'], `main ${h.main}, now ${h['sit-down']}`);
  check('control: the stand-up field races otherwise', h['stand-up'] !== h['sit-down'], h['stand-up']);
}
console.log(`romdev requests: ${calls}`);
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
