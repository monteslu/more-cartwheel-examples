// The flash sweep: no game strobes. Each entry of tests/flash-sweep.json is a stretch of real play through a game's
// brightest moments (explosions, hits, weapons, pickups, weather, menus, transitions), on the fixed clock with every
// frame captured, and held to the photosensitive flash rules by the engine's tools/lib/flash.mjs (WCAG 2.2 success
// criterion 2.3.1 and the Harding test's thresholds: at most three flashes and three red flashes a second over a sixth
// of the screen, the same for small lights switching together, no regular flicker above 3 Hz). Every entry also runs
// its CONTROL, which must fail: the same frames with an 8 Hz full-screen strobe laid over them.
//
// Hosts: romdev (the default) runs the built cart (build/cart-wgpu/carts/<app>.wasc) on 127.0.0.1:7331, GL and WebGPU,
// each entry's frames taken in ONE frame({op:'record'}) call (box-filtered by 8 in linear light into a CW_FRAME_DUMP
// file, the layout a native run writes), never a request a frame. --host native runs the native build through the
// engine's tools/flash-check.mjs, as before.
//
// An entry names a gate of tests/*gates*.json (its app, input script, start requests and environment) and how many
// frames to check, or gives those itself: { "name", "gate" | "app", "frames", "from"?, "input"?, "startCmds"?, "env"?,
// "gpu"?, "maxRise"?, "knownFail"?, "mustFail"? } (maxRise: also bound the hardest two-frame rise, for a moment that must swell, such as
// lightning; knownFail: a failure waiting on a fix outside this repository, reported as KNOWN and not counted, and reported
// again when it starts to pass so the mark comes off; mustFail: a control of the game's own, the old behaviour a fix
// replaced, which must still fail on every backend or the entry it stands beside proves nothing).
//   node tests/flash-sweep.mjs [name ...] [--gpu gl|webgpu|both] [--keep]    (romdev; default both backends)
//   node tests/flash-sweep.mjs [name ...] --host native [--keep]
// (--keep: each run's frames stay in out/flash/<name>[.<gpu>].cwfd)
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync, rmSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const argv = process.argv.slice(2), opt = (k, d) => (argv.includes(`--${k}`) ? argv[argv.indexOf(`--${k}`) + 1] : d);
const valued = new Set(['--host', '--gpu']);
const only = argv.filter((a, i) => !a.startsWith('--') && !valued.has(argv[i - 1])), keep = argv.includes('--keep');
const host = opt('host', 'romdev'), gpuArg = opt('gpu', 'both');
if (!['romdev', 'native'].includes(host)) { console.log('FAIL --host romdev|native'); process.exit(2); }
const gpus = gpuArg === 'both' ? ['gl', 'webgpu'] : [gpuArg];

const gates = new Map();
for (const f of readdirSync(join(ROOT, 'tests')).filter((n) => /gates.*\.json$/.test(n)))
  for (const g of JSON.parse(readFileSync(join(ROOT, 'tests', f), 'utf8'))) if (g.app) gates.set(g.name, g);
const entries = JSON.parse(readFileSync(join(ROOT, 'tests/flash-sweep.json'), 'utf8')).filter((e) => !only.length || only.includes(e.name));
if (!entries.length) { console.log('FAIL no sweep entries (tests/flash-sweep.json, or the names given)'); process.exit(1); }

