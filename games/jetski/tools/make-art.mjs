// The launcher art in games/jetski/art (cover.jpg, shot-1.jpg .. shot-5.jpg), captured from the built cart through romdev
// and composited here, so it can be made again whenever the game's look changes.
//
//   scripts/build.sh jetski_auto            (TARGETS=cart-wgpu is enough)
//   node games/jetski/tools/make-art.mjs [--only=cover,shot-1,...] [--live=<seconds>] [--explore] [--out=<dir>]
//
// The computer rider plays every race (the jetski_auto cart). The screenshots are the race as a player sees it, HUD
// and all, 21.40 s into lap 1 of a fresh race on each water (1920 x 1080). The cover is the atoll with the HUD off
// (JETSKI_NO_HUD) and the sea held big (swell): the pack warped close together by the spire (warp, its gap and side),
// the world held still and the course's buoys hidden (buoy_shot), and a held camera in front of the leader (peek). The
// cart renders at twice the cover's height (6400 x 3600) and the middle 1200 x 1800 is the cover, a longer lens with
// nothing scaled; art/logo.png is laid over the top. Captures land in build/art (not kept); --out writes the JPEGs
// elsewhere to compare. --live plays the first race on the romdev livestream in real time for that many seconds before
// its capture. --explore [--warps=m,m] [--cover=<json over COVER>] writes small cover candidates to build/art/explore.
// One romdev session (JETSKI_SESSION, default jetski-art); the server is ROMDEV_URL, else 127.0.0.1:7331.
import { mkdirSync, writeFileSync, existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { ROMDEV_URL } from '../../../tests/lib/romdev-url.mjs';
import { readImage, writePng } from '../../../deps/cartwheel-engine/tools/lib/png.mjs';
import { encodeJpeg } from './jpeg-encode.mjs';

const HERE = dirname(fileURLToPath(import.meta.url)), ROOT = resolve(HERE, '../../..');
const ART = resolve(HERE, '../art');
const arg = (k) => process.argv.find((a) => a.startsWith(`--${k}=`))?.slice(k.length + 3);
const OUT = resolve(arg('out') || ART), WORK = join(ROOT, 'build/art'), LIVE = +(arg('live') || 0);
const ONLY = arg('only')?.split(','), EXPLORE = process.argv.includes('--explore');
const CART = join(ROOT, 'build/cart-wgpu/carts/jetski_auto.wasc');
const QUALITY = 92;
if (!existsSync(CART)) throw new Error(`no ${CART}: build it first (TARGETS=cart-wgpu scripts/build.sh jetski_auto)`);
mkdirSync(WORK, { recursive: true });
mkdirSync(OUT, { recursive: true });

// the screenshots: COURSES order in src/courses/all.c (0 atoll, 1 sound, 2 canyon, 3 harbour, 4 storm); labels in art/art.json
// place: the place the HUD must show for the label; the computer riders' races differ a little run to run, so a race
// that does not show it at 0:21.40 is run again, up to TRIES times
const TRIES = 8;
const SHOTS = [
  { name: 'shot-1', course: 0, place: 1 }, // leading the race
  { name: 'shot-2', course: 2 }, // riders side by side, whoever leads
  { name: 'shot-3', course: 3, place: 1 },
  { name: 'shot-4', course: 4, place: 2 }, // a rival ahead
  { name: 'shot-5', course: 1 },
];
const SHOT_CLOCK = 21.39; // the first frame whose race clock reads 0:21.40
// the cover: a race on the atoll under way (clock), the pack warped to warp metres along the course, gap metres apart and
// side metres either side of the line, run settle frames; the held camera in the leader's frame (metres ahead along its
// travel, to its right, up from it) looking at a point lookBehind behind it (lookRight, lookUp); zoom: the lens, as the
// share of the frame kept
const COVER = { course: 0, clock: 6, swell: 1, warp: 1081, gap: 3.5, side: 1.6, settle: 20, zoom: 2, ahead: 11, right: 2.7, up: 3.3, lookBehind: 3.5, lookRight: 1.4, lookUp: 1.0, ...JSON.parse(arg('cover') || '{}') };
// the logo's place on the 1200 x 1800 cover (as the first cover had it)
const LOGO = { x: 4, y: 13, w: 1176, h: 301 };
const COVER_W = 1200, COVER_H = 1800;

// ── romdev ──
const URL_ = ROMDEV_URL + '/mcp', SESSION = process.env.JETSKI_SESSION || 'jetski-art';
let sid = null, rpcId = 1;
async function rpc(method, params) {
  const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION };
  if (sid) headers['mcp-session-id'] = sid;
  const r = await fetch(URL_, { method: 'POST', headers, body: JSON.stringify({ jsonrpc: '2.0', id: rpcId++, method, params }) });
  if (!sid) sid = r.headers.get('mcp-session-id');
  const t = await r.text();
  const line = t.split('\n').find((l) => l.startsWith('data:'));
  return JSON.parse(line ? line.slice(5) : t);
}
async function call(name, args) {
  const r = await rpc('tools/call', { name, arguments: args });
  if (r.error || r.result?.isError) throw new Error(`${name} ${JSON.stringify(args).slice(0, 200)}: ${JSON.stringify(r.error || r.result).slice(0, 400)}`);
  const tx = r.result.content?.[0]?.text;
  try { return JSON.parse(tx); } catch { return tx; }
}
const command = async (text) => {
  const r = await call('wasm', { op: 'command', text });
  if (r?.reply?.error) throw new Error(`'${text}': ${r.reply.error}`);
  return r.reply;
};
const shot = (path, extra = {}) => call('frame', { op: 'screenshot', path, ...extra });
async function load(size, env) {
  const r = await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', preferredSize: size, ...(env ? { firstCommand: `env ${env}` } : {}) });
  if (r.fbWidth !== size.width || r.fbHeight !== size.height) throw new Error(`the cart came up ${r.fbWidth}x${r.fbHeight}, not ${size.width}x${size.height}`);
  await call('frame', { op: 'step', frames: 120 });
}
/** a fresh race on that course, run to that race clock (a live stretch first on --live) */
async function raceTo(course, clock, live, fresh = true) {
  if (fresh) {
    await command(`course ${course}`);
    await command('race');
  }
  if (live > 0) {
    await call('host', { op: 'play', seconds: live });
    await new Promise((ok) => setTimeout(ok, live * 1000 + 500));
    await call('host', { op: 'stop' }).catch(() => {});
  }
  const r = await call('frame', { op: 'step', frames: 6000, stopWhen: { field: 'race.clock', atLeast: clock }, fields: ['race.clock', 'race.state', 'p0.pos', 'p0.vel', 'p0.place', 'p0.shown_place'] });
  if (!r.stopped) throw new Error(`course ${course}: the race clock never reached ${clock}`);
  return r.fields;
}

