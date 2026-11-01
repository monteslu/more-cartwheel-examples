// Cliffs from a held camera, the side_detile off and on, and how much each repeats (tools/cliff-measure.mjs). The off picture is the control: it
// must read as a repeat.
//   node games/jetski/tools/plaid-look.mjs <cart> <out dir> [--gl] [--released: a cart with no detile command]
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { readCourse, COURSE_IDS } from './course-read.mjs';
import { repeatShare } from './cliff-measure.mjs';
const URL_ = 'http://127.0.0.1:7331/mcp', CART = resolve(process.argv[2]), DIR = resolve(process.argv[3]), GL = process.argv.includes('--gl'), RELEASED = process.argv.includes('--released');
const SESSION = process.env.JETSKI_SESSION || 'jetski';
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
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-crests', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
// the tallest walls the course passes, from the racing line at the height of the race camera: three a course, well apart
const walls = (c, id = c.id) => {
  const cand = [];
  for (let i = 0; i < c.track.length; i += 4) for (const side of [-1, 1]) {
    const t = c.track[i], u = c.track[(i + 3) % c.track.length], l = Math.hypot(u[0] - t[0], u[1] - t[1]) || 1, nx = -(u[1] - t[1]) / l * side, nz = (u[0] - t[0]) / l * side;
    let d = 0;
    for (let r = 20; r < 260; r += 5) if (c.ground(t[0] + nx * r, t[1] + nz * r) > (id === "storm" ? 12 : 30)) { d = r; break; }
    if (!d) continue;
    const y = c.ground(t[0] + nx * (d + 40), t[1] + nz * (d + 40));
    cand.push({ x: t[0], z: t[1], ax: t[0] + nx * d, az: t[1] + nz * d, ay: Math.min(y, 60) * 0.55, score: y / Math.max(d, 60) });
  }
  cand.sort((p, q) => q.score - p.score);
  const out = [];
  for (const w of cand) if (out.length < 3 && out.every((o) => Math.hypot(o.x - w.x, o.z - w.z) > 300)) out.push(w);
  return out;
};
await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(GL ? { wgpu: false } : {}) });
const cmd = (t) => call('wasm', { op: 'command', text: t });
await call('frame', { op: 'step', frames: 20 });
for (const id of (process.env.PLAID_COURSES || COURSE_IDS.join(',')).split(',')) {
  const c = readCourse(id);
  await cmd('course ' + COURSE_IDS.indexOf(id)); await cmd('race');
  await call('frame', { op: 'step', frames: 340 }); // past the countdown and the GO, whose letters stand in the crop
  let k = 0;
  for (const w of walls(c)) {
    // from the line, at the height of the race camera: a tile shows from here, where it is a few dozen pixels across
    const cx = w.x, cz = w.z;
    await cmd(`peek ${cx} 5 ${cz} ${w.ax} ${w.ay} ${w.az}`);
    const row = [];
    for (const d of RELEASED ? [0] : [0, 1]) {
      if (!RELEASED) { await cmd('detile ' + d); await cmd('buoy_shot 0 1 0'); }
      const p = `${DIR}/${id}-${k}-${d}.png`;
      await call('frame', { op: 'stepAndShot', frames: 8, path: p });
      if (!process.env.PLAID_NO_MEASURE) row.push(repeatShare(p, 710, 300, 500, 240, 32).share.toFixed(3));
    }
    console.log(id, k++, 'at', w.x.toFixed(0), w.z.toFixed(0), 'repeat off', row[0], 'on', row[1] ?? '-');
  }
}
process.exit(0);
