// The flash sweep: no game strobes. Each entry of tests/flash-sweep.json is a stretch of real play through a game's
// brightest moments (explosions, hits, weapons, pickups, weather, menus, transitions), run on the fixed clock with
// every frame checked against the photosensitive flash rules (WCAG 2.2 success criterion 2.3.1 and the Harding test's
// thresholds: at most three flashes and three red flashes a second over a sixth of the screen, the same for small
// lights switching together, no regular flicker above 3 Hz) by the engine's tools/flash-check.mjs. Every entry also
// runs its CONTROL, which must fail: the same frames with an 8 Hz full-screen strobe laid over them.
//
// The runs go through romdev (127.0.0.1:7331, ROMDEV_URL for another server): the built dual cart
// (build/cart-wgpu), loaded on GL and on WebGPU, run to the entry's first frame with the gate's start requests,
// environment and input, then ONE frame({op:'flash'}) call that steps the checked frames and analyses each one on the
// server. Never a request per frame. GL and WebGPU of one entry run side by side (two romdev hosts at a time).
//
// An entry names a gate of tests/*gates*.json (its app, input script, start requests and environment) and how many
// frames to check, or gives those itself: { "name", "gate" | "app", "frames", "from"?, "input"?, "startCmds"?, "env"?,
// "gpu"?, "maxRise"?, "knownFail"?, "mustFail"? } (gpu: only that backend; maxRise: also bound the hardest two-frame rise,
// for a moment that must swell, such as lightning; knownFail: a failure waiting on a fix outside this repository,
// reported as KNOWN and not counted, and reported again when it starts to pass so the mark comes off; mustFail: a
// control of the game's own, the old behaviour a fix replaced, whose run must fail the flash rules on every backend,
// with the strobe control failing too, or the entry it stands beside proves nothing).
//   node tests/flash-sweep.mjs [name ...] [--gpu gl|webgpu] [--keep] [--engine DIR] [--host native]
//     --keep: each run's frames stay in out/flash/<name>-<gpu>.cwfd; --engine: another engine checkout's tools (one
//     being worked on); --host native: the native build instead (CW_FRAME_DUMP, one backend at a time, the GPU lock)
import { spawn } from 'node:child_process';
import { readFileSync, readdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k) => (argv.includes(`--${k}`) ? argv[argv.indexOf(`--${k}`) + 1] : undefined);
const valued = new Set(['--gpu', '--engine', '--host']);
const only = argv.filter((a, i) => !a.startsWith('--') && !valued.has(argv[i - 1]));
const keep = argv.includes('--keep');
const ENGINE = opt('engine') ? resolve(opt('engine')) : join(ROOT, 'deps/cartwheel-engine');
const host = opt('host') || 'romdev';
const gpus = opt('gpu') ? [opt('gpu')] : host === 'romdev' ? ['gl', 'webgpu'] : ['gl'];
if (host === 'native') {
  const { takeSuiteLock } = await import(join(ENGINE, 'tools/lib/suite-lock.mjs'));
  const { enterGpuSandbox } = await import(join(ENGINE, 'tools/lib/gpu-sandbox.mjs'));
  await enterGpuSandbox();
  await takeSuiteLock('flash-sweep');
}

const gates = new Map();
for (const f of readdirSync(join(ROOT, 'tests')).filter((n) => /gates.*\.json$/.test(n)))
  for (const g of JSON.parse(readFileSync(join(ROOT, 'tests', f), 'utf8'))) if (g.app) gates.set(g.name, g);
const entries = JSON.parse(readFileSync(join(ROOT, 'tests/flash-sweep.json'), 'utf8')).filter((e) => !only.length || only.includes(e.name));
if (!entries.length) { console.log('FAIL no sweep entries (tests/flash-sweep.json, or the names given)'); process.exit(1); }