// ── pictures ──
const lanczos = (x) => (x === 0 ? 1 : Math.abs(x) >= 3 ? 0 : (3 * Math.sin(Math.PI * x) * Math.sin(Math.PI * x / 3)) / (Math.PI * Math.PI * x * x));
/** one axis of a Lanczos-3 resample (widened by the scale when shrinking), on premultiplied float RGBA */
function resampleAxis(src, sw, sh, dw, horizontal) {
  const sn = horizontal ? sw : sh, dn = dw, scale = sn / dn, sup = 3 * Math.max(1, scale), fs = Math.max(1, scale);
  const ow = horizontal ? dn : sw, oh = horizontal ? sh : dn, out = new Float32Array(ow * oh * 4);
  for (let d = 0; d < dn; d++) {
    const c = (d + 0.5) * scale - 0.5, lo = Math.max(0, Math.ceil(c - sup)), hi = Math.min(sn - 1, Math.floor(c + sup));
    const wts = [];
    let sum = 0;
    for (let s = lo; s <= hi; s++) { const wv = lanczos((s - c) / fs); wts.push(wv); sum += wv; }
    const lines = horizontal ? sh : sw;
    for (let l = 0; l < lines; l++) {
      let r = 0, g = 0, b = 0, a = 0;
      for (let s = lo; s <= hi; s++) {
        const wv = wts[s - lo] / sum, i = 4 * (horizontal ? l * sw + s : s * sw + l);
        r += src[i] * wv; g += src[i + 1] * wv; b += src[i + 2] * wv; a += src[i + 3] * wv;
      }
      const o = 4 * (horizontal ? l * ow + d : d * ow + l);
      out[o] = r; out[o + 1] = g; out[o + 2] = b; out[o + 3] = a;
    }
  }
  return out;
}
/** an RGBA picture scaled to w x h (premultiplied while filtering, so clear edges do not darken) */
function resize(img, w, h) {
  const n = img.w * img.h, f = new Float32Array(n * 4);
  for (let i = 0; i < n; i++) {
    const a = img.rgba[4 * i + 3] / 255;
    f[4 * i] = img.rgba[4 * i] * a; f[4 * i + 1] = img.rgba[4 * i + 1] * a; f[4 * i + 2] = img.rgba[4 * i + 2] * a; f[4 * i + 3] = a;
  }
  const x = resampleAxis(f, img.w, img.h, w, true), y = resampleAxis(x, w, img.h, h, false), rgba = new Uint8Array(w * h * 4);
  const clamp = (v) => (v < 0 ? 0 : v > 255 ? 255 : Math.round(v));
  for (let i = 0; i < w * h; i++) {
    const a = Math.min(1, Math.max(0, y[4 * i + 3]));
    for (let k = 0; k < 3; k++) rgba[4 * i + k] = clamp(a > 1e-6 ? y[4 * i + k] / a : 0);
    rgba[4 * i + 3] = clamp(a * 255);
  }
  return { w, h, rgba };
}
function crop(img, x0, y0, w, h) {
  const rgba = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) rgba.set(img.rgba.subarray(4 * ((y0 + y) * img.w + x0), 4 * ((y0 + y) * img.w + x0 + w)), 4 * y * w);
  return { w, h, rgba };
}
/** src laid over dst at (x, y), source-over */
function over(dst, src, x0, y0) {
  for (let y = 0; y < src.h; y++) for (let x = 0; x < src.w; x++) {
    const dx = x + x0, dy = y + y0;
    if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
    const s = 4 * (y * src.w + x), d = 4 * (dy * dst.w + dx), a = src.rgba[s + 3] / 255;
    for (let k = 0; k < 3; k++) dst.rgba[d + k] = Math.round(src.rgba[s + k] * a + dst.rgba[d + k] * (1 - a));
  }
  return dst;
}
const saveJpeg = (path, img) => { writeFileSync(path, encodeJpeg(img.w, img.h, img.rgba, QUALITY)); console.log(`${path}  ${img.w}x${img.h}`); };

