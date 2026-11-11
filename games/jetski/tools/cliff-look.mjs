// The cliffs a rider sees on their left and right at speed, from the race camera on a real ride of the cart through romdev: a burst of pictures at each of the tallest
// walls of a course, with the land hidden once so the crop can be cliff only (tools/cliff-measure.mjs).
//   node games/jetski/tools/cliff-look.mjs <cart> <out dir> [--gl] [--courses=a,b] [--old]   (--old: a cart without the land mask commands' freeze: pictures only)
import { ROMDEV_URL } from '../../../tests/lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { readCourse, COURSE_IDS } from './course-read.mjs';
const URL_ = ROMDEV_URL+'/mcp', CART = resolve(process.argv[2]), DIR = resolve(process.argv[3]), GL = process.argv.includes('--gl'), OLD = process.argv.includes('--old');
const SESSION = process.env.JETSKI_SESSION || 'jetski';
const IDS = (process.argv.find((a) => a.startsWith('--courses='))?.slice(10) || COURSE_IDS.join(',')).split(',');
mkdirSync(DIR, { recursive: true });
let sid = null, id = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: id++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text(), line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
const call = async (name, args) => { const r = await rpc('tools/call', { name, arguments: args }); if (r.error || r.result?.isError) throw new Error(name + ' ' + JSON.stringify(r.error || r.result).slice(0, 300)); return r.result; };
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-cliff', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const cmd = (text) => call('wasm', { op: 'command', text });
await call('frame', { op: 'step', frames: 30 });
for (const cid of IDS) {
  const c = readCourse(cid), level = COURSE_IDS.indexOf(cid), L = c.track.length, cand = [];
  // where a tall wall stands close beside the line
  for (let i = 0; i < L; i += 3) {
    const t = c.track[i], u = c.track[(i + 2) % L], l = Math.hypot(u[0] - t[0], u[1] - t[1]) || 1, ux = (u[0] - t[0]) / l, uz = (u[1] - t[1]) / l;
    for (const side of [1, -1]) { // forward +z has +x on the left: left of (ux, uz) is (uz, -ux)
      const nx = uz * side, nz = -ux * side;
      let top = 0, at = 0;
      for (let r = 20; r < 170; r += 6) { const g = c.ground(t[0] + nx * r, t[1] + nz * r); if (g > top) top = g, at = r; }
      if (top > 22) cand.push({ i, side, score: top / Math.max(at, 50) });
    }
  }
  cand.sort((a, b) => b.score - a.score);
  const picks = [];
  for (const k of cand) { if (picks.length >= 4) break; const n = picks.filter((p) => p.side === k.side).length; if (n < 2 && picks.every((p) => Math.abs(p.i - k.i) > L / 10)) picks.push(k); }
  await cmd(`course ${level}`);
  let n = 0;
  for (const p of picks) {
    const s = p.i / L * c.len;
    await cmd('race'); await cmd(`go ${s - 40}`);
    await call('input', { op: 'set', ports: [{ south: true }] });
    await call('frame', { op: 'step', frames: 60 });
    const base = `${DIR}/${cid}-${n++}-${p.side > 0 ? 'left' : 'right'}`;
    for (let k = 0; k < 2; k++) await call('frame', { op: 'stepAndShot', frames: 10, path: `${base}-f${k}.png` });
    await call('input', { op: 'set', ports: [{}] });
    console.log(cid, n - 1, p.side > 0 ? 'left' : 'right', 's', s.toFixed(0));
  }
}
process.exit(0);
