// coopplatformer: every gate's control really breaks the thing the gate measures. A control the gate runner runs one frame
// short "fails" on g.tick and on the picture alone, whatever it does, so a control that had stopped doing anything would go
// unnoticed. Here each control runs for the gate's own frames, and the field the gate is about (its "controlField") must differ
// from the value the gate expects. The check of this check: a control that changes nothing must be reported.
//   node tests/coopplatformer-controls.mjs [gate-name ...]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const OUT = join(ROOT, 'out', 'controls-gate');
mkdirSync(OUT, { recursive: true });
const only = process.argv.slice(2);
const gates = JSON.parse(readFileSync(join(ROOT, 'tests/coopplatformer-gates.json'), 'utf8'));

/** @returns {unknown} the field after a run of the gate with the given environment and input */
function field(g, env, input, name) {
  const state = join(OUT, `${g.name}.json`);
  rmSync(state, { force: true });
  const args = [join(ENGINE, 'tools/run-cart.mjs'), '--host', 'native', '--app', g.app, '--frames', String(g.frames), '--state', state, '--shot', join(OUT, `${g.name}.png`)];
  if (input) args.push('--input', join(ROOT, 'tests', input));
  const r = spawnSync('node', args, { cwd: ENGINE, encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, CW_PROJECT_ROOT: ROOT, ...(g.env || {}), ...env } });
  if (r.status !== 0) throw new Error(`${g.name}: run failed: ${(r.stderr || r.stdout).slice(-400)}`);
  return JSON.parse(readFileSync(state, 'utf8'))[name];
}
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

let bad = 0;
let seen = 0;
for (const g of gates) {
  if (!g.controlField || (only.length && !only.includes(g.name))) continue;
  seen++;
  const want = g.expect?.[g.controlField];
  if (want === undefined) { console.log(`FAIL ${g.name}: its controlField ${g.controlField} has no expected value`); bad++; continue; }
  if (g.controlFrames !== undefined && g.controlFrames !== g.frames) { console.log(`FAIL ${g.name}: its control runs ${g.controlFrames} frames, not the gate's ${g.frames}`); bad++; continue; }
  const got = field(g, g.controlEnv || {}, g.controlInput || g.input, g.controlField);
  const good = !same(got, want);
  if (!good) bad++;
  console.log(`${good ? 'ok  ' : 'FAIL'} ${g.name}: control ${Object.keys(g.controlEnv || {}).join(' ') || g.controlInput}: ${g.controlField} ${JSON.stringify(got)}, the gate expects ${JSON.stringify(want)}`);
}
if (only.length ? !seen : seen < 10) { console.log(`FAIL only ${seen} gates name a controlField`); bad++; }
if (!only.length) {
  const g = gates.find((x) => x.name === 'coopplatformer-scenery');
  const got = field(g, {}, null, g.controlField);
  if (same(got, g.expect[g.controlField])) console.log(`ok   this check: no control at all leaves ${g.name}'s ${g.controlField} at ${JSON.stringify(got)}, which would be reported`);
  else { console.log(`FAIL this check: ${g.name} with no control gives ${g.controlField} ${JSON.stringify(got)}, not ${JSON.stringify(g.expect[g.controlField])}`); bad++; }
}
rmSync(OUT, { recursive: true, force: true });
console.log(bad ? `FAIL coopplatformer-controls: ${bad}` : `ok   coopplatformer-controls: ${seen} controls`);
process.exit(bad ? 1 : 0);
