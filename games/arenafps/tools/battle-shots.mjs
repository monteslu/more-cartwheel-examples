// Native-scale screenshots of the Battle map (FARMLANDS) from eye height: at the posts, along the lanes, and the player's
// own view in a match (gun and HUD). Writes out/gate/battle-shots/<name>.png and .log.
//   node games/arenafps/tools/battle-shots.mjs [gl|webgpu ...] [--res 1920x1080] [--quality high] [--only name,name]
//        [--env K=V,K=V] [--tag t]   (the env pairs go to the game; the tag ends each picture's name)
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { battle, BATTLE } from './battle-map.mjs';
const GAME = resolve(dirname(fileURLToPath(import.meta.url)), '..'), ROOT = resolve(GAME, '../..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const gpus = process.argv.slice(2).filter((a) => a === 'gl' || a === 'webgpu');
const RES = arg('--res', '1920x1080'), Q = arg('--quality', 'high'), ONLY = arg('--only', ''), TAG = arg('--tag', '');
// --env K=V,K=V: more for the game (ARENA_MODE=3 for a Battle match, ARENA_BATTLE_BG=1 for the background battle)
const EXTRA = Object.fromEntries(arg('--env', '').split(',').filter(Boolean).map((kv) => kv.split('=')));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/arenafps_auto.json'), 'utf8')).binary;
const out = join(ROOT, 'out/gate/battle-shots');
mkdirSync(out, { recursive: true });
const G = battle(join(GAME, 'levels'), false).ground, P = BATTLE.posts;
// a view from (x, z) at eye height toward (tx, tz)
const view = (x, z, tx, tz, up = 1.6, pitch = -0.04) => `${x},${G(x, z) + up},${z},${Math.atan2(-(tx - x), -(tz - z))},${pitch}`;
const mid = (a, b, t, off = 0) => { const dx = P[b].x - P[a].x, dz = P[b].z - P[a].z, l = Math.hypot(dx, dz); return [P[a].x + dx * t - (dz / l) * off, P[a].z + dz * t + (dx / l) * off]; };
const specs = [
  { name: 'farm-to-crossroads', env: { ARENA_VIEW: view(P[0].x + 9, P[0].z - 1, P[2].x, P[2].z) } },
  { name: 'crossroads-ring', env: { ARENA_VIEW: view(P[2].x - 10, P[2].z - 6, P[2].x, P[2].z, 1.6, -0.12) } },
  { name: 'lane-relay-crossroads', env: { ARENA_VIEW: view(...mid(1, 2, 0.35, 2), P[2].x, P[2].z) } },
  { name: 'lane-farm-relay', env: { ARENA_VIEW: view(...mid(0, 1, 0.45, -1.5), P[1].x, P[1].z) } },
  { name: 'depot-ruin', env: { ARENA_VIEW: view(P[3].x + 6, P[3].z - 4, P[3].x - 14, P[3].z + 18) } },
  { name: 'flank-relay-quarry', env: { ARENA_VIEW: view(...mid(1, 4, 0.5, 3), P[4].x, P[4].z) } },
  { name: 'edge-wall', env: { ARENA_VIEW: view(P[4].x + 18, P[4].z - 30, P[4].x + 28, P[4].z - 75) } },
  { name: 'overview', env: { ARENA_VIEW: `${-104},30,${84},${Math.atan2(-104, 84)},-0.3` } },
  { name: 'player', env: {}, frames: 420 },
  // toward the background battle past the field wall (src/crowd.c finds its zones on the slopes there)
  { name: 'zone-west', env: { ARENA_VIEW: view(-92, 16, -118, 10, 1.6, 0.04) } },
  { name: 'zone-north', env: { ARENA_VIEW: view(36, -88, 53, -129, 1.6, 0.06) } },
  { name: 'zone-south', env: { ARENA_VIEW: view(-18, 86, -28, 126, 1.6, 0.08) } },
];
for (const gpu of gpus.length ? gpus : ['gl'])
  for (const s of specs) {
    if (ONLY && !ONLY.split(',').includes(s.name)) continue;
    const name = `${s.name}-${gpu}${TAG ? '-' + TAG : ''}`;
    const env = { ...process.env, ARENA_ID: '3', ARENA_PLAYERS: '1', ARENA_BOTS: '8', ARENA_MODE: '1', CW_GPU: gpu, CW_FRAMES: String(s.frames || 240), CW_WARMUP_SPREAD: '0',
      SDL_AUDIODRIVER: 'dummy', SDL_VIDEODRIVER: 'offscreen', ARENA_RES: RES, CW_QUALITY: Q, CW_SCREENSHOT: join(out, name + '.png'), ...EXTRA, ...s.env };
    for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
    const p = spawnSync('timeout', ['300', exe], { env, encoding: 'utf8' });
    const log = (p.stdout || '') + (p.stderr || '');
    writeFileSync(join(out, name + '.log'), log);
    const errs = log.split('\n').filter((l) => /error|missing|float/i.test(l)).slice(0, 4);
    console.log(`${p.status === 0 ? 'ok  ' : 'FAIL'} ${name} ${s.env.ARENA_VIEW || ''}${errs.length ? '\n    ' + errs.join('\n    ') : ''}`);
  }
