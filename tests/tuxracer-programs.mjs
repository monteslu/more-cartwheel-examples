// tuxracer draws everything it means to on the cart through romdev, GL and WebGPU: a program for every draw state over a
// whole run, not only at the end of one. Each backend loads the cart once and plays: the title, a race at every preset
// (Bunny Hill sunny, Bunny Hill by night, Path of Daggers by night), then the preset switched mid-race (Very
// Low to High and back, High to Very Low and back, through every preset). The run fails on any draw state the engine
// reports with no program (the cart's log, drained after every stretch), on any skipped draw (render.skipped, which
// counts the whole run), and on the red "no program" banner in the frames kept at each stretch's end.
// Control: the release cart of v0.29.1 (--cart w3/rel.wasc or any cart) skipped Tux and the ghost after a switch.
//   node tests/tuxracer-programs.mjs [--gl] [--webgpu] [--cart <path>] [--expect-fail]
import { mkdirSync, readFileSync } from 'node:fs';
import { resolve, dirname, join, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';
import { inflateSync } from 'node:zlib';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/tuxracer-programs');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/tuxracer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const EXPECT_FAIL = process.argv.includes('--expect-fail'); // the control: this cart must be caught
const SESSION = process.env.TUXRACER_SESSION || 'tuxracer-programs', AGENT = process.env.TUXRACER_AGENT || 'tuxracer-programs';
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT };
const bye = async (code) => { try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {} process.exit(code); };
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
const post = (name, body) => new Promise((ok, no) => {
  const q = request(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: H }, (res) => { let t = ''; res.setEncoding('utf8'); res.on('data', (c) => (t += c)); res.on('end', () => ok({ ok: res.statusCode === 200, t })); });
  q.on('error', no); q.end(body);
});
const tool = async (name, args) => {
  let r; try { r = await post(name, JSON.stringify(args)); } catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  let j; try { j = JSON.parse(r.t); } catch { j = r.t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${r.t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (t) => tool('wasm', { op: 'command', text: t });
const step = (n) => tool('frame', { op: 'step', frames: n });
const STATE = /\b(?:standard|physical|basic|lambert|phong|toon|depth|distance|sprite|points|line|shader)(?:\+[a-z0-9_]+)+/g;

/** an 8-bit RGB(A) PNG's top band: the share of its pixels in the banner's red (romdev writes these) */
function bannerShare(path) {
  const b = readFileSync(path);
  let w = 0, h = 0, ct = 0; const idat = [];
  for (let o = 8; o < b.length;) { const len = b.readUInt32BE(o), type = b.toString('latin1', o + 4, o + 8), d = b.subarray(o + 8, o + 8 + len); if (type === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9]; } else if (type === 'IDAT') idat.push(d); o += 12 + len; }
  const ch = ct === 6 ? 4 : 3, raw = inflateSync(Buffer.concat(idat)), stride = w * ch, rows = Math.min(h, Math.round(h * 0.03)), px = Buffer.alloc(stride * rows);
  for (let y = 0; y < rows; y++) {
    const f = raw[y * (stride + 1)], src = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)), row = y * stride;
    for (let x = 0; x < stride; x++) { const a = x >= ch ? px[row + x - ch] : 0, up = y ? px[row - stride + x] : 0, c = y && x >= ch ? px[row - stride + x - ch] : 0; let v = src[x];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1; else if (f === 4) { const p = a + up - c, pa = Math.abs(p - a), pb = Math.abs(p - up), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      px[row + x] = v & 255; }
  }
  let red = 0;
  for (let i = 0; i < w * rows; i++) { const r = px[i * ch], g = px[i * ch + 1], bl = px[i * ch + 2]; if (r > 120 && r > g * 2.2 && r > bl * 2.2) red++; }
  return red / (w * rows);
}

let fail = 0;
for (const backend of BACKENDS) {
  console.log(`--- ${backend} ${CART}`);
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: process.env.CW_GPU_POWER || 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('wasm', { op: 'events' });                                       /* start from an empty log */
  const missing = new Map(), problems = [];
  let n = 0;
  /** a stretch of play, then what the log said, the skipped draws so far and the banner in its last frame */
  const stretch = async (label, frames) => {
    await step(frames);
    const ev = await tool('wasm', { op: 'events' });
    for (const e of ev.log || []) { const t = typeof e === 'string' ? e : e.text || e.msg || JSON.stringify(e); for (const m of t.match(STATE) || []) missing.set(m, missing.get(m) || label); } /* the engine logs a state by name only when it has no program */
    const path = join(OUT, `${basename(CART, ".wasc")}-${backend}-${String(++n).padStart(2, '0')}-${label.replace(/[^a-z0-9]+/gi, '-')}.png`);
    await tool('frame', { op: 'stepAndShot', frames: 1, path });
    const skipped = +(await rd('render.skipped')) || 0, banner = bannerShare(path);
    if (skipped || banner > 0.3) problems.push(`${label}: skipped ${skipped}, banner ${(banner * 100).toFixed(0)}%`);
    console.log(`     ${label}: skipped ${skipped}, banner ${(banner * 100).toFixed(0)}%`);
  };
  await stretch('title', 120);
  for (const q of ['verylow', 'low', 'medium', 'high', 'ultra']) {
    await cmd(`quality ${q}`);
    for (const [c, cond] of [['bunny_hill', 'sunny'], ['bunny_hill', 'night'], ['path_of_daggers', 'night']]) { await cmd(`race ${c} ${cond}`); await stretch(`${q} ${c} ${cond}`, 600); }
  }
  /* switches mid-race, both ways, and through every preset */
  for (const [from, to] of [['verylow', 'high'], ['high', 'verylow']]) {
    await cmd(`quality ${from}`); await cmd('race bunny_hill night'); await stretch(`${from} race`, 400);
    await cmd(`quality ${to}`); await stretch(`switch to ${to}`, 240);
    await cmd(`quality ${from}`); await stretch(`back to ${from}`, 240);
  }
  for (const q of ['low', 'medium', 'ultra', 'verylow', 'high']) { await cmd(`quality ${q}`); await stretch(`switch to ${q}`, 180); }
  const caught = missing.size > 0 || problems.length > 0;
  for (const [s, at] of missing) console.log(`     no program: ${s} (first at ${at})`);
  for (const p of problems) console.log(`     ${p}`);
  const ok = EXPECT_FAIL ? caught : !caught;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${backend}: ${EXPECT_FAIL ? 'control, this cart must be caught: ' : ''}${missing.size} state(s) with no program, ${problems.length} stretch(es) with skipped draws or the banner, ${n} frames looked at`);
  if (!ok) fail++;
  await tool('host', { op: 'shutdown' });
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