// ── romdev ──
const URL = process.env.ROMDEV_URL || 'http://127.0.0.1:7331';
async function call(session, tool, args) {
  const r = await fetch(`${URL}/tool/${tool}`, { method: 'POST', body: JSON.stringify(args), signal: AbortSignal.timeout(1_800_000),
    headers: { 'Content-Type': 'application/json', 'x-romdev-session': session, 'x-romdev-agent': process.env.ROMDEV_AGENT || 'flash-sweep' } });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (j && Array.isArray(j.content)) { const c = j.content.find((x) => x.type === 'text'); try { j = JSON.parse(c.text); } catch { j = c ? c.text : j; } }
  if (!r.ok || (j && (j.isError || j.error))) throw new Error(`romdev ${tool}: ${(typeof j === 'string' ? j : JSON.stringify(j)).slice(0, 800)}`);
  return j;
}
const envQuote = (v) => (/^[A-Za-z0-9_.:/,+-]*$/.test(v) ? v : `"${v.replace(/(["\\])/g, '\\$1')}"`);
/** a gate input script (run-cart's JSON) as frame({op:'record'}) steps from frame `at` + 1 to `last`; pads only */
function padSteps(script, at, last) {
  const pad = (s) => {
    const p = {};
    for (const b of s.buttons || []) p[b === 'back' ? 'select' : b] = true;
    if (s.axes) p.axes = s.axes;
    const ports = [p];
    for (const k of ['pad2', 'pad3', 'pad4']) {
      if (!s[k]) break;
      const q = {};
      for (const b of s[k].buttons || []) q[b === 'back' ? 'select' : b] = true;
      if (s[k].axes) q.axes = s[k].axes;
      ports.push(q);
    }
    return ports;
  };
  if (script.some((s) => s.keys || s.mouse || s.touches || s.text || s.wheel != null))
    throw new Error('the input script has keys, a pointer or text, which a romdev range cannot carry: give the entry a pad script or run it --host native');
  const live = script.filter((s) => s.frame <= last), steps = [];
  if (!live.length) return steps;
  if (live[0].frame - 1 > at) steps.push({ input: { ports: [{}] }, frames: live[0].frame - 1 - at });
  live.forEach((s, i) => {
    const from = Math.max(s.frame - 1, at), to = i + 1 < live.length ? Math.max(live[i + 1].frame - 1, at) : last;
    if (to > from) steps.push({ input: { ports: pad(s) }, frames: to - from });
  });
  return steps;
}
async function romdevRun(e, g, gpu) {
  const app = e.app || g.app, frames = e.frames ?? g.frames;
  let from = e.from ?? 2;
  const input = e.input ?? g.input, start = e.startCmds ?? g.startCmds ?? [], env = { ...(g.env || {}), ...(e.env || {}) };
  const cart = join(ROOT, 'build/cart-wgpu/carts', `${app}.wasc`);
  if (!existsSync(cart)) throw new Error(`no cart ${cart}: scripts/build.sh ${app}`);
  const session = `flash-sweep-${e.name}-${gpu}-${process.pid}`;
  const { wgpuPin } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
  const dir = join(ROOT, 'out/flash');
  mkdirSync(dir, { recursive: true });
  const dump = join(dir, `${e.name}.${gpu}.cwfd`);
  rmSync(dump, { force: true });
  try {
    const l = await call(session, 'loadMedia', { platform: 'wasmcart', path: cart, deterministicSeed: 1, deterministicStepMs: 1000 / 60, gpuPower: wgpuPin().power, wgpu: gpu === 'webgpu' });
    if (l.gpu !== gpu) throw new Error(`asked for ${gpu}, the cart runs on ${l.gpu}`);
    // start requests as a gate's romdev leg sends them: one a frame from frame 2, the environment riding on the first
    let at = 1;
    const plain = start.filter((c) => !c.startsWith('@')), timed = start.filter((c) => c.startsWith('@'));
    const envReq = Object.keys(env).length ? 'env ' + Object.entries(env).map(([k, v]) => `${k}=${envQuote(String(v))}`).join(' ') : null;
    const cmds = envReq ? [plain.length ? `${envReq} -- ${plain[0]}` : envReq, ...plain.slice(1)] : plain;
    for (const c of cmds) { await call(session, 'wasm', { op: 'command', text: c }); at++; }
    /* the start requests ran frames 2 to at: the range starts after them (a native run answers them inside frame 2) */
    if (e.from != null && from - 1 < at) throw new Error(`from ${from}: the start requests run frames 2 to ${at}`);
    from = Math.max(from, at + 1);
    const warmup = from - 1 - at, n = frames - from + 1;
    const commandsAt = timed.map((c) => { const m = c.match(/^@(\d+)\s+(.*)$/); return { frame: +m[1] - 1 - at, command: m[2] }; });
    if (commandsAt.some((c) => c.frame < 0)) throw new Error('a timed start request falls before the range');
    const steps = input ? padSteps(JSON.parse(readFileSync(join(ROOT, 'tests', input), 'utf8')), at, frames) : null;
    const r = await call(session, 'frame', { op: 'record', frames: n, warmup, path: dump, format: 'raw', downscale: 8, maxSeconds: 1800,
      ...(commandsAt.length ? { commandsAt } : {}), ...(steps && steps.length ? { steps } : {}) });
    if (r.truncated || r.frames !== n) throw new Error(`the range came back with ${r.frames} of ${n} frames${r.truncated ? ` (${JSON.stringify(r.truncated)})` : ''}`);
    return { dump, note: `romdev ${gpu}, ${r.source ? r.source.join('x') : '?'} frames from ${r.firstFrame}, analysed at ${r.width}x${r.height}` };
  } finally {
    await call(session, 'host', { op: 'shutdown' }).catch(() => {});
  }
}

