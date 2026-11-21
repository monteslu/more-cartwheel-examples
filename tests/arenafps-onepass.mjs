// arenafps split screen in one pass, on the cart through romdev, GL and WebGPU: each player's gun on a layer of its own
// (only that player's camera draws it) and each body's views given by a mask (never its own player's view, not a view
// whose camera it is right up against) keep the frame in one pass (the engine's `pipeline` reply: views_array true,
// views_array_why empty), at two and four players, on every arena. The picture is the one the old view hook drew view
// by view (sw view_hook 1, the same match on the same frames): pixels over 16 levels within 0.3% at each shot. The
// simulation is the same (game.hash).
// Control: sw tune mask_shift 1 gives each body the next view's mask (a player's own body drawn in its own view, from
// inside it) and must fail the bound.
// Also samples the one pass every 60 frames over a stretch of the match (explosions bend the picture through the
// distortion pass, which draws view by view while a shockwave is out).
//   node tests/arenafps-onepass.mjs [--gl] [--webgpu] [--players 2,4] [--arenas 0,1,2] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/arenafps-onepass');
const { readImage, diffImages } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/arenafps_auto.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const PLAYERS = arg('--players', '2,4').split(',').map(Number);
const ARENAS = arg('--arenas', '0,1,2').split(',').map(Number);
const SHOTS = [900, 1500];
const BOUND = 0.003;
const NAMES = ['foundry', 'courtyard', 'terraces'];
mkdirSync(OUT, { recursive: true });

const sessions = new Set();
const post = async (session, name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': session, 'x-romdev-agent': 'arenafps-onepass' }, body: JSON.stringify(args), signal: AbortSignal.timeout(600000) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.isError || j?.error) throw new Error(`romdev ${name}: ${t.slice(0, 600)}`);
  return j;
};
const bye = async (code) => {
  for (const s of sessions) { try { await post(s, 'host', { op: 'shutdown' }); } catch { /* gone */ } }
  process.exit(code);
};
let fail = 0;
const check = (what, ok, detail = '') => {
  if (!ok) fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${detail ? ': ' + detail : ''}`);
};
const pct = (x) => `${(100 * x).toFixed(4)}%`;

/** one run of a match: shots at SHOTS, the pipeline reply and game.hash at each */
async function run(backend, players, arena, tag, extraEnv, sample) {
  const session = `arenafps-onepass-${process.pid}-${backend}-${players}-${arena}-${tag}`;
  sessions.add(session);
  const env = `env ARENA_PLAYERS=${players} ARENA_ID=${arena} ARENA_MODE=0 ARENA_BOTS=4${extraEnv ? ' ' + extraEnv : ''}`;
  const loaded = await post(session, 'loadMedia', { platform: 'wasmcart', path: CART, firstCommand: env, deterministicSeed: 1, wgpu: backend === 'webgpu', gpuPower: 'low-power', preferredSize: { width: 1280, height: 720 } });
  if (loaded.gpu !== backend) throw new Error(`the cart runs on ${loaded.gpu}, not ${backend}`);
  await post(session, 'wasm', { op: 'command', text: 'quality high' }); // a fixed preset (Auto would step down on a busy host)
  let frame = 2;
  const shots = [];
  let samples = 0, onepass = 0;
  const whys = new Set();
  for (const at of SHOTS) {
    if (sample) { // the one pass sampled every 60 frames on the way
      while (frame + 61 <= at) { // (a frame left for the shot's own)
        await post(session, 'frame', { op: 'step', frames: 59 });
        const p = (await post(session, 'wasm', { op: 'command', text: 'pipeline' })).reply;
        frame += 60;
        samples++;
        if (p.views_array) onepass++;
        else whys.add(p.views_array_why);
      }
    }
    if (at - frame - 1 > 0) await post(session, 'frame', { op: 'step', frames: at - frame - 1 });
    const pipe = (await post(session, 'wasm', { op: 'command', text: 'pipeline' })).reply; // (runs frame `at`)
    frame = at;
    const path = join(OUT, `${NAMES[arena]}-${players}p-${backend}-${tag}-f${at}.png`);
    await post(session, 'frame', { op: 'screenshot', path });
    const hash = (await post(session, 'wasm', { op: 'read', name: 'game.hash' })).value;
    shots.push({ at, path, img: readImage(path), pipe, hash });
  }
  await post(session, 'host', { op: 'shutdown' });
  sessions.delete(session);
  return { shots, samples, onepass, whys: [...whys] };
}

try {
  for (const backend of BACKENDS) {
    console.log(`-- ${backend}`);
    for (const players of PLAYERS) {
      for (const arena of ARENAS) {
        const what = `${NAMES[arena]} ${players}p`;
        const masks = await run(backend, players, arena, 'masks', '', true);
        const hook = await run(backend, players, arena, 'hook', 'ARENA_T_view_hook=1', false);
        const ctl = arena === ARENAS[0] ? await run(backend, players, arena, 'control', 'ARENA_T_mask_shift=1', false) : null;
        for (let i = 0; i < SHOTS.length; i++) {
          const m = masks.shots[i], h = hook.shots[i];
          check(`${what} f${m.at}: one pass (views_array, nothing named)`, m.pipe.views_array === true && !m.pipe.views_array_why && !m.pipe.view_hook,
            `views_array ${m.pipe.views_array} why "${m.pipe.views_array_why}" view_hook ${m.pipe.view_hook} masks ${m.pipe.view_masks}`);
          check(`${what} f${m.at}: the hook's run drew view by view`, h.pipe.views_array === false && h.pipe.view_hook === true, `why "${h.pipe.views_array_why}"`);
          check(`${what} f${m.at}: the same match (game.hash)`, m.hash === h.hash, `${m.hash} / ${h.hash}`);
          const d = diffImages(m.img, h.img).gt16;
          check(`${what} f${m.at}: the hook's picture (pixels over 16 levels <= ${BOUND * 100}%)`, d <= BOUND, pct(d));
          if (ctl) {
            const dc = diffImages(ctl.shots[i].img, h.img).gt16;
            check(`${what} f${m.at}: control (each body's mask a view over) must fail the bound`, dc > BOUND, pct(dc));
          }
        }
        console.log(`     ${what}: one pass on ${masks.onepass} of ${masks.samples} sampled frames${masks.whys.length ? ` (else: ${masks.whys.join(', ')})` : ''}`);
      }
    }
  }
} catch (e) {
  console.error(e.message);
  await bye(3);
}
console.log(fail ? `arenafps-onepass: ${fail} check(s) failed` : 'arenafps-onepass: ok');
await bye(fail ? 1 : 0);