function run(args, env) {
  return new Promise((ok) => {
    const p = spawn('node', args, { env, stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', err = '';
    p.stdout.on('data', (d) => { out += d; });
    p.stderr.on('data', (d) => { err += d; });
    p.on('close', (status) => ok({ status, out, err }));
  });
}

let bad = 0, known = 0, runs = 0, requests = 0;
const table = [];
for (const e of entries) {
  const g = e.gate ? gates.get(e.gate) : {};
  if (!g) { console.log(`FAIL ${e.name}: no gate named ${e.gate}`); bad++; continue; }
  const app = e.app || g.app, input = e.input ?? g.input, start = e.startCmds ?? g.startCmds, env = { ...(g.env || {}), ...(e.env || {}) };
  const legs = await Promise.all(gpus.filter((gpu) => !e.gpu || e.gpu === gpu).map(async (gpu) => {
    const json = join(ROOT, 'out/flash', `${e.name}-${gpu}-${process.pid}.json`);
    const args = [join(ENGINE, 'tools/flash-check.mjs'), '--host', host, '--app', app, '--frames', String(e.frames ?? g.frames), '--from', String(e.from ?? 2),
      '--label', `${e.name} (${gpu})`, '--gpu', gpu, '--json', json];
    if (input) args.push('--input', join(ROOT, 'tests', input));
    if (start && start.length) args.push('--start-cmds', start.join(';'));
    if (Object.keys(env).length) args.push('--env', JSON.stringify(env));
    if (e.maxRise != null) args.push('--max-rise', String(e.maxRise));
    if (keep) args.push('--keep', join(ROOT, 'out/flash', `${e.name}-${gpu}.cwfd`));
    const r = await run(args, { ...process.env, CW_PROJECT_ROOT: ROOT, CW_ROMDEV_AGENT: process.env.CW_ROMDEV_AGENT || 'flash-sweep' });
    let j = null;
    try { j = JSON.parse(readFileSync(json, 'utf8')); } catch { /* the run failed before its result */ }
    rmSync(json, { force: true });
    return { gpu, ...r, j };
  }));
  for (const l of legs) {
    runs++;
    process.stdout.write(l.out || '');
    if (l.j && l.j.romdevCalls) requests += l.j.romdevCalls;
    const res = l.j && l.j.result;
    table.push({ name: e.name, gpu: l.gpu, ok: l.status === 0, flashes: res ? res.regions.flashes : null, red: res ? res.regionsRed.flashes : null,
      fields: res ? res.fields.flashes : null, controlFails: l.j && l.j.control ? l.j.control.failsAsItMust : null });
    if (e.mustFail) {
      /* the run itself must fail its bounds (not the run breaking, nor the strobe control going unseen) */
      const runFailed = !!(l.j && l.j.problems && l.j.problems.length && l.j.control && l.j.control.failsAsItMust);
      console.log(runFailed ? `ok   ${e.name} (${l.gpu}) fails as it must (${e.mustFail})` : `FAIL ${e.name} (${l.gpu}) did not fail its bounds: a control that must fail (${e.mustFail})`);
      if (!runFailed) bad++;
      table[table.length - 1].ok = runFailed, table[table.length - 1].mustFail = true;
      continue;
    }
    if (e.knownFail) {
      console.log(l.status !== 0 ? `KNOWN ${e.name} (${l.gpu}) fails, waiting on: ${e.knownFail}` : `NOTE ${e.name} (${l.gpu}) passes now: take its knownFail mark off`);
      if (l.status !== 0) known++;
      continue;
    }
    if (l.status !== 0) { bad++; if (l.err) process.stdout.write(l.err.slice(-1500)); }
  }
}
console.log('\nentry                              gpu     flashes/s  red  fields  control');
for (const t of table) console.log(`${t.ok ? 'ok  ' : 'FAIL'} ${(t.name + (t.mustFail ? ' (must fail)' : '')).padEnd(30)} ${t.gpu.padEnd(7)} ${String(t.flashes ?? '-').padStart(9)} ${String(t.red ?? '-').padStart(4)} ${String(t.fields ?? '-').padStart(7)}  ${t.controlFails == null ? '-' : t.controlFails ? 'fails' : 'PASSED'}`);
if (host === 'romdev') console.log(`romdev requests: ${requests} for ${runs} runs`);
console.log(bad ? `FAIL flash sweep: ${bad} of ${runs} runs` : `ok   flash sweep: ${runs - known} runs, none strobes, every control fails${known ? ` (${known} known failure(s) waiting on fixes elsewhere)` : ''}`);
process.exit(bad ? 1 : 0);
