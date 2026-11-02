// A game has a program for everything it draws at every graphics preset on both backends, read from the engine's own
// report: the states it says are in none of the app's tables (the pipeline's error, cw.three.missing, T3_MISSING_LOG) and
// the draws it skipped (render.skipped). A missing state is a skipped draw and a red "no program" banner in a player's hands,
// and Auto steps a weak machine down to the levels a High-only run never asks for.
//
// Every run keeps the warm-up spread over frames, as a player gets it (--warmup-spread): a test run does the whole warm-up in
// frame 1, under other conditions, and has missed states this way. The states of the whole run count, from the log, since
// cw.three.missing only tells of the last frame. A clean run proves only what it reached, so each scene names a state field
// that says it was there, and a run that did not get there fails.
//
// The control: the game's table is compiled again with one state removed and linked into a scratch binary in out/, and that
// run must report that state and skip draws. A gate that could not see a missing state fails on this.
//
//   node tests/<game>-quality.mjs [--gpu gl|webgpu] [--quality low,medium] [--scene <regex>] [--no-control] [--control-only] [--step-all] [--all] [--shard i/n] [--control-state <state>]
//                                 [--json <file>]
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const TOOL = join(ENGINE, 'tools/run-cart.mjs');
export const QUALITIES = ['low', 'medium', 'high', 'ultra'];
/** a player pressing a button on and on, to get through the menus: [from, to) every `every` frames */
export const mash = (from, to, every, buttons = ['south'], extra = {}) => {
  const out = [];
  for (let f = from; f < to; f += every) out.push({ frame: f, buttons, ...extra }, { frame: f + 2, buttons: [], ...extra });
  return out;
};
const argv = process.argv.slice(2);
const opt = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };

/** a debug bytes field as the state dump writes it: hex, up to the first NUL */
const bytesText = (v) => (typeof v === 'string' && /^([0-9a-f]{2})+$/i.test(v) ? Buffer.from(v, 'hex').toString('latin1').split('\0')[0] : String(v ?? ''));

/** one run of one scene: what the engine reported, whole run */
function runScene(outDir, scene, gpu, quality, exe) {
  const logs = join(outDir, 'logs');
  rmSync(logs, { recursive: true, force: true });
  mkdirSync(logs, { recursive: true });
  const state = join(outDir, 's.json'), log = join(outDir, 's.log');
  rmSync(state, { force: true });
  const args = [TOOL, '--host', 'native', '--gpu', gpu, '--app', scene.app, '--frames', String(scene.frames), '--warmup-spread', '--allow-skipped',
    '--shot', join(outDir, 's.png'), '--state', state, '--log', log];
  // 'step': the settings stepped down in play, as Auto does: High from the start, then Medium, then Low
  const cmds = [...(scene.cmds || []), ...(quality === 'step' ? [`@${Math.floor(scene.frames / 2)} quality medium`, `@${Math.floor(scene.frames * 3 / 4)} quality low`] : [])];
  if (cmds.length) args.push('--start-cmds', cmds.join(';'));
  if (scene.input) {
    const f = join(outDir, 'input.json');
    writeFileSync(f, JSON.stringify(scene.input));
    args.push('--input', f);
  }
  if (exe) args.push('--exe', exe.exe, ...(existsSync(exe.assets) ? ['--assets', exe.assets] : []));
  const env = { ...process.env, CW_PROJECT_ROOT: ROOT, SDL_AUDIODRIVER: 'dummy', CW_PROGRAMS_LOG_DIR: logs, ...(scene.env || {}) };
  // otherwise the preset from the first frame, the warm-up included
  if (quality !== 'step') env.CW_QUALITY = quality;
  const t0 = Date.now();
  const r = spawnSync('node', args, { cwd: ENGINE, encoding: 'utf8', maxBuffer: 1 << 26, env });
  const res = { ms: Date.now() - t0 };
  if (r.status !== 0) return { ...res, failed: `run failed: ${(r.stderr || r.stdout || '').slice(-600)}` };
  const s = JSON.parse(readFileSync(state, 'utf8'));
  const text = existsSync(log) ? readFileSync(log, 'utf8') : '';
  const seen = new Set();
  for (const m of text.matchAll(/Missing:\n((?:[^\n]*\+[^\n]*\n?)+)/g)) for (const l of m[1].split('\n')) if (/^[a-z]+(\+\S+)?$/.test(l.trim())) seen.add(l.trim());
  for (const l of bytesText(s['cw.three.missing_states']).split('\n')) if (l.trim()) seen.add(l.trim());
  for (const f of spawnSync('sh', ['-c', `cat "${logs}"/* 2>/dev/null`], { encoding: 'utf8', maxBuffer: 1 << 26 }).stdout.split('\n')) if (f.trim() && !f.startsWith('#')) seen.add(f.trim());
  const reports = [...text.matchAll(/(\d+) draw state\(s\) are in none/g)].map((m) => +m[1]);
  const skipped = s['render.skipped'] ?? null;
  return { ...res, state: s, missing: [...seen].sort(), reports, lastFrameMissing: s['cw.three.missing'] ?? null, skipped, shot: join(outDir, 's.png') };
}