// ── the run ──
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'jetski-art', version: '1' } });
await fetch(URL_, { method: 'POST', headers: { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', 'x-romdev-session': SESSION, 'mcp-session-id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) });
const want = (name) => !ONLY || ONLY.includes(name);
let live = LIVE;

if (!EXPLORE && SHOTS.some((s) => want(s.name))) {
  await load({ width: 1920, height: 1080 });
  for (const s of SHOTS.filter((v) => want(v.name))) {
    let f;
    for (let t = 1; ; t++) {
      f = await raceTo(s.course, +(arg('clock') || SHOT_CLOCK), live);
      live = 0;
      if (!s.place || f['p0.shown_place'] === s.place) break;
      console.log(`${s.name}: race ${t} showed place ${f['p0.shown_place']}, not ${s.place}`);
      if (t === TRIES) throw new Error(`${s.name}: no race in ${TRIES} showed place ${s.place} at ${SHOT_CLOCK} s`);
    }
    const png = join(WORK, `${s.name}.png`);
    await shot(png);
    console.log(`${s.name}: course ${s.course}, clock ${f['race.clock'].toFixed(3)}, place ${f['p0.shown_place']}`);
    const img = readImage(png);
    if (img.w !== 1920 || img.h !== 1080) throw new Error(`${png} is ${img.w}x${img.h}`);
    saveJpeg(join(OUT, `${s.name}.jpg`), img);
  }
}

