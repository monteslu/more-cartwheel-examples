// arenafps: the Battle mode's far soldier and background battle, on the native autopilot build (GL and WebGPU). Run it
// as a GPU job (with-gpu-suite.mjs, or a pool lane):
//   node tests/arenafps-battle.mjs [--old <arenafps_auto before>] [--gpus gl,webgpu] [--out out/arenafps-battle]
//
//   soldier   the same picture at 5 m: a camera 5 m in front of a bot (ARENA_CHASE), the build before against this one,
//             must match; the control (ARENA_SOLDIER_LOD=2, the far mesh at 5 m) must differ
//   battle    the background battle on Courtyard from a player's eye on the roof across the arena, looking out over the
//             far wall at each of its zones, with it off (the before) and on: on must change the picture there, and no
//             draw may lack its program (the engine's red banner fails the run)
// Pictures land in --out; the pass or fail of each check is printed, and the exit status is 1 on any failure.
import { spawn } from 'node:child_process';
import { mkdirSync, existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const { readImage, diffImages, diffAreas } = await import(join(ENGINE, 'tools/lib/png.mjs'));

const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 && argv[i + 1] ? argv[i + 1] : d; };
const GPUS = opt('gpus', 'gl,webgpu').split(',');
const OUT = resolve(opt('out', join(ROOT, 'out/arenafps-battle')));
const ONLY = opt('only', 'soldier,battle').split(',');
mkdirSync(OUT, { recursive: true });
const built = (() => {
  try { return JSON.parse(readFileSync(join(ROOT, 'build/native/apps/arenafps_auto.json'), 'utf8')).binary; } catch { /* below */ }
  return join(ROOT, 'build/native/games/arenafps/arenafps_auto');
})();
const NEW = resolve(opt('new', built));
const OLD = opt('old', '') && resolve(opt('old', ''));

async function run(exe, gpu, extra, frames, shot) {
  const env = { ...process.env, CW_FRAMES: String(frames), SDL_AUDIODRIVER: 'dummy', ARENA_PLAYERS: extra.ARENA_PLAYERS || '1', ARENA_RES: '1920x1080', CW_WGPU_VALIDATION: '0',
    ARENA_NOHUD: '1', CW_QUALITY: 'high', CW_WARMUP_SPREAD: '0', CW_DEBUG_CMDS: 'perf 60;pipeline', ...extra };
  if (shot) env.CW_SCREENSHOT = shot;
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = await new Promise((done) => {
    const c = spawn('timeout', ['-s', 'KILL', '300', exe], { env });
    let stdout = '', stderr = '';
    c.stdout.on('data', (d) => (stdout += d));
    c.stderr.on('data', (d) => (stderr += d));
    c.on('close', (status) => done({ status, stdout, stderr }));
  });
  if (r.status !== 0) throw new Error(`${exe} ${gpu} exited ${r.status}: ${r.stderr.slice(-1500)}`);
  const json = r.stdout.split('\n').filter((l) => l.startsWith('{')).map((l) => { try { return JSON.parse(l); } catch { return {}; } });
  const perf = json.find((j) => j.frames !== undefined) || {};
  const pipe = json.find((j) => j.passes) || {};
  return { ...r, perf, pipe };
}

let fail = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); if (!ok) fail++; };
const summary = [];