/** the scene's own proof it was reached; null when it was */
function notReached(scene, s) {
  if (!scene.reach) return null;
  for (const [k, want] of Object.entries(scene.reach)) {
    const raw = s[k];
    const v = Array.isArray(raw) ? raw[0] : raw;
    const ok = typeof want === 'function' ? want(v, s) : v === want;
    if (!ok) return `${k}=${JSON.stringify(raw)}`;
  }
  return null;
}

/** the table's generated C with one row out, compiled and linked into a scratch binary (nothing in the tree changes) */
function controlBuild(game, app, state, tables, outDir) {
  mkdirSync(outDir, { recursive: true });
  const build = join(ROOT, 'build/native');
  const exeRel = spawnSync('sh', ['-c', `node -e "console.log(require('./apps/${app}.json').binary)"`], { cwd: build, encoding: 'utf8' }).stdout.trim();
  const target = exeRel.startsWith('/') ? exeRel.slice(build.length + 1) : exeRel;
  const cmds = spawnSync('ninja', ['-t', 'commands', target], { cwd: build, encoding: 'utf8', maxBuffer: 1 << 28 }).stdout.split('\n');
  let removed = 0;
  const libs = [];
  for (const t of tables) {
    const objs = [];
    for (const suffix of ['gl', 'wgpu']) {
      const src = join(ROOT, `games/${game}/programs/${t}_${suffix}.c`);
      let c = readFileSync(src, 'utf8');
      const rows = c.split('\n');
      // '*': the lit rows out, to list what a scene draws (--control-state '*' with a control that then fails)
      const out = rows.filter((l) => (state === '*' ? /^  \{ "(standard|physical)/.test(l) : l.startsWith(`  { "${state}", `)));
      if (out.length) {
        c = rows.filter((l) => !out.includes(l)).join('\n').replace(new RegExp(`(const t3_gen_table ${t}_${suffix} = \\{ programs, )(\\d+)( \\})`), (_, a, n, b) => `${a}${+n - out.length}${b}`);
        removed++;
      }
      const copy = join(outDir, `${t}_${suffix}.c`);
      writeFileSync(copy, c);
      const obj = join(outDir, `${t}_${suffix}.o`);
      const cc = cmds.find((l) => l.includes(`programs/${t}_${suffix}.c`) && l.includes(' -c '));
      if (!cc) throw new Error(`control: no compile command for ${t}_${suffix}.c`);
      const one = cc.replace(/-MD -MT \S+ -MF \S+/, '').replace(/-o \S+/, `-o ${obj}`).replace(/-c \S+/, `-c ${copy}`);
      const r = spawnSync('sh', ['-c', one], { cwd: build, encoding: 'utf8' });
      if (r.status !== 0) throw new Error(`control: compile failed: ${r.stderr.slice(-500)}`);
      objs.push(obj);
    }
    const lib = join(outDir, `libcwprog_${t}.a`);
    rmSync(lib, { force: true });
    const r = spawnSync('ar', ['qcs', lib, ...objs], { encoding: 'utf8' });
    if (r.status !== 0) throw new Error(`control: ar failed: ${r.stderr}`);
    libs.push([t, lib]);
  }
  if (!removed) throw new Error(`control: ${state} is in no table of ${game} (${tables.join(', ')})`);
  const exe = join(outDir, basename(target));
  let link = cmds.filter((l) => l.includes(` -o ${target} `)).pop();
  if (!link) throw new Error(`control: no link command for ${target}`);
  link = link.replace(` -o ${target} `, ` -Wl,-rpath,${join(build, dirname(target))} -o ${exe} `).replace(/-Wl,--dependency-file=\S+/, '');
  for (const [, lib] of libs) link = link.replace(new RegExp(`\\S*libcwprog_${basename(lib).slice(10, -2)}\\.a`, 'g'), lib);
  const r = spawnSync('sh', ['-c', link], { cwd: build, encoding: 'utf8' });
  if (r.status !== 0) throw new Error(`control: link failed: ${r.stderr.slice(-500)}`);
  // the pack and the assets are found beside the binary
  for (const ext of ['.cwpak', '.assets']) if (existsSync(join(build, target + ext))) symlinkSync(join(build, target + ext), exe + ext);
  return { exe, assets: join(build, `${target}.assets`), removed };
}

/**
 * @param {object} g
 * @param {string} g.game       games/<game>
 * @param {string[]} g.tables   its program tables (programs/<table>.txt)
 * @param {object[]} g.scenes   { name, app, frames, cmds?, env?, input?, reach?, show?, presets?, mustNotReach? (a control: its reach must fail, and a run that reaches it does) }
 * @param {object|object[]} g.control    { state, scene (a scene's name), gpu?, quality? }: a state that scene draws at that preset; several when one scene can not speak for the rest
 * @param {string[]} [g.qualities] the presets
 */
export function sweep({ game, tables, scenes, control, qualities = QUALITIES }) {
  const OUT = join(ROOT, 'out/quality', game);
  mkdirSync(OUT, { recursive: true });
  const gpus = opt('--gpu') ? opt('--gpu').split(',') : ['gl', 'webgpu'];
  const qs = opt('--quality') ? opt('--quality').split(',') : qualities;
  const only = opt('--scene') ? new RegExp(opt('--scene')) : null;
  const shard = opt('--shard') ? opt('--shard').split('/').map(Number) : null; // i/n: every n-th scene from i, to run a long sweep in pieces
  const results = [];
  let fail = 0;
  if (!argv.includes('--control-only')) for (const gpu of gpus) for (const q of [...qs, ...(opt('--quality') ? [] : ['step'])]) for (const [si, sc] of scenes.entries()) {
    if (only && !only.test(sc.name)) continue;
    if (shard && si % shard[1] !== shard[0]) continue;
    // a long gate holds the GPU lock too long: some scenes name the presets a plain gate run gives them; --all runs every preset everywhere
    if (sc.presets && !argv.includes('--all') && q !== 'step' && !sc.presets.includes(q)) continue;
    if (q === 'step' && si !== 0 && !argv.includes('--step-all')) continue; // one scene takes the step down, unless asked
    const r = runScene(OUT, sc, gpu, q);
    const offReach = r.state && notReached(sc, r.state);
    const off = r.failed || (sc.mustNotReach ? (offReach ? null : 'a control scene that must not be reached was') : offReach) || (r.state && [].concat(r.state['cw.quality'])[0] !== (q === 'step' ? 0 : QUALITIES.indexOf(q)) && `cw.quality=${r.state['cw.quality']}`);
    const bad = r.failed ? -1 : r.missing.length + (r.skipped ? 1 : 0);
    if (off || bad) fail++;
    const line = `${off ? 'NOT REACHED' : bad ? 'FAIL' : 'ok'} ${gpu} ${q} ${sc.name}: missing ${r.missing ? r.missing.length : '?'} last-frame ${r.lastFrameMissing} skipped ${r.skipped}${off ? ` (${off})` : ''}${r.missing && r.missing.length ? ` [${r.missing.join(' ')}]` : ''} ${(r.ms / 1000).toFixed(0)}s${sc.show && r.state ? ' ' + sc.show.map((k) => `${k}=${JSON.stringify(r.state[k])}`).join(' ') : ''}`;
    console.log(line);
    results.push({ game, gpu, quality: q, scene: sc.name, missing: r.missing, reports: r.reports, lastFrameMissing: r.lastFrameMissing, skipped: r.skipped, reached: !off, off: off || null, ms: r.ms });
    if (opt('--json')) writeFileSync(opt('--json'), JSON.stringify(results));
  }
  if (control && !argv.includes('--no-control')) for (const k of [].concat(control)) {
    const sc = scenes.find((s) => s.name === k.scene);
    const dir = join(OUT, 'control');
    const state = opt('--control-state') || k.state;
    const exe = controlBuild(game, sc.app, state, tables, dir);
    for (const gpu of k.gpu ? [k.gpu] : gpus) {
      const q = k.quality || 'low';
      const r = runScene(OUT, sc, gpu, q, exe);
      const ok = !r.failed && r.missing.includes(state) && r.skipped > 0;
      console.log(`${ok ? 'ok  ' : 'FAIL'} control ${gpu} ${q} ${sc.name}, ${state} out of the table: must report it and skip draws: ${r.failed || `missing [${r.missing.join(' ')}] skipped ${r.skipped}`}`);
      if (!ok) fail++;
    }
    rmSync(dir, { recursive: true, force: true });
  }
  process.exit(fail ? 1 : 0);
}
