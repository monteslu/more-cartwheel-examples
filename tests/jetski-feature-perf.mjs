// What the 0.44 features cost the GPU, through romdev's frame({op:'timing'}) on the built cart (the engine's
// tools/lib/romdev-timing.mjs), High at 1080p, against the race at speed on the same course as the baseline:
//   podium  the podium after the line (the pier, the three parked craft, the camera round them): within 10 % (and 0.6 ms)
//           of the race at speed
//   split   two players side by side, both views chased and drawn: under 16.7 ms a frame in the reference GPU's units
//           (60 frames a second on the 890M; the lane's timing scale maps the run there), and the single view as before
// On a lane with a timing scale the reference-GPU numbers are printed beside the raw ones. Control: a podium
// 2 ms past the race's budget must fail (the arithmetic, as jetski-weather-perf does); the split run must have
// drawn two views and the one-player run one (view.views, read in the runs).
//   node tests/jetski-feature-perf.mjs --case podium|split [--gl] [--webgpu] [--courses 0,3]
import { join } from 'node:path';
import { ROOT, BACKENDS, arg } from './lib/jetski-romdev.mjs';
import { romdevTiming, timingScale } from '../deps/cartwheel-engine/tools/lib/romdev-timing.mjs';

const CART = join(ROOT, 'build/cart-wgpu/carts/jetski.wasc');
const CASE = arg('--case', 'podium');
const COURSES = arg('--courses', '0,3').split(',').map(Number);
let fail = 0;
const check = (n, ok, d) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${n}: ${d}`); if (!ok) fail++; };
const f2 = (v) => (+v).toFixed(2);
/* the reference GPU's ms for a raw mean on this lane, or the raw mean when the lane has no scale */
const ref = (gpu, ms) => { const s = timingScale(gpu); return s && s.b ? (ms - (s.a || 0)) / s.b : ms; };
/* the views the pipeline drew, read back from the run's debug fields (view.views) */
const viewsOf = (t) => { const m = JSON.stringify(t).match(/"view\.views"[^0-9]*([0-9]+)/); return m ? +m[1] : -1; };
let lastViews = -1;
const time = async (gpu, commands, warmup = 420) => {
  let best = Infinity;
  for (let k = 0; k < 3; k++) {
    const t = await romdevTiming({ cart: CART, gpu, commands: ['quality high', ...commands], warmup, frames: 120, window: 120, debugFields: ['view.views'] });
    best = Math.min(best, t.gpuMs.mean);
    lastViews = viewsOf(t);
    if (!t.contention?.noisy) break;
    console.log(`  (noisy run, again: ${(t.contention.reasons || []).join('; ').slice(0, 120)})`);
  }
  return best;
};
const over = (base, t) => t > base * 1.1 && t - base > 0.6;
for (const gpu of BACKENDS) for (const c of COURSES) {
  const race = await time(gpu, [`course ${c}`, 'mode 0', 'sw players 1', 'race', 'sw autosteer 1', 'no_dq 1', 'wxsw event 0', 'go 900']);
  if (CASE === 'podium') {
    const pod = await time(gpu, [`course ${c}`, 'mode 0', 'sw players 1', 'race', 'no_dq 1', 'wxsw event 0', 'go 900', 'finish'], 620);
    check(`${gpu} course ${c}: the podium costs no more than the race`, !over(race, pod), `race ${f2(race)} ms, podium ${f2(pod)} ms (reference GPU: ${f2(ref(gpu, race))}, ${f2(ref(gpu, pod))})`);
    /* control: the bound itself, a podium 2 ms over the race's budget must fail it */
    const ctl = Math.max(race * 1.1, race + 0.6) + 2;
    check(`${gpu} course ${c}: control, a podium 2 ms past the budget must fail`, over(race, ctl), `${f2(ctl)} against ${f2(race)}`);
  } else if (CASE === 'split') {
    const oneViews = lastViews;
    const two = await time(gpu, [`course ${c}`, 'mode 0', 'sw players 2', 'race', 'sw autosteer 1', 'no_dq 1', 'wxsw event 0', 'go 900']);
    const r1 = ref(gpu, race), r2 = ref(gpu, two);
    check(`${gpu} course ${c}: two views at 60 frames a second on the reference GPU`, r2 < 16.7, `one view ${f2(race)} ms, two ${f2(two)} ms (reference GPU: ${f2(r1)}, ${f2(r2)}; ${f2(two / race)}x)`);
    /* the run measured what it says: two views drawn in it, one in the single player's (the control) */
    check(`${gpu} course ${c}: the split run drew two views, the single one (the control) one`, lastViews === 2 && oneViews === 1, `${lastViews} and ${oneViews}`);
  }
}
process.exit(fail ? 1 : 0);
