// skyfighter's graphics row in Settings, on the cart through romdev with real pad and pointer input: every preset reached in
// both directions on GL and on WebGPU, the word in the save kept with each step, and the choice still there after a restart.
// Then a save holding Auto or a Custom word (the menu cannot choose them; the engine's word can hold them) and what the row
// does from there. Then the desktop's own save path: the native build, on a save file, across two runs.
//
//   node tests/skyfighter-settings.mjs [--gpu gl|webgpu] [--cart <file>] [--exe <file>] [--no-native]
//
// A choice is only restored outside a deterministic run (cartwheel/quality.h), so the romdev loads here carry no seed and the
// native runs set CW_PLAYER_TIMING. Starts from a zeroed save on the cart it loads, and leaves it zeroed: romdev keeps a cart's
// save between loads by its path.
//
// Controls that must fail: the same walk checker fed by presses on the PITCH row (the row with no presets, so nothing cycles),
// and the same restart checker after the saved word is cleared (the cart must come back on its Medium default, not on the
// preset picked). Run with --cart on a build of the old modulus to see the walk fail on High going to Low.
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { wgpuPin } from '../deps/cartwheel-engine/tools/lib/wgpu-pin.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const OUT = join(ROOT, 'out/skyfighter-settings');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const CART = resolve(opt('--cart', join(ROOT, 'build/cart-wgpu/carts/skyfighter.wasc')));
const EXE = resolve(opt('--exe', join(ROOT, 'build/native/games/skyfighter/skyfighter')));
const GPUS = argv.includes('--gpu') ? [opt('--gpu')] : ['gl', 'webgpu'];
const SESSION = `skyfighter-settings-${process.pid}`;
const SETTINGS = 5; // mission.state
const VERY_LOW = 0, LOW = 1, MEDIUM = 2, HIGH = 3, ULTRA = 4, NAMES = ['Very Low', 'Low', 'Medium', 'High', 'Ultra'];
// the saved word of a preset at Resolution 100% (cartwheel/quality.h, word v2: code 7, the preset in bits 3-5, Resolution in 6-8)
const presetWord = (level) => 7 | level << 3 | 5 << 6;
const MODE_PRESET = 0, MODE_CUSTOM = 1, MODE_AUTO = 2; // cw.quality_mode
const GRAPHICS_OFFSET = 8; // preferences.graphics: magic, the flag byte, three bytes of settings, then this word
const ROW = { pointerY: 169 + 42 + 19, right: 650, left: 400 }; // the graphics row: the right of x 590 steps up, the left of it down

