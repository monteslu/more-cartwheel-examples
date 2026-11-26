// arenafps: what the soldiers cost, per soldier, against the build before. The autopilot build with ARENA_CROWD=n (n
// soldiers running circles round the spawn points, the players standing still looking at them, no bots: soldier.c's
// bench crowd, and the same crowd as that build draws its fighters in tests/arenafps-anim-control.patch) through
// tests/arenafps-split-bench.mjs (native, GL and WebGPU, the main thread's CPU time per phase, the GPU time per pass,
// the draws), at 0, 1, 8 and 32 soldiers with one player and at 0 and 8 with four. Per soldier is the difference from
// no soldiers over n. Timing belongs on one GPU: run it as a timing job.
//   node tests/arenafps-anim-cost.mjs --exes <old arenafps_auto>,<new arenafps_auto> [--gpus gl,webgpu] [--runs 3]
//        [--json out/arenafps-anim-cost.json]
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 && argv[i + 1] ? argv[i + 1] : d; };
const EXES = opt('exes', '');
const GPUS = opt('gpus', 'gl,webgpu');
const RUNS = opt('runs', '3');
const OUT = resolve(opt('json', join(ROOT, 'out/arenafps-anim-cost.json')));
mkdirSync(dirname(OUT), { recursive: true });
const CASES = [[1, 0], [1, 1], [1, 8], [1, 32], [4, 0], [4, 8]];
const all = [];
for (const [players, n] of CASES) {
  const json = join(ROOT, `out/arenafps-anim-cost-${players}p-${n}.json`);
  const args = ['tests/arenafps-split-bench.mjs', '--players', String(players), '--gpus', GPUS, '--runs', RUNS, '--end', '900', '--window', '480', '--json', json];
  if (EXES) args.push('--exes', EXES);
  const r = spawnSync('node', args, { cwd: ROOT, env: { ...process.env, ARENA_BOTS: '0', ARENA_CROWD: String(n), ARENA_ID: '1' }, stdio: ['ignore', 'inherit', 'inherit'] });
  if (r.status) { console.error(`bench ${players}p ${n} failed (${r.status})`); process.exit(r.status || 1); }
  for (const x of JSON.parse(readFileSync(json, 'utf8'))) all.push({ ...x, soldiers: n });
}
writeFileSync(OUT, JSON.stringify(all, null, 1) + '\n');
// per soldier: (n soldiers - none) / n, by build, backend and players
const name = (b) => (b === 'A' ? 'before' : b === 'B' ? 'after' : 'this build');
const rows = ['| build | backend | players | soldiers | sim CPU ms | render CPU ms | GPU ms | draws | per soldier: sim CPU us | render CPU us | GPU us | draws |', '|---|---|---|---|---|---|---|---|---|---|---|---|'];
for (const x of all) {
  const z = all.find((y) => y.build === x.build && y.gpu === x.gpu && y.players === x.players && y.soldiers === 0);
  const per = (k, s = 1000) => (x.soldiers && z && x[k] != null && z[k] != null ? (((x[k] - z[k]) / x.soldiers) * s).toFixed(s === 1 ? 1 : 0) : '-');
  rows.push(`| ${name(x.build)} | ${x.gpu} | ${x.players} | ${x.soldiers} | ${x.cpu_sim} | ${x.cpu_render} | ${x.gpu_ms} | ${x.draws} | ${per('cpu_sim')} | ${per('cpu_render')} | ${per('gpu_ms')} | ${per('draws', 1)} |`);
}
console.log(rows.join('\n'));
console.error(`wrote ${OUT}`);
