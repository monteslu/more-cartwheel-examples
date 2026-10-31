// Pixels near black in a band of a frame: the check for the black band along the far crests.
//   node black-count.mjs in.png [y0 y1]
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const { readImage } = await import(resolve(dirname(fileURLToPath(import.meta.url)), '../../../deps/cartwheel-engine/tools/lib/png.mjs'));
const im = readImage(process.argv[2]), y0 = +(process.argv[3] ?? 470), y1 = +(process.argv[4] ?? 520);
let n = 0;
for (let y = y0; y < y1; y++) for (let x = 0; x < im.w; x++) { const i = 4 * (y * im.w + x); if (im.rgba[i] < 10 && im.rgba[i + 1] < 10 && im.rgba[i + 2] < 10) n++; }
console.log(process.argv[2].split('/').pop(), 'near-black pixels in rows', y0, 'to', y1, ':', n);
