// Where the water meets the racers (the engine's waterline part on every craft and rider, the players' foam skirts), on
// the cart through romdev, GL and WebGPU, High at 1080p. Each picture pair is one held moment (sw freeze 1: the sea and the
// race stand still), so the pictures differ by the switch alone:
//   rest     the human's craft at rest on the grid, seen from its side at the water (sw cam_side 3.2), held still: along the
//            hull's contact with the water the part (the meniscus line, the wet band, the hull's wet gloss) changes at least
//            1000 of the hull's pixels by more than 18 levels against the same moment without it (sw wl_part 0), and 3 x what
//            two shots with it differ by (the sea's ripples keep moving in a held moment). CONTROL: two more shots with the
//            part must not pass that bound
//   foam     with every foam on, the contact band at rest is at most 3 % white water (the reference photos show none at
//            rest). CONTROL: sw skirt 2 (the skirt at full white at any speed) floods it
//   speed    the race at speed (go 600), chase camera: the foam skirt along the human's hull is alive (fx.skirt_alive, at
//            least 30 particles; the pictures are kept to look at). CONTROL: sw skirt 0 has none
//   node tests/jetski-hull-wet.mjs [--gl] [--webgpu] [--course 0]
import { join } from 'node:path';
import { readImage } from '../deps/cartwheel-engine/tools/lib/png.mjs';
import { ROOT, BACKENDS, arg, client } from './lib/jetski-romdev.mjs';
const CART = join(ROOT, 'build/cart-wgpu/carts/jetski.wasc');
const C = client('jetski-hull-wet');
const course = arg('--course', '0');
async function shot(gpu, tag, frames = 2) {
  await C.run(frames);
  const p = join(C.OUT, `${gpu}-${tag}.png`);
  await C.shot(p);
  return readImage(p);
}
/* the hull's contact with the water in the side view (the rider's boots are above it): changed pixels where either picture
 * is the hull's grey or black (the teal sea, whose ripples keep moving in a held moment, is left out) */
const sat = (p, i) => Math.max(p.rgba[i], p.rgba[i + 1], p.rgba[i + 2]) - Math.min(p.rgba[i], p.rgba[i + 1], p.rgba[i + 2]);
const box = (a, b) => { let n = 0; for (let y = 525; y < 650; y++) for (let x = 420; x < 1520; x++) { const i = (y * a.w + x) * 4; if (sat(a, i) > 40 && sat(b, i) > 40) continue; const d = Math.abs(a.rgba[i] - b.rgba[i]) + Math.abs(a.rgba[i + 1] - b.rgba[i + 1]) + Math.abs(a.rgba[i + 2] - b.rgba[i + 2]); if (d > 18) n++; } return n; };
const shows = (d, noise) => d >= 1000 && d >= 3 * noise;
/* the share of the contact band that is white water (every channel at 200 or more): at rest the reference photos show none
 * (a calm hull's reflection meets it, jt2; an ankle in swash a 1 to 3 px rim, pd3), so at most 3 % */
const foam = (a) => { let n = 0, t = 0; for (let y = 525; y < 650; y++) for (let x = 420; x < 1520; x++) { const i = (y * a.w + x) * 4; t++; if (Math.min(a.rgba[i], a.rgba[i + 1], a.rgba[i + 2]) >= 200) n++; } return n / t; };
for (const gpu of BACKENDS) {
  await C.load(CART, gpu);
  for (const s of ['quality high', `course ${course}`, 'mode 0', 'sw players 1', 'race', 'sw cam_side 3.2', 'sw hud 0']) await C.cmd(s);
  await C.run(800); /* (the grid's first splash soaks every racer: dry by now) */
  await C.cmd('sw freeze 1');
  const on = await shot(gpu, 'rest-on', 4), on2 = await shot(gpu, 'rest-on2'), on3 = await shot(gpu, 'rest-on3');
  await C.cmd('sw wl_part 0');
  const off = await shot(gpu, 'rest-off', 4);
  const d = box(on, off), noise = box(on, on2), noise2 = box(on2, on3);
  C.check(`${gpu} rest: the waterline shows along the hull's contact with the water`, shows(d, noise), `${d} hull pixels changed against the part off (>= 1000 and >= 3 x the ${noise} of two shots with it)`);
  C.check(`${gpu} rest: control, two more shots with the part do not pass that bound`, !shows(noise2, noise), `${noise2} against ${noise}`);
  /* foam never hides the waterline: with every foam on (skirt, wakes, ripples) the contact band at rest is clear water */
  const fr = foam(on);
  C.check(`${gpu} rest: the contact band is not covered by foam`, fr <= 0.03, `${(fr * 100).toFixed(2)} % white (<= 3 %)`);
  await C.cmd('sw wl_part 1'); await C.cmd('sw skirt 2'); await C.cmd('sw freeze 0');
  const flood = foam(await shot(gpu, 'rest-flood', 60));
  C.check(`${gpu} rest: control, a skirt at full white at rest (sw skirt 2) floods it`, flood > 0.03, `${(flood * 100).toFixed(2)} % white`);

  for (const skirt of [1, 0]) {
    await C.load(CART, gpu);
    for (const s of ['quality high', `course ${course}`, 'mode 0', 'sw players 1', `sw skirt ${skirt}`, 'race', 'sw autosteer 1', 'no_dq 1', 'wxsw event 0', 'go 600', 'sw hud 0']) await C.cmd(s);
    await C.run(90);
    await shot(gpu, `speed-${skirt ? 'on' : 'off'}`, 1);
    const alive = await C.rd('fx.skirt_alive'), v = await C.rd('p0.speed');
    if (skirt) C.check(`${gpu} speed: the foam skirt along the hull at speed`, alive >= 30 && v > 15, `${alive} skirt particles alive at ${v.toFixed(1)} m/s (>= 30)`);
    else C.check(`${gpu} speed: control, sw skirt 0 has none`, alive === 0, `${alive}`);
  }
}
await C.bye(C.fails ? 1 : 0);
