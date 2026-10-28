// The games' _auto replays on a native build of any platform, without the
// engine's gate runner (which drives Linux test machines): run each <game>_auto
// for its replay gate's frame count (REPLAY below names the gate per game), check the gate's expected values, and write the
// named state the gate compares. With --ref, the state must equal a reference
// run's (another platform's) field for field: the replays are deterministic,
// so every platform must reach the same bits.
//
//   node tests/native-replay.mjs --bin <dir> --out <dir> [--gpu gl|webgpu|auto] [--ref <dir>] [game ...]
//
//   --bin   where the <game>_auto executables are: a build tree (build/native,
//           games/<game>/<game>_auto) or a folder holding <game>_auto[.exe]
//           (or <game>_auto.app) with its .cwpak beside it
//   --out   <out>/<game>.json: {gate, gpu, api, state} per game
//   --gpu   CW_GPU for the runs (default gl); auto lets the game pick
//   --ref   a directory of another run's <game>.json to compare against
//   --frames N run each game's first N frames instead of the whole replay: no
//           end-of-replay expectations, but the state at frame N is written and
//           (with --ref) compared, so platforms are held to the same bits on a
//           short slice
//   --smoke N  run each game N frames only: it starts and draws on that API
//           (no expectations); for runners too slow for the full replay
//
// Exit status: 0 when every game ran, met its gate's expectations and (with
// --ref) matched; 1 otherwise.
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync, statSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const VALUED = new Set(['--bin', '--out', '--gpu', '--ref', '--smoke', '--frames']);
const BIN = opt('--bin') && resolve(opt('--bin'));
const OUT = opt('--out') && resolve(opt('--out'));
const GPU = opt('--gpu', 'gl');
const REF = opt('--ref') && resolve(opt('--ref'));
const SMOKE = opt('--smoke') ? +opt('--smoke') : 0;
const SLICE = opt('--frames') ? +opt('--frames') : 0;
if (!BIN || !OUT) {
  console.error('usage: node tests/native-replay.mjs --bin <dir> --out <dir> [--gpu gl|webgpu|auto] [--ref <dir>] [game ...]');
  process.exit(2);
}
const GAMES = argv.filter((a, i) => !a.startsWith('--') && !VALUED.has(argv[i - 1]));
// each game's replay: a gate whose app is <game>_auto, plays by itself and sets no environment
const REPLAY = {
  arenafps: 'arenafps-match',
  brawler: 'brawler-brawl',
  coopplatformer: 'coopplatformer-2p',
  fighter: 'fighter-auto',
  hordeshooter: 'hordeshooter-match',
  partypack: 'partypack-tournament-short',
};
const GATE_FILES = ['gates.json', 'brawler-gates.json', 'coopplatformer-gates.json', 'fighter-gates.json', 'partypack-gates.json'];
const gates = GATE_FILES.flatMap((f) => JSON.parse(readFileSync(join(ROOT, 'tests', f), 'utf8')));
const games = GAMES.length ? GAMES : Object.keys(REPLAY);
const exe = process.platform === 'win32' ? '.exe' : '';
const isFile = (f) => existsSync(f) && statSync(f).isFile();

function binary(app) {
  const game = app.replace(/_auto$/, '');
  for (const f of [join(BIN, 'games', game, app + exe), join(BIN, app + exe), join(BIN, `${app}.app/Contents/MacOS/${app}`)])
    if (isFile(f)) return f;
  return null;
}
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

mkdirSync(OUT, { recursive: true });
let failed = 0;
for (const game of games) {
  const gate = gates.find((g) => g.name === REPLAY[game]);
  if (!gate) { console.error(`${game}: no replay gate (REPLAY in tests/native-replay.mjs, gates in tests/${GATE_FILES.join(", ")})`); failed++; continue; }
  const bin = binary(gate.app);
  if (!bin) { console.error(`${game}: no ${gate.app}${exe} under ${BIN}`); failed++; continue; }
  const dump = join(OUT, `${game}.state.json`);
  const env = {
    ...process.env, CW_FRAMES: String(SMOKE || SLICE || gate.frames), CW_FRAME_HZ: '60', CW_WARMUP_SPREAD: '0', CW_DEBUG_DUMP: dump,
    SDL_AUDIODRIVER: process.env.SDL_AUDIODRIVER || 'dummy',
  };
  if (GPU === 'auto') delete env.CW_GPU;
  else env.CW_GPU = GPU;
  const t0 = Date.now();
  const r = spawnSync(bin, [], { env, encoding: 'utf8', timeout: +(process.env.CW_RUN_TIMEOUT_MS || 3600000), cwd: dirname(bin) });
  const secs = ((Date.now() - t0) / 1000).toFixed(1);
  if (r.error || r.status !== 0 || !existsSync(dump)) {
    console.error(`${game}: ${gate.app} ${r.error ? r.error.message : `exited ${r.status ?? r.signal}`} after ${secs} s\n${(r.stderr || '').split('\n').slice(-30).join('\n')}`);
    failed++;
    continue;
  }
  const all = JSON.parse(readFileSync(dump, 'utf8'));
  const state = Object.fromEntries(gate.fields.map((f) => [f, all[f]]));
  const api = all['cw.gpu.api'] === 2 ? 'webgpu' : all['cw.gpu.api'] === 1 ? 'gl' : 'unknown';
  const adapter = /WebGPU adapter: (.*)/.exec(r.stderr || '')?.[1] || /GL: (.*)/.exec(r.stderr || '')?.[1] || '';
  const problems = [];
  for (const [k, v] of Object.entries(SMOKE || SLICE ? {} : gate.expect || {})) if (!same(all[k], v)) problems.push(`${k} = ${JSON.stringify(all[k])}, the gate expects ${JSON.stringify(v)}`);
  for (const f of gate.fields) if (all[f] === undefined) problems.push(`${f}: missing from the state`);
  if (GPU !== 'auto' && api !== GPU) problems.push(`ran on ${api}, not ${GPU}`);
  if (all['cw.gpu.errors']) problems.push(`${all['cw.gpu.errors']} GPU error(s)`);
  let matched = null;
  if (REF && !SMOKE) {
    const refFile = join(REF, `${game}.json`);
    if (!existsSync(refFile)) problems.push(`no reference ${refFile}`);
    else {
      const ref = JSON.parse(readFileSync(refFile, 'utf8')).state;
      const diff = gate.fields.filter((f) => !same(ref[f], state[f]));
      matched = diff.length === 0;
      for (const f of diff) problems.push(`${f} = ${JSON.stringify(state[f])}, the reference has ${JSON.stringify(ref[f])}`);
    }
  }
  if (!SMOKE) writeFileSync(join(OUT, `${game}.json`), JSON.stringify({ gate: gate.name, app: gate.app, frames: SLICE || gate.frames, gpu: GPU, api, adapter,
    platform: `${process.platform}-${process.arch}`, seconds: +secs, matched, state }, null, 2) + '\n');
  if (problems.length) { failed++; console.error(`${game}: FAIL (${api}, ${secs} s)\n  ${problems.join('\n  ')}`); }
  else console.log(`${game}: ok (${api}${adapter ? `, ${adapter}` : ''}, ${SMOKE || SLICE || gate.frames} frames${SMOKE ? ' (smoke)' : SLICE ? ' (slice)' : ''}, ${secs} s${matched ? ', state identical to the reference' : ''})`);
}
process.exit(failed ? 1 : 0);