let failed = 0;
const check = (name, ok, detail = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? `: ${detail}` : ''}`); if (!ok) failed++; };

async function call(tool, a) {
  const res = await fetch(`http://127.0.0.1:7331/tool/${tool}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(a) });
  const t = await res.text();
  let b; try { b = JSON.parse(t); } catch { b = t; }
  if (!res.ok || (b && b.isError)) throw new Error(`${tool}: ${t.slice(0, 500)}`);
  if (b && Array.isArray(b.content)) { const c = b.content.find((x) => x.type === 'text'); try { return JSON.parse(c.text); } catch { return c ? c.text : b; } }
  return b;
}
const step = (frames) => call('frame', { op: 'step', frames });
const fields = async () => Object.fromEntries(((await call('wasm', { op: 'debugState' })).fields || []).map((f) => [f.name, f.values ?? f.value]));
async function tap(button) {
  await call('input', { op: 'set', ports: [{ [button]: true }] });
  await step(2);
  await call('input', { op: 'set', ports: [{}] });
  await step(4);
}
async function click(x, y) {
  const v = (await fields())['cw.view'];
  const sx = v[2] / v[0], sy = v[3] / v[1];
  const at = (left) => call('input', { op: 'pointer', id: 0, x: Math.round(v[4] + x * sx), y: Math.round(v[5] + y * sy), left, active: true });
  await at(false); await step(2);
  await at(true); await step(2);
  await at(false); await step(4);
}
const saveFile = join(OUT, 'save.sav');
async function readSave() {
  await call('state', { op: 'exportSram', path: saveFile });
  return readFileSync(saveFile);
}
async function writeSave(bytes) {
  writeFileSync(saveFile, bytes);
  await call('state', { op: 'importSram', path: saveFile });
}
/** the preset as the row prints it (cw_quality_name(cw_quality_get()) is what the engine answers here too), and the saved word */
async function reading() {
  const reply = (await call('wasm', { op: 'command', text: 'quality' })).reply;
  const s = await fields();
  return { level: s['cw.quality'], mode: s['cw.quality_mode'], name: reply.preset, settings: reply.settings, word: (await readSave()).readUInt32LE(GRAPHICS_OFFSET), state: s };
}
async function load(gpu) {
  const pin = wgpuPin();
  const r = await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: pin.power, wgpu: gpu === 'webgpu' });
  if (r.gpu !== gpu) throw new Error(`the cart runs on ${r.gpu}, not ${gpu}`);
  if (gpu === 'webgpu') pin.check('romdev', r.gpuAdapter && (r.gpuAdapter.device || r.gpuAdapter.description));
  let s = await fields();
  for (let n = 0; !(s['cw.tick'] > 0) && n < 2000; n++) { await step(1); s = await fields(); }
  if (!(s['cw.tick'] > 0)) throw new Error('the cart never ticked');
  await step(20);
}
const restart = async (gpu) => { await call('host', { op: 'shutdown' }); await load(gpu); };
/** the settings screen, on the graphics row */
async function openRow() {
  await tap('north');
  const s = await fields();
  if (s['mission.state'] !== SETTINGS) throw new Error(`north did not open Settings (mission.state ${s['mission.state']})`);
  await tap('down');
}
/**
 * press, read, and hold each step to the next preset with its word in the save. Returns what it saw; ok only for a walk that
 * went preset by preset the given way, round all five, back to the start.
 */
async function walk(press, dir) {
  const start = await reading();
  const seen = [start.level], problems = [];
  for (let i = 0; i < NAMES.length; i++) {
    await press();
    const r = await reading();
    seen.push(r.level);
    const want = (seen[i] + NAMES.length + dir) % NAMES.length;
    if (r.level !== want) problems.push(`${NAMES[seen[i]]} went to ${NAMES[r.level]}, not ${NAMES[want]}`);
    if (r.name !== NAMES[r.level]) problems.push(`the row would print ${r.name} on ${NAMES[r.level]}`);
    if (r.word !== presetWord(r.level)) problems.push(`the save holds ${r.word} on ${NAMES[r.level]}`);
    if (r.mode !== MODE_PRESET) problems.push(`mode ${r.mode} on ${NAMES[r.level]}`);
    if (r.state['cw.gpu.errors'] || r.state['render.skipped']) problems.push(`${NAMES[r.level]}: ${r.state['cw.gpu.errors'] || 0} GPU errors, ${r.state['render.skipped'] || 0} draws skipped`);
  }
  if (new Set(seen).size !== NAMES.length) problems.push(`reached ${new Set(seen).size} of ${NAMES.length} presets`);
  return { ok: problems.length === 0, text: seen.map((q) => NAMES[q] ?? q).join(' > '), problems };
}
const report = (name, w) => check(name, w.ok, `${w.text}${w.problems.length ? ` (${w.problems.join('; ')})` : ''}`);

