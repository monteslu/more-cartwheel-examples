// A native-scale crop of a PNG, to look at without the viewer downsampling: crop.mjs in.png out.png x y w h
import { writeFileSync } from 'node:fs';
import { deflateSync, crc32 } from 'node:zlib';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const { readImage } = await import(resolve(here, '../../../deps/cartwheel-engine/tools/lib/png.mjs'));
const [, , inp, out, X, Y, W, H] = process.argv, x0 = +X, y0 = +Y, w = +W, h = +H, im = readImage(inp), raw = Buffer.alloc((w * 3 + 1) * h);
for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const s = 4 * ((y + y0) * im.w + x + x0), d = y * (w * 3 + 1) + 1 + x * 3; raw[d] = im.rgba[s]; raw[d + 1] = im.rgba[s + 1]; raw[d + 2] = im.rgba[s + 2]; }
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td) >>> 0); return Buffer.concat([l, td, c]); };
const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 2;
writeFileSync(out, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]));
