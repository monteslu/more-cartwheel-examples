// Is anything floating? For each course: the places its scatter is densest on slopes, its landmark and its hook, shot from close
// by with a held camera, native scale, to look at. Prints each place's ground height. A person (or agent) looks at the sheet.
//   node games/jetski/tools/grounding-sweep.mjs <cart> <course index> <course id> <map,map> <out dir> [--gl]
import { mkdirSync, readFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const HERE = dirname(fileURLToPath(import.meta.url));
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), LEVEL = +process.argv[3], ID = process.argv[4], MAPS = process.argv[5].split(','), DIR = resolve(process.argv[6]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski-ground';
mkdirSync(DIR, { recursive: true });
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text();
  const line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
const call = async (name, args) => {
  const r = await rpc('tools/call', { name, arguments: args });
  if (r.error || r.result?.isError) throw new Error(name + ' ' + JSON.stringify(r.error || r.result).slice(0, 300));
  return r.result;
};
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-ground', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const cmd = async (text) => (await call('wasm', { op: 'command', text })).content[0].text;
await call('frame', { op: 'step', frames: 30 });
await cmd(`course ${LEVEL}`);
const N = 448, SIZE = 1792, spots = [];
for (const m of MAPS) {
  const D = readFileSync(join(HERE, `../assets/course/${ID}/${m}.u8`));
  const cand = [];
  for (let j = 8; j < N - 8; j += 16) for (let i = 8; i < N - 8; i += 16) if (D[j * N + i] > 120) cand.push([((i + 0.5) / N - 0.5) * SIZE, ((j + 0.5) / N - 0.5) * SIZE]);
  const step = Math.max(1, Math.floor(cand.length / 8));
  for (let k = 0, n = 0; k < cand.length && n < 8; k += step, n++) spots.push([m, ...cand[k]]);
}
let n = 0;
for (const [m, x, z] of spots) {
  const g = JSON.parse((await cmd(`peek 0 0 0 ${x} 0 ${z}`))).reply.ground;
  await cmd(`peek ${x + 22} ${g + 7} ${z + 14} ${x} ${g + 1} ${z}`);
  const p = `${DIR}/${String(n++).padStart(2, '0')}-${m}.png`;
  await call('frame', { op: 'stepAndShot', frames: 3, path: p });
  console.log(m, x.toFixed(0), z.toFixed(0), 'ground', g.toFixed(1));
}
process.exit(0);
