// Every course in every weather from the race camera, on the built cart through romdev: a frame of the race at speed
// (the computer steering the seat) in a calm, a chop and a storm, saved whole and as a native-scale crop of its middle
// (no downscale, so the water's detail can be judged). Writes out/gate/weather-look/<course>-<weather>[-crop].png.
//   node games/jetski/tools/weather-look.mjs [--gl] [--courses 0,1,2,3,4] [--at 900]
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { inflateSync, deflateSync, crc32 } from 'node:zlib';
import { join } from 'node:path';
import { client, ROOT, arg } from '../../../tests/lib/jetski-romdev.mjs';

const R = client('jetski-weather-look'), OUT = join(ROOT, 'out/gate/weather-look');
mkdirSync(OUT, { recursive: true });
const CART = join(ROOT, 'build/cart-wgpu/carts/jetski.wasc'), backend = process.argv.includes('--gl') ? 'gl' : 'webgpu';
const COURSES = arg('--courses', '0,1,2,3,4').split(',').map(Number), AT = arg('--at', '900');

function readPng(path) {
  const b = readFileSync(path), w = b.readUInt32BE(16), h = b.readUInt32BE(20), ch = b[25] === 6 ? 4 : 3, parts = [];
  for (let o = 8; o < b.length;) { const n = b.readUInt32BE(o); if (b.toString('latin1', o + 4, o + 8) === 'IDAT') parts.push(b.subarray(o + 8, o + 8 + n)); o += 12 + n; }
  const raw = inflateSync(Buffer.concat(parts)), out = Buffer.alloc(w * h * ch), s = w * ch;
  for (let y = 0; y < h; y++) {
    const f = raw[y * (s + 1)], r = y * (s + 1) + 1, d = y * s;
    for (let x = 0; x < s; x++) {
      const a = x >= ch ? out[d + x - ch] : 0, u = y ? out[d + x - s] : 0, c = y && x >= ch ? out[d + x - s - ch] : 0;
      let p = raw[r + x];
      if (f === 1) p += a; else if (f === 2) p += u; else if (f === 3) p += (a + u) >> 1;
      else if (f === 4) { const q = a + u - c, pa = Math.abs(q - a), pb = Math.abs(q - u), pc = Math.abs(q - c); p += pa <= pb && pa <= pc ? a : pb <= pc ? u : c; }
      out[d + x] = p & 255;
    }
  }
  return { w, h, ch, px: out };
}
function writePng(path, w, h, ch, px) {
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const n = Buffer.concat([Buffer.from(t), d]), e = Buffer.alloc(4); e.writeUInt32BE(crc32(n)); return Buffer.concat([l, n, e]); };
  const raw = Buffer.alloc((w * ch + 1) * h);
  for (let y = 0; y < h; y++) px.copy(raw, y * (w * ch + 1) + 1, y * w * ch, (y + 1) * w * ch);
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = ch === 4 ? 6 : 2;
  writeFileSync(path, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

for (const c of COURSES) for (const w of [0, 1, 2]) {
  await R.load(CART, backend);
  await R.run(30);
  for (const k of [`course ${c}`, `weather ${w}`, 'race', 'sw autosteer 1', 'no_dq 1', 'wxsw event 0']) await R.cmd(k);
  await R.run(240, [[0, 240, 'south']]);
  await R.cmd(`go ${AT}`);
  await R.run(200, [[0, 200, 'south']]);
  const path = join(OUT, `${c}-${w}.png`);
  await R.shot(path);
  const im = readPng(path), cw = Math.floor(im.w / 2), chh = Math.floor(im.h / 2), x0 = Math.floor(im.w / 4), y0 = Math.floor(im.h * 0.42);
  const crop = Buffer.alloc(cw * chh * im.ch);
  for (let y = 0; y < chh; y++) im.px.copy(crop, y * cw * im.ch, ((y0 + y) * im.w + x0) * im.ch, ((y0 + y) * im.w + x0 + cw) * im.ch);
  writePng(join(OUT, `${c}-${w}-crop.png`), cw, chh, im.ch, crop);
  console.log(`course ${c} weather ${w}: ${path}`);
}
await R.bye(0);