let bad = 0, known = 0, runs = 0;
const { flashGate, readFrameDump } = await import(join(ENGINE, 'tools/lib/flash.mjs'));
if (host === 'native') {
  const { takeSuiteLock } = await import(join(ENGINE, 'tools/lib/suite-lock.mjs'));
  const { enterGpuSandbox } = await import(join(ENGINE, 'tools/lib/gpu-sandbox.mjs'));
  await enterGpuSandbox();
  await takeSuiteLock('flash-sweep');
}
for (const e of entries) {
  const g = e.gate ? gates.get(e.gate) : {};
  if (!g) { console.log(`FAIL ${e.name}: no gate named ${e.gate}`); bad++; continue; }
  const legs = host === 'native' ? [e.gpu || 'gl'] : e.gpu ? [e.gpu] : gpus;
  let failed = 0, errors = 0;
  for (const gpu of legs) {
    runs++;
    const label = `${e.name} (${host} ${gpu})`;
    let ok;
    if (host === 'native') {
      const app = e.app || g.app, input = e.input ?? g.input, start = e.startCmds ?? g.startCmds, env = { ...(g.env || {}), ...(e.env || {}) };
      const args = [join(ENGINE, 'tools/flash-check.mjs'), '--host', 'native', '--app', app, '--frames', String(e.frames ?? g.frames), '--from', String(e.from ?? 2),
        '--label', label, '--gpu', gpu];
      if (input) args.push('--input', join(ROOT, 'tests', input));
      if (start && start.length) args.push('--start-cmds', start.join(';'));
      if (e.maxRise != null) args.push('--max-rise', String(e.maxRise));
      if (keep) args.push('--keep', join(ROOT, 'out/flash', `${e.name}.cwfd`));
      const r = spawnSync('node', args, { encoding: 'utf8', env: { ...process.env, ...env, CW_PROJECT_ROOT: ROOT } });
      process.stdout.write(r.stdout || '');
      if (r.status !== 0 && r.stderr) process.stdout.write(r.stderr.slice(-1500));
      ok = r.status === 0;
    } else {
      try {
        const t0 = Date.now(), { dump, note } = await romdevRun(e, g, gpu);
        console.log(`  ${label}: ${note} in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
        const d = await readFrameDump(dump);
        const res = flashGate(d.w, d.h, d.frames, e.maxRise != null ? { maxRise: e.maxRise } : {}, { label });
        for (const line of res.lines) console.log(line);
        ok = res.ok;
        if (!keep) rmSync(dump, { force: true });
      } catch (err) {
        console.log(`FAIL ${label}: ${err.message}`);
        ok = false, errors++;
      }
    }
    if (!ok) failed++;
  }
  if (e.mustFail) {
    console.log(failed === legs.length && !errors ? `ok   ${e.name} fails as it must (${e.mustFail})` : `FAIL ${e.name} passed on ${legs.length - failed} of ${legs.length} backends: a control that must fail (${e.mustFail})`);
    if (failed !== legs.length || errors) bad++;
    continue;
  }
  if (e.knownFail) {
    console.log(failed ? `KNOWN ${e.name} fails, waiting on: ${e.knownFail}` : `NOTE ${e.name} passes now: take its knownFail mark off`);
    if (failed) known++;
    continue;
  }
  if (failed) bad++;
}
console.log(bad ? `FAIL flash sweep: ${bad} of ${entries.length} entries` : `ok   flash sweep: ${entries.length - known} entries (${runs} runs), none strobes, every control fails${known ? ` (${known} known failure(s) waiting on fixes elsewhere)` : ''}`);
process.exit(bad ? 1 : 0);