async function suite(gpu) {
  console.log(`--- the cart on ${gpu}`);
  await load(gpu);
  const size = (await readSave()).length;
  const fresh = Buffer.alloc(size);
  await writeSave(fresh);
  await restart(gpu);
  const first = await reading();
  check('a first run starts on Medium, with no choice saved', first.level === MEDIUM && first.word === 0, `${NAMES[first.level]}, word ${first.word}`);

  // control: the same checker, presses on the row that has no presets
  await tap('north');
  const ctl = await walk(() => tap('right'), 1);
  if (ctl.ok) { console.log(`FAIL control passed: the walk checker took presses on the PITCH row as a walk: ${ctl.text}`); failed++; }
  else console.log(`ok   control failed as it must, right on the PITCH row: ${ctl.text}`);
  await tap('down');

  report('right walks up through all four', await walk(() => tap('right'), 1));
  report('left walks down through all four', await walk(() => tap('left'), -1));
  report('south steps up', await walk(() => tap('south'), 1));
  report('a click on the right of the row steps up', await walk(() => click(ROW.right, ROW.pointerY), 1));
  report('a click on the left of the row steps down', await walk(() => click(ROW.left, ROW.pointerY), -1));

  // what the save is for: the choice after a restart, each of the four
  for (const q of [ULTRA, LOW, HIGH, MEDIUM]) {
    await openRowOnce();
    for (let n = 0; (await reading()).level !== q && n < NAMES.length; n++) await tap('right');
    const before = await reading();
    await restart(gpu);
    const after = await reading();
    check(`${NAMES[q]} survives a restart`, before.level === q && after.level === q && after.mode === MODE_PRESET && after.word === presetWord(q) && after.name === NAMES[q], `${NAMES[before.level]} before, ${NAMES[after.level]} after, word ${after.word}`);
    if (q === ULTRA) {
      // the wrap, from the restored preset rather than one set this run
      await openRow();
      await tap('left');
      const down = await reading();
      await tap('right');
      const up = await reading();
      await tap('right');
      const wrap = await reading();
      check('from a restored Ultra: left is High, right is Ultra, right again is Very Low', down.level === HIGH && up.level === ULTRA && wrap.level === VERY_LOW, `${NAMES[down.level]}, ${NAMES[up.level]}, ${NAMES[wrap.level]}`);
      await tap('left'); // back on Ultra, the choice the next restart starts from
      await restart(gpu);
    }
  }

  // control: a cleared word and the same restart must not read as the choice surviving
  await openRowOnce();
  for (let n = 0; (await reading()).level !== ULTRA && n < NAMES.length; n++) await tap('right');
  const cleared = Buffer.from(await readSave());
  cleared.writeUInt32LE(0, GRAPHICS_OFFSET);
  await writeSave(cleared);
  await restart(gpu);
  const noChoice = await reading();
  if (noChoice.level === ULTRA) { console.log('FAIL control passed: Ultra came back with no word in the save, so the restart check proves nothing'); failed++; }
  else console.log(`ok   control failed as it must, Ultra picked and the word cleared: back on ${NAMES[noChoice.level]}`);

  // a save holding Auto (the engine's word 5) or a Custom word: the menu offers neither, a save can hold them
  const withWord = async (word) => {
    const b = Buffer.from(await readSave());
    b.writeUInt32LE(word, GRAPHICS_OFFSET);
    await writeSave(b);
    await restart(gpu);
    return reading();
  };
  const auto = await withWord(5);
  check('a saved Auto restores as Auto', auto.mode === MODE_AUTO, `mode ${auto.mode}, ${NAMES[auto.level]}`);
  await openRow();
  const fromAuto = auto.level;
  await tap('right');
  const leftAuto = await reading();
  check('from Auto, right leaves Auto for the next preset and saves that', leftAuto.mode === MODE_PRESET && leftAuto.level === (fromAuto + 1) % NAMES.length && leftAuto.word === presetWord(leftAuto.level), `${NAMES[fromAuto]} to ${NAMES[leftAuto.level]}, mode ${leftAuto.mode}, word ${leftAuto.word}`);
  // resolution 100%, ambient occlusion off, shadows High, FXAA, effects Ultra: matches no preset
  const custom = await withWord(6 | 5 << 3 | 0 << 6 | 2 << 7 | 1 << 9 | 3 << 11);
  check('a saved Custom word restores as Custom', custom.mode === MODE_CUSTOM, `mode ${custom.mode}, level ${custom.level}`);
  await openRow();
  const fromCustom = custom.level;
  await tap('left');
  const leftCustom = await reading();
  check('from Custom, left leaves it for a preset and saves that', leftCustom.mode === MODE_PRESET && leftCustom.level === (fromCustom + NAMES.length - 1) % NAMES.length && leftCustom.word === presetWord(leftCustom.level), `${NAMES[fromCustom]} to ${NAMES[leftCustom.level]}, mode ${leftCustom.mode}, word ${leftCustom.word}`);

  await writeSave(fresh);
  await call('host', { op: 'shutdown' });

  async function openRowOnce() {
    const s = await fields();
    if (s['mission.state'] !== SETTINGS) await openRow();
  }
}

