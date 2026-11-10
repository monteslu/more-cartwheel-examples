// What the weather costs the GPU, through romdev's frame({op:'timing'}) on the built cart (the engine's
// tools/lib/romdev-timing.mjs): a race on each course in its usual weather against the same moment calm, in a storm, and
// in a storm with the course's big waves going through. The budget is the game as it was: no weather may cost over 10 %
// (and 0.6 ms) more than the course in its usual weather. High, 1080p, the server's pinned test GPU. Control: the
// same comparison with a 2 ms frame added to the storm (the cart's `sw` cannot add work, so the control is the check's
// arithmetic run on a storm with its mean raised by 2 ms; it must fail).
//   node tests/jetski-weather-perf.mjs [--gl] [--webgpu] [--courses 0,4]
import { join } from 'node:path';
import { ROOT, BACKENDS, arg } from './lib/jetski-romdev.mjs';
import { romdevTiming } from '../deps/cartwheel-engine/tools/lib/romdev-timing.mjs';

const CART = join(ROOT, 'build/cart-wgpu/carts/jetski.wasc');
const COURSES = arg('--courses', '0,4').split(',').map(Number);
let fail = 0;
const check = (n, ok, d) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${n}: ${d}`); if (!ok) fail++; };
const over = (base, t) => t > base * 1.1 && t - base > 0.6;
for (const gpu of BACKENDS) for (const c of COURSES) {
  const time = async (extra, after = []) => {
    /* a run another process shared the GPU with is run again, up to three times; the quietest counts */
    let best = Infinity;
    for (let k = 0; k < 3; k++) {
      const t = await romdevTiming({ cart: CART, gpu, commands: ['quality high', `course ${c}`, ...extra, 'race', 'sw autosteer 1', 'no_dq 1', 'wxsw event 0', 'go 900', ...after], warmup: 420, frames: 120, window: 120 });
      best = Math.min(best, t.gpuMs.mean);
      if (!t.contention?.noisy) break;
      console.log(`  (noisy run, again: ${(t.contention.reasons || []).join('; ').slice(0, 120)})`);
    }
    return best;
  };
  const base = await time(['weather -1']), calm = await time(['weather 0']), storm = await time(['weather 2']);
  const ev = await time(['weather 2'], ['event']);
  const f = (v) => v.toFixed(2);
  check(`${gpu} course ${c}: the weather keeps to the budget`, !over(base, calm) && !over(base, storm) && !over(base, ev), `GPU ms a frame: usual ${f(base)}, calm ${f(calm)}, storm ${f(storm)}, storm with the big waves ${f(ev)}`);
  check(`${gpu} course ${c} control: 2 ms more must fail`, over(base, storm + 2), `${f(storm + 2)} against ${f(base)}`);
}
process.exit(fail ? 1 : 0);
