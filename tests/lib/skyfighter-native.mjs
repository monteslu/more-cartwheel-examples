// skyfighter's native build run headless on software GL (no GPU): one sortie launched from the title, the debug
// state at the last frame (CW_DEBUG_DUMP) and a screenshot of it. Used by tests/skyfighter-feel.mjs.
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const arg = (name, def) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : def; };
const exe = join(resolve(ROOT, arg('--build', 'build/native')), 'games/skyfighter/skyfighter');
export const OUT = join(ROOT, 'out/skyfighter-feel');
mkdirSync(OUT, { recursive: true });
/* one run. input: [[frame, buttons, rt]] from frame 25 (South on the title launches the selected mission at 25);
 * cmds: debug requests "@frame command"; shot: a png name to keep (1920x1080) */
export function run({ tag, mission, frames, quality = 'verylow', cmds = [], input = [], shot = null, hideHud = false, scale = 2, extraEnv = {} }) {
  const dump = join(OUT, `${tag}.json`), script = join(OUT, `${tag}.input`);
  const lines = ['25 1', '28 0', ...input.map(([f, b, rt = 0]) => `${f} ${b} 0 0 0 0 0 ${rt}`)];
  writeFileSync(script, lines.join('\n') + '\n');
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', LIBGL_ALWAYS_SOFTWARE: '1', GALLIUM_DRIVER: 'llvmpipe', CW_GPU: 'gl', SDL_AUDIODRIVER: 'dummy',
    LP_NUM_THREADS: process.env.LP_NUM_THREADS || '4', CW_WORKERS: '2', CW_FRAMES: String(frames), CW_WARMUP_SPREAD: '0', CW_QUALITY: quality,
    CW_PIXEL_SCALE: String(scale), CW_INPUT: script, SKY_ART_MISSION: String(mission), CW_DEBUG_DUMP: dump, CW_START_CMDS: cmds.join(';'), ...extraEnv };
  if (shot) env.CW_SCREENSHOT = join(OUT, shot);
  if (hideHud) env.SKY_ART_HIDE_HUD = '1';
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  return new Promise((done, fail) => {
    const p = spawn('nice', ['-n', '10', exe], { env });
    let log = '';
    p.stderr.on('data', (d) => { log += d; }); p.stdout.on('data', (d) => { log += d; });
    const timer = setTimeout(() => p.kill('SIGKILL'), 900e3);
    p.on('close', (code) => {
      clearTimeout(timer);
      if (code !== 0) return fail(new Error(`${tag}: exit ${code}\n${log.slice(-1200)}`));
      const j = JSON.parse(readFileSync(dump, 'utf8'));
      rmSync(script, { force: true });
      if (j['mission.state'] !== 1 && j['mission.state'] !== 6) return fail(new Error(`${tag}: mission ${mission} not in play (state ${j['mission.state']})\n${log.slice(-600)}`));
      j.log = log; done(j);
    });
  });
}
export async function pool(tasks, jobs = Number(arg('--jobs', 3))) {
  const results = []; let next = 0;
  await Promise.all(Array.from({ length: jobs }, async () => { while (next < tasks.length) { const i = next++; results[i] = await tasks[i](); } }));
  return results;
}
