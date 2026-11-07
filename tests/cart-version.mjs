// Every cart says which version it is: its manifest's `version` (which
// cw_add_app works out from this checkout, the engine's
// tools/lib/app-version.mjs) is the version this build is.
//
//   node tests/cart-version.mjs [--expect <v>] [--dir <dir>] [game...]
//
// The version expected: --expect, else on a v* tag build (GITHUB_REF_TYPE=tag)
// the tag without its v, else what the engine's rule gives this checkout
// (<last tag>-dev.g<commit>). A tag build's version must be a release (no
// prerelease part). The carts: <dir>/<game>.wasc for each game named, else
// every .wasc in <dir> (default build/cart-wgpu/carts). Any other file in
// <dir> (a release's native packages) must carry the version in its name.
// CONTROL that must fail: a copy of the first cart with its manifest set to
// another version.
import { spawnSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const val = (k) => { const i = argv.indexOf(k); if (i < 0) return undefined; const v = argv[i + 1]; argv.splice(i, 2); return v; };
let expect = val('--expect');
const dir = resolve(val('--dir') || join(ROOT, 'build', 'cart-wgpu', 'carts'));
const games = argv;
const tag = process.env.GITHUB_REF_TYPE === 'tag' ? process.env.GITHUB_REF_NAME : null;
if (!expect && tag) expect = tag.replace(/^v/, '');
if (!expect) {
  // the engine's own rule (an SDK ships it under tools/lib, as does the engine's source)
  const lib = ['deps/cartwheel-sdk/cart-wgpu', 'deps/cartwheel-sdk/linux-x64', 'deps/cartwheel-engine']
    .map((d) => join(ROOT, d, 'tools/lib/app-version.mjs')).find(existsSync);
  if (!lib) { console.error('FAIL no tools/lib/app-version.mjs in deps/: run scripts/fetch-deps.sh'); process.exit(1); }
  const { resolveAppVersion } = await import(pathToFileURL(lib).href);
  expect = resolveAppVersion({ dir: ROOT }).version;
}
let failed = 0;
const fail = (m) => { failed++; console.log(`FAIL ${m}`); };
if (tag && /[-+]/.test(expect)) fail(`tag ${tag}: ${expect} is not a release version`);

const files = existsSync(dir) ? readdirSync(dir) : [];
const carts = games.length ? games.map((g) => `${g}.wasc`) : files.filter((f) => f.endsWith('.wasc'));
if (!carts.length) fail(`no carts in ${dir}`);
/** each cart's manifest version (null: no cart or no manifest) */
const versions = carts.map((c) => {
  const r = spawnSync('unzip', ['-p', join(dir, c), 'manifest.json'], { encoding: 'utf8', maxBuffer: 1 << 24 });
  try { return [c, r.status === 0 ? JSON.parse(r.stdout).version ?? '(none)' : null]; } catch { return [c, null]; }
});
for (const [c, v] of versions) {
  if (v === null) fail(`${c}: no cart or no manifest.json in ${dir}`);
  else if (v !== expect) fail(`${c}: manifest version ${v}, not ${expect}`);
  else console.log(`ok   ${c.padEnd(24)} ${v}`);
}
// a release's other files (native packages): the version in each name
for (const f of files.filter((f) => !f.endsWith('.wasc'))) {
  if (!f.includes(`-${expect}-`) && !f.includes(`-${expect}.`)) fail(`${f}: its name does not carry ${expect}`);
  else console.log(`ok   ${f}`);
}
// CONTROL: a copy of the first cart whose manifest says another version must be caught
const first = versions.find(([, v]) => v !== null);
if (first) {
  const ctl = join(ROOT, 'out', 'cart-version-control');
  rmSync(ctl, { recursive: true, force: true });
  mkdirSync(ctl, { recursive: true });
  copyFileSync(join(dir, first[0]), join(ctl, first[0]));
  const m = JSON.parse(spawnSync('unzip', ['-p', join(ctl, first[0]), 'manifest.json'], { encoding: 'utf8' }).stdout);
  m.version = `${expect}-control`;
  writeFileSync(join(ctl, 'manifest.json'), JSON.stringify(m, null, 2));
  const z = spawnSync('zip', ['-q', first[0], 'manifest.json'], { cwd: ctl, encoding: 'utf8' });
  const r = spawnSync('unzip', ['-p', join(ctl, first[0]), 'manifest.json'], { encoding: 'utf8' });
  const got = r.status === 0 ? JSON.parse(r.stdout).version : null;
  if (z.status !== 0) fail(`CONTROL: could not rewrite the copy's manifest (${z.stderr})`);
  else if (got === expect) fail(`CONTROL: ${first[0]} with its manifest set to ${m.version} passed`);
  else console.log(`ok   CONTROL: ${first[0]} with its manifest set to ${got} is caught`);
}

console.log(failed ? `${failed} failed` : `cart version: every cart is ${expect}`);
process.exit(failed ? 1 : 0);
