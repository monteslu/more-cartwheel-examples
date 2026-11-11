// The jetski weather gates' client for romdev (ROMDEV_URL, else ROMDEV_PORT on 127.0.0.1, else 127.0.0.1:7331): the built cart only, each run one input({op:'timeline'})
// call (a whole schedule of held buttons, stepped in one request), debug commands and reads in between.
import { mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { ROMDEV_URL } from './romdev-url.mjs';
const ROMDEV = ROMDEV_URL; /* the pool sets it per job: each GPU of a host has its own romdev */
export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
export const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
export const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];

/** @param {string} name the gate's name: its romdev session and its out/ folder */
export function client(name) {
  const SESSION = process.env.JETSKI_SESSION || `${name}-${process.pid}`, H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': name };
  const OUT = join(ROOT, 'out', name);
  mkdirSync(OUT, { recursive: true });
  let requests = 0, frame = 0, fail = 0;
  const bye = async (code) => {
    try { await fetch(`${ROMDEV}/tool/host`, { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
    console.log(`${name}: ${requests} romdev requests`);
    process.exit(code);
  };
  for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
  const tool = async (t, args) => {
    requests++;
    let r;
    try { r = await fetch(`${ROMDEV}/tool/${t}`, { method: 'POST', headers: H, body: JSON.stringify(args) }); } catch (e) { console.error(`romdev ${t}: ${e.message}`); await bye(3); }
    const txt = await r.text();
    let j; try { j = JSON.parse(txt); } catch { j = txt; }
    if (!r.ok || j?.error) { console.error(`romdev ${t}: ${txt.slice(0, 500)}`); await bye(3); }
    return j;
  };
  const api = {
    OUT, tool, bye,
    get frame() { return frame; },
    /** a fresh cart on a backend: frame 0 */
    async load(cart, backend) {
      /* a busy shared server can drop a host it has just loaded: load again, a few times, before calling it a failure */
      let l, s;
      for (let k = 0; ; k++) {
        l = await tool('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
        if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
        const r = await fetch(`${ROMDEV}/tool/frame`, { method: 'POST', headers: H, body: JSON.stringify({ op: 'step', frames: 1 }) });
        requests++;
        s = await r.json().catch(() => ({ error: 'bad reply' }));
        if (r.ok && !s?.error) break;
        if (k >= 3) { console.error(`romdev frame after load: ${JSON.stringify(s).slice(0, 300)}`); await bye(3); }
        console.log(`  (the host was dropped right after its load: loading again, ${k + 1})`);
        await new Promise((res) => setTimeout(res, 3000));
      }
      frame = s.frameCount ?? s.frame ?? 1;
      if (typeof frame !== 'number') { console.error('no frame count: ' + JSON.stringify(s).slice(0, 300)); await bye(3); }
      /* the cart's assets stream in under a loading screen: the game's own debug fields come once it has started */
      for (let k = 0; k < 120; k++) {
        const v = await tool('wasm', { op: 'read', name: 'cw.loading' });
        if (Number(v.values ?? v.value) === 0) break;
        const st = await tool('frame', { op: 'step', frames: 30 });
        frame = st.frameCount ?? frame + 30;
      }
      return l;
    },
    async cmd(text) { const r = await tool('wasm', { op: 'command', text }); frame += r?.framesRun ?? 0; return r?.reply ?? r; },
    async rd(n) { const v = await tool('wasm', { op: 'read', name: n }); frame += v?.framesRun ?? 0; const x = v.values ?? v.value; return Array.isArray(x) ? x.map(Number) : Number(x); },
    /** one timeline: [[from, until, buttons...], ...] relative to now, then neutral to `frames` from now */
    async run(frames, holds = []) {
      /* a long schedule goes in pieces of at most 2400 frames, each one ranged call (one request stepping minutes of play
         outlasts an HTTP request's time limit) */
      let r, done = 0;
      while (done < frames) {
        const n = Math.min(2400, frames - done);
        const tl = holds.filter(([a, b]) => b > done && a < done + n).map(([a, b, ...bt]) => ({ frame: frame + Math.max(a, done) - done, until: frame + Math.min(b, done + n) - done, buttons: bt }));
        if (!tl.length) tl.push({ frame, until: frame + 1, buttons: ['select'] }); /* (a timeline needs an entry: select does nothing in a race) */
        r = await tool('input', { op: 'timeline', timeline: tl, untilFrame: frame + n });
        frame = r.frameCount ?? frame + n;
        done += n;
      }
      return r;
    },
    shot: (path) => tool('frame', { op: 'stepAndShot', frames: 1, path }).then((r) => { frame++; return r; }),
    check(label, ok, detail) { console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}: ${detail}`); if (!ok) fail++; return ok; },
    get fails() { return fail; },
  };
  return api;
}