/** the held camera for the cover from the leader's place and travel */
function coverCamera(pos, vel, c) {
  const l = Math.hypot(vel[0], vel[2]) || 1, fx = vel[0] / l, fz = vel[2] / l, rx = fz, rz = -fx; // forward, and to the rider's right
  const cam = [pos[0] + fx * c.ahead + rx * c.right, pos[1] + c.up, pos[2] + fz * c.ahead + rz * c.right];
  const at = [pos[0] - fx * c.lookBehind + rx * (c.lookRight || 0), pos[1] + c.lookUp, pos[2] - fz * c.lookBehind + rz * (c.lookRight || 0)];
  return `peek ${cam.map((v) => v.toFixed(3)).join(' ')} ${at.map((v) => v.toFixed(3)).join(' ')}`;
}
/** the pack placed at c.warp metres along the course (the racers 11 m apart, side to side), run c.settle frames, held still,
 *  and the camera put in front of the leader; the crop is the middle 2:3 of the frame */
async function coverFrame(c, png, w, h) {
  await command(`warp ${c.warp} ${c.gap ?? 11} ${c.side ?? 3}`);
  const f = await call('frame', { op: 'step', frames: c.settle, fields: ['race.clock', 'p0.pos', 'p0.vel', 'p0.dist', 'p1.dist', 'p2.dist', 'p0.air_time'] });
  await command('buoy_shot 1 0 1'); // the world held still, the course's buoys out of the picture
  await command(coverCamera(f.fields['p0.pos'], f.fields['p0.vel'], c));
  await call('frame', { op: 'step', frames: 3 });
  const ch = Math.round(h / c.zoom), cw = Math.round(ch * 2 / 3); // a longer lens: the middle of a bigger frame
  await shot(png, { crop: { x: Math.round((w - cw) / 2), y: Math.round((h - ch) / 2), w: cw, h: ch } });
  await command('peek');
  await command('buoy_shot 1 1 0');
  return f.fields;
}
/** a race on the cover's course, under way, the sea held at the cover's size */
async function coverRace(c, live) {
  if (c.swell) await command(`swell ${c.swell}`);
  await raceTo(c.course, c.clock, live);
}

if (EXPLORE) {
  // candidates at 1080p (the same framing as the 4K cover): one race, the pack warped to each place in turn
  const dir = join(WORK, 'explore');
  mkdirSync(dir, { recursive: true });
  await load({ width: 1920 * COVER.zoom, height: 1080 * COVER.zoom }, 'JETSKI_NO_HUD=1');
  await coverRace(COVER, 0);
  // --variants=<json array of overrides over COVER>: each in turn at each warp
  const variants = JSON.parse(arg('variants') || '[{}]');
  for (const warp of (arg('warps') || String(COVER.warp)).split(',').map(Number)) for (const [vi, v] of variants.entries()) {
    const png = join(dir, `cover-${warp}-${vi}.png`);
    const f = await coverFrame({ ...COVER, warp, ...v }, png, 1920 * COVER.zoom, 1080 * COVER.zoom);
    const small = resize(readImage(png), 360, 540);
    writePng(png, small.w, small.h, small.rgba);
    console.log(`${png}: p0 ${f['p0.pos'].map((v) => v.toFixed(1))} dist ${f['p0.dist'].toFixed(1)} ${f['p1.dist'].toFixed(1)} ${f['p2.dist'].toFixed(1)} air ${f['p0.air_time']}`);
  }
  process.exit(0);
}

if (want('cover')) {
  const size = { width: Math.round(COVER_H * 16 / 9) * COVER.zoom, height: COVER_H * COVER.zoom }; // the crop is the cover's size: nothing scaled
  await load(size, 'JETSKI_NO_HUD=1');
  await coverRace(COVER, live);
  const png = join(WORK, 'cover-raw.png');
  const f = await coverFrame(COVER, png, size.width, size.height);
  console.log(`cover: course ${COVER.course}, the pack at ${COVER.warp} m, p0 at ${f['p0.dist'].toFixed(1)} m`);
  const raw = readImage(png);
  if (raw.w !== COVER_W || raw.h !== COVER_H) throw new Error(`the cover crop is ${raw.w}x${raw.h}, not ${COVER_W}x${COVER_H}`);
  const img = { w: raw.w, h: raw.h, rgba: raw.rgba };
  over(img, resize(readImage(join(ART, 'logo.png')), LOGO.w, LOGO.h), LOGO.x, LOGO.y);
  saveJpeg(join(OUT, 'cover.jpg'), img);
}
process.exit(0);
