// Is anything floating? For a course, a held camera close by: where its scatter is densest, every kind of thing placed by hand, its ramps, its landmark and
// hook, and the shore all the way round the course. Native-scale pictures and contact sheets to look at, pages of nine.
//   node games/jetski/tools/grounding-sweep.mjs <cart> <course id> <out dir> [--gl] [--keep]
import { ROMDEV_URL } from '../../../tests/lib/romdev-url.mjs';
import { mkdirSync, readFileSync, readdirSync, rmSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { readCourse, COURSE_IDS } from './course-read.mjs';
const HERE = dirname(fileURLToPath(import.meta.url));
const URL_ = ROMDEV_URL+'/mcp', CART = resolve(process.argv[2]), ID = process.argv[3], DIR = resolve(process.argv[4]), GL = process.argv.includes('--gl');
const SESSION = process.env.JETSKI_SESSION || 'jetski';
const RAW = join(DIR, ID + '-raw');
mkdirSync(RAW, { recursive: true });
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
const c = readCourse(ID), LEVEL = COURSE_IDS.indexOf(ID);
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-ground', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const cmd = async (text) => (await call('wasm', { op: 'command', text })).content[0].text;
await call('frame', { op: 'step', frames: 30 });
await cmd(`course ${LEVEL}`);
// out of the menu and past the countdown, so the pictures are clear but for the small HUD
await cmd('race');
await call('frame', { op: 'step', frames: 260 });
console.log(ID, 'grounding', (await cmd('grounding')).replace(/\s+/g, ' ').slice(0, 300));
// [label, x, z, how far off, how high over the ground or water]
const spots = [];
const N = 448, SIZE = c.quads * c.spacing;
for (const m of c.maps) {
  const D = readFileSync(join(HERE, `../assets/course/${ID}/${m}.u8`)), cand = [];
  for (let j = 8; j < N - 8; j += 16) for (let i = 8; i < N - 8; i += 16) if (D[j * N + i] > 120) cand.push([((i + 0.5) / N - 0.5) * SIZE, ((j + 0.5) / N - 0.5) * SIZE]);
  const step = Math.max(1, Math.floor(cand.length / 6));
  for (let k = 0, n = 0; k < cand.length && n < 6; k += step, n++) spots.push([m, cand[k][0], cand[k][1], 26, 8]);
}
const reach = { ship: 60, wreck: 60, rig: 70, warehouse: 48, block: 50, containers: 40, crane: 50, lamp: 16, tower: 26 };
c.models.forEach((name, m) => {
  const all = c.placed.filter((p) => p[0] === m), step = Math.max(1, Math.floor(all.length / 5));
  for (let k = 0, n = 0; k < all.length && n < 5; k += step, n++) spots.push([name, all[k][1], all[k][2], reach[name] ?? 40, 7]);
});
for (let k = 0, n = c.ramps.length, st = Math.max(1, Math.floor(n / 4)); k < n; k += st) spots.push(['ramp', c.ramps[k][0], c.ramps[k][1], 38, 4]);
if (c.landmark && !(c.landmark[0] === 0 && c.landmark[2] === 0)) spots.push(['landmark', c.landmark[0], c.landmark[2], 60, 14]);
if (c.hook?.kind) spots.push(['hook', c.hook.x, c.hook.z, 70, 12]);
// the shore all the way round: from the racing line, sideways, each bank in turn
const SHORE = 24;
const shores = [];
for (let k = 0; k < SHORE; k++) {
  const t = c.track[Math.floor((k / SHORE) * c.track.length)], u = c.track[(Math.floor((k / SHORE) * c.track.length) + 3) % c.track.length];
  const dx = u[0] - t[0], dz = u[1] - t[1], l = Math.hypot(dx, dz) || 1, side = k % 2 ? 1 : -1;
  shores.push(['shore' + k, t[0], t[1], t[0] - (dz / l) * side * 90, t[1] + (dx / l) * side * 90]);
}
let n = 0;
for (const [m, x, z, off, up] of spots) {
  const g = JSON.parse(await cmd(`peek 0 0 0 ${x} 0 ${z}`)).reply.ground, base = Math.max(g, 0);
  // from the side of the thing the open air is: try the four sides, the first one with the camera over the ground or water nearby and not inside a wall
  let cx = x + off * 0.84, cz = z + off * 0.54;
  for (const [a, b] of [[0.84, 0.54], [-0.84, 0.54], [0.84, -0.54], [-0.84, -0.54]]) {
    const h = JSON.parse(await cmd(`peek 0 0 0 ${x + off * a} 0 ${z + off * b}`)).reply.ground;
    if (h < base + 4) { cx = x + off * a, cz = z + off * b; break; }
  }
  const gc = JSON.parse(await cmd(`peek 0 0 0 ${cx} 0 ${cz}`)).reply.ground;
  await cmd(`peek ${cx} ${Math.max(gc, 0) + up} ${cz} ${x} ${base + 1} ${z}`);
  await call('frame', { op: 'stepAndShot', frames: 3, path: `${RAW}/${String(n++).padStart(3, '0')}-${m}.png` });
}
for (const [m, x, z, ax, az] of shores) {
  await cmd(`peek ${x} 6 ${z} ${ax} 6 ${az}`);
  await call('frame', { op: 'stepAndShot', frames: 3, path: `${RAW}/${String(n++).padStart(3, '0')}-${m}.png` });
}
await cmd('peek');
console.log(ID, n, 'pictures');
const all = readdirSync(RAW).filter((f) => f.endsWith('.png')).sort();
for (let p = 0, page = 0; p < all.length; p += 9, page++) {
  const r = spawnSync('montage', [...all.slice(p, p + 9).map((f) => join(RAW, f)), '-geometry', '640x360+2+2', '-tile', '3x3', '-label', '%t', '-pointsize', '16', '-quality', '90', join(DIR, `${ID}-sweep-${page}.jpg`)], { encoding: 'utf8' });
  if (r.status) throw new Error('montage: ' + r.stderr);
}
if (!process.argv.includes('--keep')) rmSync(RAW, { recursive: true, force: true });
process.exit(0);
