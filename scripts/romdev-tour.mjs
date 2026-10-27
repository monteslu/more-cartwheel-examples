// romdev-tour.mjs <cart.wasc> <tag> [gl|wgpu]: loads the cart into romdev under the session "partypack-live", plays the
// autopilot tournament in chunks and screenshots each mini-game once mid-play, plus the board and the final podium.
// Output: out/v/<tag>-<state>-<game>.png. Reads the game's debug fields (game.mg, game.state, game.tick).
import { writeFileSync } from 'node:fs';
const [cart, tag, host = 'gl'] = process.argv.slice(2);
const ROOT = new URL('..', import.meta.url).pathname;
const call = async (tool, body) => (await fetch(`http://127.0.0.1:7331/tool/${tool}`, { method: 'POST', headers: { 'content-type': 'application/json', 'x-romdev-session': 'partypack-live' }, body: JSON.stringify(body) })).json();
const L = await call('loadMedia', { platform: 'wasmcart', path: cart, ...(host === 'gl' ? { wgpu: false } : { gpuPower: 'low-power' }) });
console.log('loaded', L.gpu, L.gpuAdapter?.device ?? '');
const NAMES = ['hurdle', 'echo', 'beatbox', 'quiz', 'blast', 'raft', 'stack', 'loop'];
const field = async (n) => { const d = await call('wasm', { op: 'debugState' }); return d.fields?.find((f) => f.name === n)?.value ?? d.fields?.find((f) => f.name === n)?.values?.[0]; };
const seen = new Set();
let frames = 0, last = '';
while (frames < 60000) {
  const r = await call('frame', { op: 'step', frames: 45 });
  if (r.error) { console.log('ERR', r.error.slice(0, 160)); break; }
  frames += 45;
  const st = await field('game.state'), mg = await field('game.mg'), tick = await field('game.tick');
  const key = `${st}-${mg}`;
  // states: 3 board, 4 intro, 6 play, 9 final
  if (st === 6 && tick > 240 && !seen.has('p' + mg)) { seen.add('p' + mg); await call('frame', { op: 'screenshot', path: `${ROOT}out/v/${tag}-play-${NAMES[mg]}.png` }); console.log('shot', NAMES[mg], frames); }
  if (st === 3 && !seen.has('board') && frames > 100) { seen.add('board'); await call('frame', { op: 'screenshot', path: `${ROOT}out/v/${tag}-board.png` }); console.log('shot board'); }
  if (st === 8 && !seen.has('r' + mg)) { seen.add('r' + mg); await call('frame', { op: 'screenshot', path: `${ROOT}out/v/${tag}-results-${NAMES[mg]}.png` }); console.log('shot results', NAMES[mg]); }
  if (st === 9 && !seen.has('final') ) { seen.add('final'); await new Promise((r) => setTimeout(r, 100)); await call('frame', { op: 'step', frames: 150 }); await call('frame', { op: 'screenshot', path: `${ROOT}out/v/${tag}-final.png` }); console.log('shot final'); }
  if (seen.size >= 30) break;
  last = key;
}
console.log('done', frames, [...seen].join(','));