/** the native build on a save file of its own, two runs: not a deterministic run, so it writes and reads the file */
function native(gpu) {
  console.log(`--- the native build on ${gpu}`);
  const dir = join(OUT, `native-${gpu}`);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const run = (tag, script, frames) => {
    const state = join(dir, `${tag}.json`), input = join(dir, `${tag}.input.json`);
    writeFileSync(input, JSON.stringify(script));
    const r = spawnSync('node', [join(ENGINE, 'tools/run-cart.mjs'), '--host', 'native', '--gpu', gpu, '--app', 'skyfighter', '--frames', String(frames), '--input', input, '--state', state, '--exe', EXE], {
      cwd: ENGINE, encoding: 'utf8', maxBuffer: 1 << 26,
      env: { ...process.env, CW_PROJECT_ROOT: ROOT, CW_PLAYER_TIMING: '1', CW_SAVE_FILE: join(dir, 'save.dat'), CW_QUALITY: '' },
    });
    if (r.status !== 0) throw new Error(`native ${tag}: ${r.stdout.slice(-400)} ${r.stderr.slice(-600)}`);
    return JSON.parse(readFileSync(state, 'utf8'));
  };
  const presses = (...buttons) => buttons.flatMap((b, i) => [{ frame: 60 + i * 8, buttons: [b] }, { frame: 62 + i * 8, buttons: [] }]);
  const FRAMES = 140;
  const picked = run('pick', presses('north', 'down', 'right', 'right'), FRAMES);
  check('native: from Medium, two rights reach Ultra', picked['cw.quality'] === ULTRA, `cw.quality ${picked['cw.quality']}`);
  const word = readFileSync(join(dir, 'save.dat')).readUInt32LE(GRAPHICS_OFFSET);
  check('native: the save file holds Ultra', word === presetWord(ULTRA), `word ${word}`);
  const again = run('again', [], 30);
  check('native: Ultra survives a restart', again['cw.quality'] === ULTRA, `cw.quality ${again['cw.quality']}`);
  const back = run('back', presses('north', 'down', 'right'), FRAMES);
  check('native: right from a restored Ultra wraps to Very Low', back['cw.quality'] === VERY_LOW, `cw.quality ${back['cw.quality']}`);
  // control: no save file, the same run must come back on the default
  rmSync(join(dir, 'save.dat'));
  const none = run('none', [], 30);
  if (none['cw.quality'] === ULTRA || none['cw.quality'] === VERY_LOW) { console.log(`FAIL control passed: no save file and still ${NAMES[none['cw.quality']]}`); failed++; }
  else console.log(`ok   control failed as it must, no save file: ${NAMES[none['cw.quality']]}`);
  rmSync(dir, { recursive: true, force: true });
}

mkdirSync(OUT, { recursive: true });
if (!existsSync(CART)) { console.error(`no ${CART}: scripts/build.sh skyfighter`); process.exit(2); }
for (const gpu of GPUS) await suite(gpu);
if (!argv.includes('--no-native')) for (const gpu of GPUS) native(gpu);
rmSync(saveFile, { force: true });
console.log(failed ? `FAIL ${failed}` : 'PASS');
process.exit(failed ? 1 : 0);
