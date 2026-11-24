// The games' own items in the shell's pause menu (cartwheel/shell.h pause_items), on the cart through romdev, GL and
// WebGPU, by the pad alone: Start pauses, the d-pad moves down to the item, the south button takes it.
//   hordeshooter: "Restart run" (the row under Resume) puts the run back to its start: the run's kills go back to 0 (at
//     most 2 by the time the menu has closed, rounds still in flight) and play goes on (game.restarts counts it). Control, which must fail: sw no_restart 1 (the item does
//     nothing), the run keeps its kills.
//   partypack: in Free Play, "Play it again" (the row under Resume) opens the same mini-game's card again; "Pick another
//     game" (the row under it) goes back to the list. In a tournament, "Leave the tournament" goes back to the menu.
//     Control, which must fail: sw no_pause_items 1 (the shell's own three rows: the row under Resume is Settings),
//     play stays where it was.
//   node tests/pause-items.mjs <hordeshooter|partypack> [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const GAME = process.argv[2];
if (!['hordeshooter', 'partypack'].includes(GAME)) { console.error('usage: node tests/pause-items.mjs <hordeshooter|partypack> [--gl] [--webgpu] [--cart <path>]'); process.exit(2); }
const OUT = join(ROOT, `out/pause-items-${GAME}`);
const SESSION = `pause-items-${GAME}-${process.pid}`;
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, `build/cart-wgpu/carts/${GAME}.wasc`)));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const H = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': 'pause-items' };
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(`${ROMDEV_URL}/tool/host`, { method: 'POST', headers: H, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => bye(130));
for (const ev of ['unhandledRejection', 'uncaughtException']) process.on(ev, (e) => { console.error(e); bye(3); });
const tool = async (name, args) => {
  let r;
  try { r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: H, body: JSON.stringify(args) }); }
  catch (e) { console.error(`romdev ${name}: ${e.cause?.message || e.message} (the server is down)`); await bye(3); }
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return +(v.values ?? v.value); };
const step = (frames) => tool('frame', { op: 'step', frames });
/* ranges, not loops: a press is one request (held 4 frames, let go for 8); a stretch of play is one request */
const press = (b) => tool('input', { op: 'sequence', steps: [{ input: { ports: [{ [b]: true }, {}] }, frames: 4 }, { input: { ports: [{}, {}] }, frames: 8 }] });
const hold = (frames, p0) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, {}] }, frames }, { input: { ports: [{}, {}] }, frames: 1 }] });
const shot = (name) => tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, name) });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const SHELL_PLAY = 3;

async function load(backend, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press('south'); await step(20); /* the title: Play */
  await press('south');                 /* the join screen: P1 takes a seat */
  await press('start'); await step(40); /* play */
}
/* Start, then `downs` presses of down, then south: the pause menu's row that many under Resume */
async function pauseRow(tag, downs) {
  await press('start'); await step(20);
  for (let i = 0; i < downs; i++) await press('down');
  await shot(`${tag}-menu.png`);
  await press('south'); await step(30);
}

async function horde(backend, tag, pre) {
  await load(backend, pre);
  await hold(60 * 20, { south: true, axes: { rx: 1, ry: 0.3 } }); /* shooting about for 20 s */
  await hold(60 * 10, { south: true, axes: { rx: -1, ry: -0.3 } });
  const kills = await rd('game.kills');
  await pauseRow(`${backend}-${tag}`, 1);
  const r = { kills, after: await rd('game.kills'), restarts: await rd('game.restarts'), screen: await rd('shell.screen') };
  await hold(120, { up: true, south: true });
  r.playing = await rd('game.ticks');
  await shot(`${backend}-${tag}-after.png`);
  return r;
}

const S_PICK = 1, S_INTRO = 4, S_PLAY = 6, S_MENU = 0;
async function party(backend, tag, pre, row) {
  await load(backend, pre);
  await press('down'); await press('south'); await step(30); /* the hub: Free Play, the list */
  const pick = await rd('game.state');
  await press('south'); await step(40); /* the first game in the list: its card */
  await press('south');                 /* ready */
  let f = 0;
  for (; f < 60 * 12 && (await rd('game.state')) !== S_PLAY; f += 30) await step(30);
  await hold(120, { right: true, south: true });
  const before = await rd('game.state'), cur = await rd('game.mg');
  await pauseRow(`${backend}-${tag}`, row);
  return { pick, before, cur, state: await rd('game.state'), after: await rd('game.mg'), picks: await rd('game.pause_picks'), screen: await rd('shell.screen') };
}

async function tournament(backend, tag, pre) {
  await load(backend, pre);
  await press('south');                 /* the hub: the highlighted Party Tournament */
  for (let f = 0; f < 120 && (await rd('game.state')) !== 3; f += 30) await step(30);
  await step(160); await press('south'); /* the board's next, once the tokens have moved */
  for (let f = 0; f < 240 && (await rd('game.state')) !== S_INTRO; f += 30) await step(30);
  await step(40); await press('south');  /* the card's ready */
  for (let f = 0; f < 60 * 12 && (await rd('game.state')) !== S_PLAY; f += 30) await step(30);
  await hold(120, { right: true, south: true });
  const before = await rd('game.state');
  await pauseRow(`${backend}-${tag}`, 1);
  return { before, state: await rd('game.state'), picks: await rd('game.pause_picks'), screen: await rd('shell.screen') };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (GAME === 'hordeshooter') {
    const a = await horde(backend, 'restart');
    check('Restart run: the run starts over and plays on', a.kills >= 10 && a.after <= 2 && a.restarts === 1 && a.screen === SHELL_PLAY,
      `kills ${a.kills} before, ${a.after} after, game.restarts ${a.restarts}, shell.screen ${a.screen}`);
    const c = await horde(backend, 'restart-control', 'sw no_restart 1');
    check('control: sw no_restart 1, the run keeps its kills', !(c.kills >= 10 && c.after <= 2 && c.restarts === 1),
      `kills ${c.kills} before, ${c.after} after, game.restarts ${c.restarts}`);
  } else {
    const a = await party(backend, 'again', null, 1);
    check('Free Play, Play it again: the same game\'s card opens again', a.pick === S_PICK && a.before === S_PLAY && a.state === S_INTRO && a.after === a.cur && a.picks === 1 && a.screen === SHELL_PLAY,
      `list ${a.pick}, playing ${a.before}, after: state ${a.state}, game ${a.after} (was ${a.cur}), picks ${a.picks}, shell.screen ${a.screen}`);
    const b = await party(backend, 'another', null, 2);
    check('Free Play, Pick another game: back to the list', b.before === S_PLAY && b.state === S_PICK && b.picks === 1 && b.screen === SHELL_PLAY,
      `playing ${b.before}, after: state ${b.state}, picks ${b.picks}, shell.screen ${b.screen}`);
    const t = await tournament(backend, 'leave');
    check('Tournament, Leave the tournament: back to the menu', t.before === S_PLAY && t.state === S_MENU && t.picks === 1 && t.screen === SHELL_PLAY,
      `playing ${t.before}, after: state ${t.state}, picks ${t.picks}, shell.screen ${t.screen}`);
    const c = await party(backend, 'control', 'sw no_pause_items 1', 1);
    check('control: sw no_pause_items 1, the row under Resume is not the game\'s', !(c.state === S_INTRO && c.picks === 1),
      `after: state ${c.state}, picks ${c.picks}, shell.screen ${c.screen}`);
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
