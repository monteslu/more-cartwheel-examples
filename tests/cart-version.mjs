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
// every .wasc in <dir> (default build/cart-wgpu/carts). Only *.wasc files are
// carts: anything else in <dir> (a smoke run's <game>.wasc.sav, a .tmp) is
// ignored, except that a release's native packages (*.tar.gz, *.AppImage,
// *.zip, *.apk) must carry the version in their names.
// SELF-TEST: a scratch directory holding a cart, its .wasc.sav and a .tmp
// passes, and the same with a package whose name lacks the version fails.
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

const PACKAGE = /\.(tar\.gz|AppImage|zip|apk)$/;
/** a cart's manifest version (null: no cart or no manifest) */
const manifestVersion = (file) => {
  const r = spawnSync('unzip', ['-p', file, 'manifest.json'], { encoding: 'utf8', maxBuffer: 1 << 24 });
  try { return r.status === 0 ? JSON.parse(r.stdout).version ?? '(none)' : null; } catch { return null; }
};
/** check one directory: its carts' manifests and its packages' names; returns [problems, lines, versions] */
function checkDir(d, names, want) {
  const problems = [], lines = [];
  const files = existsSync(d) ? readdirSync(d) : [];
  const carts = names.length ? names.map((g) => `${g}.wasc`) : files.filter((f) => f.endsWith('.wasc'));
  if (!carts.length) problems.push(`no carts in ${d}`);
  const versions = carts.map((c) => [c, manifestVersion(join(d, c))]);
  for (const [c, v] of versions) {
    if (v === null) problems.push(`${c}: no cart or no manifest.json in ${d}`);
    else if (v !== want) problems.push(`${c}: manifest version ${v}, not ${want}`);
    else lines.push(`ok   ${c.padEnd(24)} ${v}`);
  }
  for (const f of files.filter((f) => PACKAGE.test(f))) {
    if (!f.includes(`-${want}-`) && !f.includes(`-${want}.`)) problems.push(`${f}: its name does not carry ${want}`);
    else lines.push(`ok   ${f}`);
  }
  return [problems, lines, versions];
}
const [problems, lines, versions] = checkDir(dir, games, expect);
lines.forEach((l) => console.log(l));
problems.forEach(fail);
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

// SELF-TEST: leftovers beside the carts are ignored; a package without the version is not
if (first) {
  const st = join(ROOT, 'out', 'cart-version-selftest');
  rmSync(st, { recursive: true, force: true });
  mkdirSync(st, { recursive: true });
  copyFileSync(join(dir, first[0]), join(st, first[0]));
  writeFileSync(join(st, `${first[0]}.sav`), 'save data');
  writeFileSync(join(st, `${first[0]}.tmp`), 'partial');
  const [p1] = checkDir(st, [], first[1]);
  if (p1.length) fail(`SELF-TEST: a cart with a .wasc.sav and a .tmp beside it failed: ${p1.join('; ')}`);
  else console.log('ok   SELF-TEST: a .wasc.sav and a .tmp beside the carts are ignored');
  writeFileSync(join(st, 'game-0.0.0-linux-x64.tar.gz'), '');
  const [p2] = checkDir(st, [], first[1]);
  if (p2.length !== 1) fail(`SELF-TEST: a package without the version gave ${p2.length} problems, not 1`);
  else console.log('ok   SELF-TEST: a package whose name lacks the version is caught');
}

console.log(failed ? `${failed} failed` : `cart version: every cart is ${expect}`);
process.exit(failed ? 1 : 0);
