// The plants move in the wind (the engine's foliage wind, cartwheel/foliage.h), on the built carts through romdev, GL and
// WebGPU. The camera holds still on a stand of plants and six frames six ticks apart are compared inside a box round
// them: the mean change a pixel must be well above what the same box shows with the plants held still (the control,
// which must fail the same bar).
//   - Jet ski: Crater Atoll's palms from the start line (peek holds the camera), in a calm and in a chop (no rain or
//     lightning in the box); control `sw sway 0`. The chop must sway more than the calm.
//   - Pirates: the title's island, palms and undergrowth; control `sw still_plants 1`.
//   - Tux racer: a shrub's crossed cards on a windy Bunny Hill (cam parks the camera, Tux hidden); control
//     `opt cardwind off`.
//   node tests/plants-wind.mjs [--gl] [--webgpu] [--only jetski,pirates,tuxracer]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { ROOT, BACKENDS, arg } from './lib/jetski-romdev.mjs';
import { readImage } from '../deps/cartwheel-engine/tools/lib/png.mjs';

const ONLY = arg('--only', 'jetski,pirates,tuxracer').split(',');
const OUT = join(ROOT, 'out', 'plants-wind');
mkdirSync(OUT, { recursive: true });
let fail = 0;
const check = (label, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}: ${detail}`); if (!ok) fail++; };

function client(session) {
  const H = { 'Content-Type': 'application/json', 'x-romdev-session': session, 'x-romdev-agent': 'plants-wind' };
  const tool = async (t, a) => {
    const r = await fetch(`${ROMDEV_URL}/tool/${t}`, { method: 'POST', headers: H, body: JSON.stringify(a) });
    const j = await r.json().catch(() => ({ error: 'not JSON' }));
    if (r.headers.get('x-romdev-session') !== session) { console.error(`romdev ${t}: the reply came from another session`); process.exit(3); }
    if (!r.ok || j.error) { console.error(`romdev ${t}: ${JSON.stringify(j).slice(0, 400)}`); process.exit(3); }
    return j;
  };
  const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
  return {
    tool, rd,
    cmd: (text) => tool('wasm', { op: 'command', text, maxFrames: 600 }),
    step: (frames) => tool('frame', { op: 'step', frames }),
    async load(cart, backend) {
      await tool('loadMedia', { platform: 'wasmcart', path: join(ROOT, 'build/cart-wgpu/carts', cart), gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
      await tool('frame', { op: 'step', frames: 1 });
      for (let k = 0; k < 200; k++) {
        const v = await rd('cw.loading');
        if (Array.isArray(v) ? Number(v[0]) >= Number(v[1]) : Number(v) === 0) break;
        await tool('frame', { op: 'step', frames: 30 });
      }
    },
    /** six frames six ticks apart: the mean change of a pixel (0..255) between neighbours inside the box */
    async motion(tag, [x, y, w, h]) {
      const imgs = [];
      for (let k = 0; k < 6; k++) {
        const p = join(OUT, `${tag}-${k}.png`);
        await tool('frame', { op: 'stepAndShot', frames: 6, path: p });
        imgs.push(readImage(p));
      }
      let sum = 0;
      for (let k = 1; k < imgs.length; k++) for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
        const o = ((y + j) * imgs[k].w + x + i) * 4;
        for (let c = 0; c < 3; c++) sum += Math.abs(imgs[k].rgba[o + c] - imgs[k - 1].rgba[o + c]);
      }
      return sum / (3 * w * h * (imgs.length - 1));
    },
    close: () => tool('host', { op: 'shutdown' }),
  };
}
const f2 = (v) => v.toFixed(2);

for (const backend of BACKENDS) {
  if (ONLY.includes('jetski')) {
    const R = client(`plants-wind-jetski-${backend}-${process.pid}`);
    await R.load('jetski.wasc', backend);
    const PALMS = [1020, 320, 460, 260];
    const ride = async (weather, sway) => {
      for (const c of [`sw sway ${sway}`, 'course 0', `weather ${weather}`, 'race', 'no_dq 1', 'wxsw event 0', 'peek -170 7 140 -117 6 90']) await R.cmd(c);
      await R.step(30);
      return R.motion(`jetski-${backend}-w${weather}-s${sway}`, PALMS);
    };
    const chop = await ride(1, 1), calm = await ride(0, 1), stillCalm = await ride(0, 0);
    /* a calm and a chop have no rain or lightning: the plants alone move in the box */
    const CALM_BAR = 0.1;
    check(`${backend} jetski: palms breathe in a calm`, calm > CALM_BAR, `${f2(calm)} a pixel (bar ${CALM_BAR})`);
    check(`${backend} jetski control: held still in a calm fails the bar`, !(stillCalm > CALM_BAR), `${f2(stillCalm)}`);
    check(`${backend} jetski: and sway harder in a chop`, chop > calm * 1.5, `chop ${f2(chop)}, calm ${f2(calm)}`);
    await R.close();
  }
  if (ONLY.includes('pirates')) {
    const R = client(`plants-wind-pirates-${backend}-${process.pid}`);
    await R.load('pirates.wasc', backend);
    await R.step(300); /* the title, its island held still under the menu */
    const ISLE = [490, 195, 200, 90];
    const sway = await R.motion(`pirates-${backend}-sway`, ISLE);
    await R.cmd('sw still_plants 1');
    await R.step(6);
    const still = await R.motion(`pirates-${backend}-still`, ISLE);
    const BAR = 0.8;
    check(`${backend} pirates: palms and undergrowth sway downwind`, sway > BAR, `${f2(sway)} a pixel (bar ${BAR})`);
    check(`${backend} pirates control: held still fails the bar`, !(still > BAR), `${f2(still)}`);
    await R.close();
  }
  if (ONLY.includes('tuxracer')) {
    const R = client(`plants-wind-tuxracer-${backend}-${process.pid}`);
    await R.load('tuxracer.wasc', backend);
    await R.step(200);
    await R.cmd('race bunny_hill sunny wind');
    await R.step(240);
    await R.cmd('tp 75.5 -13'); /* beside a shrub near the top (bunny_hill objects.json: tree3 at x 77.865, d 18.075) */
    await R.step(2);
    const p = (await R.rd('tux.pos')).map(Number);
    await R.cmd(`cam ${p[0] - 1} ${p[1] + 0.8} ${p[2] + 2} 77.865 ${p[1] - 0.4} -18.075`);
    await R.cmd('hide tux 1');
    const SHRUB = [880, 630, 160, 170];
    await R.step(10);
    const sway = await R.motion(`tuxracer-${backend}-sway`, SHRUB);
    await R.cmd('opt cardwind off');
    await R.step(10);
    const still = await R.motion(`tuxracer-${backend}-still`, SHRUB);
    const BAR = 0.25;
    check(`${backend} tuxracer: the shrub's cards sway`, sway > BAR, `${f2(sway)} a pixel (bar ${BAR})`);
    check(`${backend} tuxracer control: held still fails the bar`, !(still > BAR), `${f2(still)}`);
    await R.close();
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
