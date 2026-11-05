// Every release cart carries its launcher art: the manifest's `art` names an
// icon, a cover, a background, a logo, a title screen and 3 to 6 labelled
// screenshots, each one in the cart and the shape wasmcart's describeArtProblems
// expects, with a description and an author beside them.
// CONTROL that must fail: the same check on a manifest missing its cover.
//
//   node tests/cart-art.mjs [game ...]     (default: every release game)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const GAMES = ['arenafps', 'brawler', 'coopplatformer', 'fighter', 'hordeshooter', 'jetski', 'partypack', 'pirates', 'skyfighter', 'tuxracer'];
const games = process.argv.slice(2).length ? process.argv.slice(2) : GAMES;
// the checks the cart SDK's pack tool uses (wasmcart src/cartArt.js)
const lib = join(ROOT, 'deps', 'cartwheel-sdk', 'cart-wgpu', 'deps', 'wasmcart-wgpu', 'src', 'cartArt.js');
if (!existsSync(lib)) { console.log(`FAIL no ${lib}: the cart SDK is older than the art support`); process.exit(1); }
const { describeArtProblems } = await import(pathToFileURL(lib).href);

/** what is wrong with one cart's art and credits ([] when nothing) */
function problems(manifest, read) {
  const out = [];
  if (!manifest) return ['no manifest.json'];
  if (typeof manifest.description !== 'string' || !manifest.description.trim()) out.push('no description');
  else if (manifest.description.length > 300) out.push(`description is ${manifest.description.length} characters (over 300)`);
  if (typeof manifest.author !== 'string' || !manifest.author.trim()) out.push('no author');
  const art = manifest.art;
  if (!art) return [...out, 'no art'];
  for (const k of ['icon', 'cover', 'background', 'logo', 'title']) if (typeof art[k] !== 'string') out.push(`no ${k}`);
  const shots = Array.isArray(art.screenshots) ? art.screenshots : [];
  if (shots.length < 3 || shots.length > 6) out.push(`${shots.length} screenshots (3 to 6)`);
  for (const s of shots) if (!s.label || !String(s.label).trim()) out.push(`screenshot ${s.path} has no label`);
  return [...out, ...describeArtProblems(art, read)];
}

function openCart(file) {
  const read = (p) => {
    const r = spawnSync('unzip', ['-p', file, p], { maxBuffer: 1 << 28 });
    return r.status === 0 && r.stdout.length ? new Uint8Array(r.stdout) : null;
  };
  const m = read('manifest.json');
  return { manifest: m ? JSON.parse(Buffer.from(m).toString('utf8')) : null, read };
}

let failed = 0;
let control = null;
for (const g of games) {
  const file = join(ROOT, 'build', 'cart-wgpu', 'carts', `${g}.wasc`);
  if (!existsSync(file)) { failed++; console.log(`FAIL ${g}: no ${file}`); continue; }
  const { manifest, read } = openCart(file);
  const p = problems(manifest, read);
  if (p.length) { failed++; console.log(`FAIL ${g}: ${p.join('; ')}`); } else {
    console.log(`ok   ${g}: ${manifest.art.screenshots.length} screenshots, by ${manifest.author}`);
    control ??= { manifest, read };
  }
}
if (control) {
  const art = { ...control.manifest.art };
  delete art.cover;
  const p = problems({ ...control.manifest, art }, control.read);
  if (!p.some((x) => /cover/.test(x))) { failed++; console.log('FAIL control: a cart with no cover passed'); } else console.log('ok   control: a cart with no cover is caught');
}
process.exit(failed ? 1 : 0);
