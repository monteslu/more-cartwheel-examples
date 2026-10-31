// From the press of Start on the join screen to play the picture keeps moving: no frame over the target, per game, on the
// cart through romdev with real pad input. Frames are timed in the wall clock around one frame step each, with no state
// read inside the window; the median of 3 loads per frame index, so one stall on a shared machine does not decide it.
//
//   node tests/start-press-frames.mjs [game ...] [--gpu gl|webgpu] [--target ms]
//
// Control that must fail: the same meter over the load's own first frames must see a frame over the target. A meter
// that reports nothing there could not see a stall after the press either.
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const gpu = opt('--gpu', 'gl'), target = +opt('--target', 100);
const games = argv.filter((a, i) => !a.startsWith('--') && !argv[i - 1]?.startsWith('--'));
const session = `start-press-${process.pid}`;
async function call(tool, a) {
  const res = await fetch(`http://127.0.0.1:7331/tool/${tool}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': session }, body: JSON.stringify(a) });
  const t = await res.text();
  let b; try { b = JSON.parse(t); } catch { b = t; }
  if (!res.ok || (b && b.isError)) throw new Error(`${tool}: ${t.slice(0, 400)}`);
  return b;
}
const step = async () => { const t = performance.now(); await call('frame', { op: 'step', frames: 1 }); return performance.now() - t; };
const fields = async () => Object.fromEntries(((await call('wasm', { op: 'debugState' })).fields || []).map((f) => [f.name, f.value]));
const median = (a) => [...a].sort((x, y) => x - y)[a.length >> 1];
let failed = 0;
for (const g of games.length ? games : ['hordeshooter', 'brawler']) {
  const runs = [], load = [];
  for (let r = 0; r < 3; r++) {
    await call('loadMedia', { platform: 'wasmcart', path: join(ROOT, 'build/cart-wgpu/carts', `${g}.wasc`), gpuPower: 'low-power', wgpu: gpu === 'webgpu' });
    let longest = 0, s = await fields();
    for (let n = 0; !(s['cw.tick'] > 0) && n < 5000; n++) { longest = Math.max(longest, await step()); s = await fields(); }
    load.push(longest);
    for (let i = 0; i < 20; i++) await step();
    await call('input', { op: 'set', ports: [{ start: true }] }); await step(); await step();
    await call('input', { op: 'set', ports: [{}] });
    for (let i = 0; i < 20; i++) await step();
    if ((await fields())['shell.screen'] !== 2) throw new Error(`${g}: Start on the title did not open the join screen`);
    await call('input', { op: 'set', ports: [{ start: true }] });
    const f = [];
    for (let i = 0; i < 60; i++) { f.push(await step()); if (i === 1) await call('input', { op: 'set', ports: [{}] }); }
    s = await fields();
    if (s['shell.screen'] !== 3) throw new Error(`${g}: Start on the join screen did not reach play (screen ${s['shell.screen']})`);
    runs.push(f);
    await call('host', { op: 'shutdown' });
  }
  const m = runs[0].map((_, i) => median(runs.map((a) => a[i])));
  const worst = Math.max(...m);
  const ok = worst <= target;
  console.log(`${ok ? 'PASS' : 'FAIL'} ${g} ${gpu}: longest frame from the press to play ${worst.toFixed(0)} ms (target ${target}), first frames ${m.slice(0, 5).map((v) => v.toFixed(0)).join(' ')}`);
  if (!ok) failed++;
  if (median(load) <= target) { console.log(`CONTROL PASSED ${g} ${gpu}: the load's own frames peaked at ${median(load).toFixed(0)} ms, so the meter saw no stall where one must be`); failed++; }
  else console.log(`control failed as it must: the load's longest frame ${median(load).toFixed(0)} ms is over ${target}`);
}
process.exit(failed ? 1 : 0);
