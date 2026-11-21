// arenafps split screen, wall time a frame on a host that waits for each frame (romdev's wasmcart host): the views in
// one pass (each gun on its own layer, bodies masked per view) against the old view hook (sw view_hook 1, every view
// drawn on its own), interleaved over rounds in the same match, at two and four players, GL and WebGPU, High. Run it
// on a RELEASE cart (RELEASE=1 scripts/build.sh arenafps_auto). Prints the GPU, its clock, the machine's load and
// whether another session's GPU work overlapped (contention), and each variant's median of the rounds' mean wall ms.
//   node tests/arenafps-onepass-timing.mjs [--gl] [--webgpu] [--players 2,4] [--arena 2] [--rounds 5] [--detail] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { loadavg } from 'node:os';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/arenafps_auto.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const PLAYERS = arg('--players', '2,4').split(',').map(Number);
const ARENA = +arg('--arena', '2');
const ROUNDS = +arg('--rounds', '5');
const DETAIL = process.argv.includes('--detail'); // also masks drawn view by view, and (WebGPU) the one pass with its views submitted together
const post = async (session, name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': session, 'x-romdev-agent': 'arenafps-onepass-timing' }, body: JSON.stringify(args), signal: AbortSignal.timeout(1200000) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.isError || j?.error) throw new Error(`romdev ${name}: ${t.slice(0, 600)}`);
  return j;
};
const median = (a) => { const s = [...a].sort((x, y) => x - y); return s.length % 2 ? s[(s.length - 1) / 2] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };
const roundWall = (v) => median(v.rounds.map((r) => (typeof r.wallMs === 'number' ? r.wallMs : r.wallMs.mean)));
const rows = [];
for (const backend of BACKENDS) {
  for (const players of PLAYERS) {
    const session = `arenafps-onepass-timing-${process.pid}-${backend}-${players}`;
    const loaded = await post(session, 'loadMedia', { platform: 'wasmcart', path: CART, firstCommand: `env ARENA_PLAYERS=${players} ARENA_ID=${ARENA} ARENA_MODE=0 ARENA_BOTS=4 ARENA_REPEAT=1`,
      wgpu: backend === 'webgpu', gpuPower: 'low-power', ...(backend === 'webgpu' ? { gpuTiming: true } : {}), preferredSize: { width: 1920, height: 1080 } });
    try {
      if (loaded.gpu !== backend) throw new Error(`the cart runs on ${loaded.gpu}, not ${backend}`);
      await post(session, 'wasm', { op: 'command', text: 'quality high' });
      await post(session, 'frame', { op: 'step', frames: 400 }); // into the match
      const variants = [
        { name: 'view hook (each view on its own)', commands: ['sw view_hook 1', 'pipeline views_array 1', 'pipeline views_submit 1'] },
        { name: 'one pass (masks, gun layers)', commands: ['sw view_hook 0', 'pipeline views_array 1', 'pipeline views_submit 1'] },
        ...(DETAIL ? [
          { name: 'masks, each view on its own', commands: ['sw view_hook 0', 'pipeline views_array 0', 'pipeline views_submit 1'] },
          ...(backend === 'webgpu' ? [{ name: 'one pass, views submitted together', commands: ['sw view_hook 0', 'pipeline views_array 1', 'pipeline views_submit 0'] }] : []),
        ] : []),
      ];
      const load = loadavg()[0];
      const t = await post(session, 'frame', { op: 'timing', frames: 240, warmup: 60, window: 120, rounds: ROUNDS, variants, maxSeconds: 900 });
      const clock = t.gpu && t.gpu.clockMhz ? `${Math.round(t.gpu.clockMhz.mean)} MHz` : '?';
      const dev = t.gpu?.adapter?.device || t.gpu?.glRenderer || '?';
      console.log(`-- ${backend} ${players}p (${dev}, GPU clock ${clock}, load ${load.toFixed(2)}, contention ${t.contention?.noisy ? 'NOISY' : 'quiet'})`);
      const out = {};
      for (const v of t.variants) {
        out[v.name] = roundWall(v);
        console.log(`     ${v.name.padEnd(34)} wall ${roundWall(v).toFixed(2)} ms (rounds ${v.rounds.map((r) => (typeof r.wallMs === 'number' ? r.wallMs : r.wallMs.mean).toFixed(2)).join('/')}), cpu ${v.cpuMs.mean.toFixed(2)}, gpu ${v.gpuMs ? v.gpuMs.mean.toFixed(2) : '-'}`);
      }
      const hook = out[variants[0].name], one = out[variants[1].name];
      rows.push({ backend, players, hook, one, clock, load, noisy: !!t.contention?.noisy });
    } finally {
      await post(session, 'host', { op: 'shutdown' }).catch(() => {});
    }
  }
}
console.log('\n| host | players | view hook ms | one pass ms | change | GPU clock | load |');
console.log('|---|---|---|---|---|---|---|');
for (const r of rows) console.log(`| ${r.backend} | ${r.players} | ${r.hook.toFixed(2)} | ${r.one.toFixed(2)} | ${(100 * (r.one / r.hook - 1)).toFixed(1)}% | ${r.clock} | ${r.load.toFixed(2)}${r.noisy ? ' (noisy)' : ''} |`);
