// arenafps: a rocket blast's flames start upright and the jump pads' lift sparks rise from the pad's face. On the built
// cart through romdev (arenafps_auto: four seats play Foundry deathmatch by themselves), GL and WebGPU:
//   1. every blast's Fire particles start within 15 degrees of upright (angle 0, angle_random at most 0.08 of a turn),
//      while its Smoke keeps any angle (angle_random 1: the readback can tell the two apart);
//   2. every pad's PadLift particles come from a flat box (emission_shape BOX, emission_extents at most 0.05 m high);
//   3. frames of the match are kept every 25 frames from 600 to 1200 for a look from the play cameras (the jump pads
//      in the lower views).
// CONTROL: --control <cart> (a build from before the change) must fail checks 1 and 2.
//   node tests/arenafps-fx.mjs [--gl] [--webgpu] [--control out/arena-before/arenafps_auto.wasc]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = 'arenafps-fx', OUT = join(ROOT, 'out/arenafps-fx');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const CART = resolve(ROOT, arg('--cart', 'build/cart-wgpu/carts/arenafps_auto.wasc'));
const CONTROL = arg('--control', null);
mkdirSync(OUT, { recursive: true });

let requests = 0;
const post = (name, body) => new Promise((ok, no) => {
  requests++;
  const q = request(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': SESSION } }, (res) => {
    let t = '';
    res.setEncoding('utf8');
    res.on('data', (c) => (t += c));
    res.on('end', () => ok({ status: res.statusCode, text: t }));
  });
  q.on('error', no);
  q.end(body);
});
const bye = async (code) => {
  try { await post('host', '{"op":"shutdown"}'); } catch {}
  console.log(`romdev requests: ${requests}`);
  process.exit(code);
};
const tool = async (name, args) => {
  let r;
  try { r = await post(name, JSON.stringify(args)); }
  catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  const sm = /session: ([\w-]+)/.exec(r.text);
  if (sm && sm[1] !== SESSION) { console.error(`romdev ${name}: a reply for session ${sm[1]}, not ${SESSION}: discarded`); await bye(3); }
  let j; try { j = JSON.parse(r.text); } catch { j = r.text; }
  if (r.status !== 200 || j?.error || j?.isError) { console.error(`romdev ${name}: ${r.text.slice(0, 400)}`); await bye(3); }
  return j;
};
const cmd = async (text, maxFrames = 8) => {
  const j = await tool('wasm', { op: 'command', text, maxFrames });
  if (j.reply?.error) { console.error(`${text}: ${JSON.stringify(j.reply)}`); await bye(3); }
  return j.reply;
};
const get = async (uid, ...props) => {
  const r = await cmd(`get #${uid} ${props.join(' ')}`);
  return r.values ? Object.fromEntries(Object.entries(r.values).map(([k, v]) => [k, v.value])) : { [props[0]]: r.value };
};
const vec = (s) => String(s).replace(/^[a-z]+\d?\(/i, '').match(/-?[\d.]+(e-?\d+)?/g).map(Number);

// the property checks on one cart; returns the failures
async function check(cart, backend, tag, frames) {
  await tool('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  await tool('frame', { op: 'step', frames: 300 });
  const nodes = [];
  (function walk(n, path) {
    const p = `${path}/${n.name}`;
    if (/^(Fire|Smoke|PadLift\d*)$/.test(n.name)) nodes.push({ name: n.name, uid: n.uid, path: p });
    if (Array.isArray(n.children)) n.children.forEach((c) => walk(c, p));
  })(await cmd('tree / 12'), '');
  const out = [];
  const fire = nodes.filter((n) => n.name === 'Fire'), smoke = nodes.filter((n) => n.name === 'Smoke'), pads = nodes.filter((n) => /^PadLift/.test(n.name));
  if (!fire.length || !smoke.length || !pads.length) { console.error(`${tag}: nodes missing (${fire.length} Fire, ${smoke.length} Smoke, ${pads.length} PadLift)`); await bye(3); }
  for (const n of fire) {
    const v = await get(n.uid, 'angle', 'angle_random');
    if (!(+v.angle === 0 && +v.angle_random <= 0.08)) out.push(`${n.path}: angle ${v.angle}, angle_random ${v.angle_random}`);
  }
  const sm = await get(smoke[0].uid, 'angle_random');
  console.log(`info ${tag}: ${fire.length} blast Fire nodes, ${pads.length} pads; ${smoke[0].path} angle_random ${sm.angle_random} (any angle)`);
  for (const n of pads) {
    const v = await get(n.uid, 'emission_shape', 'emission_extents');
    const e = vec(v.emission_extents);
    if (!(v.emission_shape === 'BOX' && e[1] <= 0.05)) out.push(`${n.path}: ${v.emission_shape} extents ${v.emission_extents}`);
  }
  if (frames) {
    await tool('frame', { op: 'step', frames: 300 });
    for (let f = 600; f <= 1200; f += 25) {
      await tool('frame', { op: 'screenshot', path: join(OUT, `${tag}-f${f}.png`) });
      await tool('frame', { op: 'step', frames: 25 });
    }
  }
  return out;
}

let fails = 0;
const say = (ok, line) => { if (!ok) fails++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${line}`); };
for (const backend of BACKENDS) {
  const bad = await check(CART, backend, `${backend}`, true);
  say(!bad.length, `${backend}: every Fire starts upright and every PadLift rises from a flat box${bad.length ? ': ' + bad.slice(0, 4).join('; ') : ''}`);
  if (CONTROL) {
    const cbad = await check(resolve(ROOT, CONTROL), backend, `${backend}-control`, true);
    say(cbad.length > 0, `${backend}: control (${CONTROL}) fails the checks (${cbad.length} nodes: ${cbad.slice(0, 2).join('; ')})`);
  }
}
await bye(fails ? 1 : 0);