for (const gpu of GPUS) {
  if (ONLY.includes('soldier')) {
    // the second seat's soldier (its own eye is on it, so its detail is the nearest's) seen from 5 m in front by the
    // first view's camera, on its chest: the near mesh, the same before and after
    const cam = { ARENA_ID: '1', ARENA_MODE: '0', ARENA_BOTS: '2', ARENA_PLAYERS: '2', ARENA_CHASE: '1,5,0,1.5,1.2' };
    const shots = {};
    for (const [k, exe, extra] of [['old', OLD, {}], ['new', NEW, {}], ['control', NEW, { ARENA_SOLDIER_LOD: '2' }]]) {
      if (!exe) continue;
      const f = join(OUT, `soldier5m-${k}-${gpu}.png`);
      const r = await run(exe, gpu, { ...cam, ...extra }, 400, f);
      shots[k] = f;
      const lines = r.stderr.split('\n').filter((l) => /soldier mesh level/.test(l));
      summary.push({ gpu, case: `soldier5m-${k}`, draws: r.perf.draw_calls ?? r.pipe.draw_calls, tris: r.perf.triangles ?? r.pipe.triangles, levels: lines.map((l) => l.replace(/.*arenafps: /, '')) });
    }
    if (shots.old) {
      const d = diffImages(readImage(shots.old), readImage(shots.new));
      check(d.comparable && d.gt2 === 0, `${gpu} soldier at 5 m: this build against the one before (${(100 * (d.gt2 || 0)).toFixed(4)}% of pixels off by more than 2, max ${d.max})`);
    }
    const c = diffImages(readImage(shots.new), readImage(shots.control)), ca = diffAreas(readImage(shots.new), readImage(shots.control), { level: 8, size: 3 });
    check(c.comparable && c.gt2 > 0, `${gpu} control (the far mesh at 5 m) differs: ${(100 * (c.gt2 || 0)).toFixed(3)}% of pixels off by more than 2, areas ${(100 * (ca.share || 0)).toFixed(3)}%`);
  }
  if (ONLY.includes('battle')) {
    for (const id of ['1']) {
      // where the zones are: a short run that logs them
      const probe = await run(NEW, gpu, { ARENA_ID: id, ARENA_MODE: '1', ARENA_BOTS: '4', ARENA_BATTLE_BG: '1' }, 30, '');
      const zones = [...probe.stderr.matchAll(/crowd: zone (\d+) at (-?\d+) (-?\d+)/g)].map((m) => ({ k: +m[1], x: +m[2], z: +m[3] }));
      check(zones.length > 0, `${gpu} arena ${id}: ${zones.length} zones (${zones.map((z) => `${z.x},${z.z}`).join(' ')})`);
      const half = id === '1' ? 30 : 26;
      for (const z of zones.slice(0, 2)) {
        const r = Math.hypot(z.x, z.z), ex = -(z.x / r) * 15, ez = -(z.z / r) * 15, ey = 5.5;
        const yaw = Math.atan2(-(z.x - ex), -(z.z - ez)), pitch = 0.035;
        const view = `${ex.toFixed(2)},${ey},${ez.toFixed(2)},${yaw.toFixed(4)},${pitch.toFixed(4)}`;
        const pics = {};
        for (const on of ['0', '1']) {
          const f = join(OUT, `battle-a${id}-z${z.k}-${on === '1' ? 'on' : 'off'}-${gpu}.png`);
          const res = await run(NEW, gpu, { ARENA_ID: id, ARENA_MODE: '1', ARENA_BOTS: '0', ARENA_BATTLE_BG: on, ARENA_VIEW: view }, 420, f);
          pics[on] = f;
          check(!/have no program/.test(res.stderr), `${gpu} arena ${id} zone ${z.k} battle ${on === '1' ? 'on' : 'off'}: every draw has its program`);
          summary.push({ gpu, case: `battle-a${id}-z${z.k}-${on}`, view, draws: res.perf.draw_calls ?? res.pipe.draw_calls, tris: res.perf.triangles ?? res.pipe.triangles });
        }
        const d = diffAreas(readImage(pics['0']), readImage(pics['1']), { level: 10, size: 2 });
        check(d.comparable && d.share > 0, `${gpu} arena ${id} zone ${z.k}: the battle changes the picture past the walls (${(100 * (d.share || 0)).toFixed(3)}% in blocks)`);
      }
    }
  }
}
writeFileSync(join(OUT, 'summary.json'), JSON.stringify(summary, null, 1) + '\n');
for (const s of summary) console.log(JSON.stringify(s));
process.exit(fail ? 1 : 0);
