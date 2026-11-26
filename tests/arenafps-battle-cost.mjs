// arenafps: what the background battle (crowd.c) costs, one view at High: tests/arenafps-split-bench.mjs with the
// battle off and on (ARENA_BATTLE_BG), in turn, on Courtyard (the arena whose valley holds the most of it) with the
// first view looking out at it, no bots (the soldiers are not what is measured). A timing job: on a pool lane the
// lane's scale (CW_GPU_TIMING_SCALE_GL / _WEBGPU, the slope b of lane = a + b x ref) turns the GPU difference into
// 890M units.
//   node tests/arenafps-battle-cost.mjs [--gpus webgpu,gl] [--rounds 3] [--view x,y,z,yaw,pitch]
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 && argv[i + 1] ? argv[i + 1] : d; };
const GPUS = opt('gpus', 'webgpu,gl').split(',');
const ROUNDS = +opt('rounds', '3');
const VIEW = opt('view', '');
const OUT = join(ROOT, 'out/arenafps-battle-cost');
mkdirSync(OUT, { recursive: true });
const slope = (gpu) => { try { const v = JSON.parse(process.env[`CW_GPU_TIMING_SCALE_${gpu === 'gl' ? 'GL' : 'WEBGPU'}`] || 'null'); return v && v.b > 0 ? v.b : null; } catch { return null; } }; // {a, b, ...}: lane ms = a + b x 890M ms
const rows = [];
for (let r = 0; r < ROUNDS; r++)
  for (const on of ['0', '1']) {
    const json = join(OUT, `r${r}-${on}.json`);
    const env = { ...process.env, ARENA_BATTLE_BG: on, ARENA_ID: '1', ARENA_MODE: '1', ARENA_BOTS: '0', CW_QUALITY: 'high' };
    if (VIEW) env.ARENA_VIEW = VIEW;
    const res = spawnSync('node', ['tests/arenafps-split-bench.mjs', '--players', '1', '--gpus', GPUS.join(','), '--runs', '3', '--end', '900', '--window', '480', '--json', json], { cwd: ROOT, env, stdio: ['ignore', 'inherit', 'inherit'] });
    if (res.status) { console.error(`bench failed (${res.status})`); process.exit(res.status || 1); }
    for (const x of JSON.parse(readFileSync(json, 'utf8'))) rows.push({ round: r, on: +on, ...x });
  }
const med = (a) => { const s = [...a].sort((x, y) => x - y); return s[Math.floor(s.length / 2)]; };
const table = ['| backend | battle | GPU ms (median of rounds) | scene pass GPU ms | draws | render CPU ms | frame ms |', '|---|---|---|---|---|---|---|'];
const result = {};
for (const gpu of GPUS) {
  for (const on of [0, 1]) {
    const xs = rows.filter((x) => x.gpu === gpu && x.on === on);
    const g = med(xs.map((x) => x.gpu_ms)), sc = med(xs.map((x) => (x.gpu_passes && (x.gpu_passes.scene ?? x.gpu_passes.main)) || 0));
    result[`${gpu}-${on}`] = g;
    table.push(`| ${gpu} | ${on ? 'on' : 'off'} | ${g} | ${sc} | ${med(xs.map((x) => x.draws))} | ${med(xs.map((x) => x.cpu_render))} | ${med(xs.map((x) => x.frame))} |`);
  }
  const d = result[`${gpu}-1`] - result[`${gpu}-0`], b = slope(gpu);
  table.push(`| ${gpu} | difference | ${d.toFixed(2)} lane ms${b ? `, ${(d / b).toFixed(2)} ms in 890M units (lane slope ${b})` : ' (no lane scale: this GPU\'s own ms)'} | | | | |`);
}
console.log(table.join('\n'));
writeFileSync(join(OUT, 'rows.json'), JSON.stringify(rows, null, 1) + '\n');
