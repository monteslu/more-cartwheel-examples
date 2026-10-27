// romdev-menu.mjs <cart.wasc> <tag> [gl|wgpu]: the shell's title, join and the hub menu (a person on pad 1), then a tournament board.
const [cart, tag, host = 'gl'] = process.argv.slice(2);
const ROOT = new URL('..', import.meta.url).pathname;
const call = async (tool, body) => (await fetch(`http://127.0.0.1:7331/tool/${tool}`, { method: 'POST', headers: { 'content-type': 'application/json', 'x-romdev-session': 'partypack-live' }, body: JSON.stringify(body) })).json();
await call('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(host === 'gl' ? { wgpu: false } : {}) }); // gpuPower always: without it romdev takes the display GPU
const shot = async (n, frames = 90) => { await call('frame', { op: 'step', frames }); const r = await call('frame', { op: 'screenshot', path: `${ROOT}out/v/${tag}-${n}.png` }); console.log(n, r.error ?? 'ok'); };
const press = async (b, frames = 4) => { const r = await call('input', { op: 'press', button: b, frames }); if (r.error) console.log('press', b, r.error.slice(0, 100)); };
await shot('title', 120);
await press('south', 6); await shot('play-select', 60);
await press('south', 6); await shot('join', 90);
await press('start', 6); await shot('menu1', 150);
await press('down'); await shot('menu2', 60);
await press('up'); await press('south'); await shot('board', 200);
await press('south'); await shot('intro', 120);
await press('south'); await shot('count', 60);
await shot('play', 400);
console.log('done');
