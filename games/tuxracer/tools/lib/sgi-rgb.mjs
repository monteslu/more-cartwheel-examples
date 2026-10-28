// SGI .rgb (IRIS RGB) decoder: 1 byte per channel, RLE or raw, 1..4 channels.
// Returns { w, h, ch, data } with data[(x + y*w)*ch + c] and row 0 at the
// BOTTOM of the image, exactly as Tux Racer's image.c hands it to course_load.c.
import { readFileSync } from 'node:fs';

export function readSgi(path) {
  const b = readFileSync(path);
  if (b.readUInt16BE(0) !== 474) throw new Error(`${path}: not an SGI image`);
  const storage = b[2], bpc = b[3];
  const w = b.readUInt16BE(6), h = b.readUInt16BE(8), ch = b.readUInt16BE(10);
  if (bpc !== 1) throw new Error(`${path}: ${bpc} bytes per channel unsupported`);
  const data = new Uint8Array(w * h * ch);
  if (storage === 0) {
    for (let c = 0; c < ch; c++)
      for (let y = 0; y < h; y++) {
        const off = 512 + (y * w) + (c * w * h);
        for (let x = 0; x < w; x++) data[(x + y * w) * ch + c] = b[off + x];
      }
  } else {
    const tables = h * ch;
    const starts = [], sizes = [];
    for (let i = 0; i < tables; i++) { starts.push(b.readUInt32BE(512 + i * 4)); sizes.push(b.readUInt32BE(512 + tables * 4 + i * 4)); }
    for (let c = 0; c < ch; c++)
      for (let y = 0; y < h; y++) {
        let p = starts[y + c * h];
        const end = p + sizes[y + c * h];
        let x = 0;
        while (p < end) {
          const pixel = b[p++];
          let count = pixel & 0x7f;
          if (!count) break;
          if (pixel & 0x80) { while (count--) data[(x++ + y * w) * ch + c] = b[p++]; }
          else { const v = b[p++]; while (count--) data[(x++ + y * w) * ch + c] = v; }
        }
      }
  }
  return { w, h, ch, data };
}

// RGBA8 top-down copy (for PNGs); alpha 255 when the image has none.
export function toRgbaTopDown(img) {
  const { w, h, ch, data } = img;
  const out = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const s = (x + (h - 1 - y) * w) * ch, d = (x + y * w) * 4;
    if (ch === 1) { out[d] = out[d + 1] = out[d + 2] = data[s]; out[d + 3] = 255; }
    else if (ch === 2) { out[d] = out[d + 1] = out[d + 2] = data[s]; out[d + 3] = data[s + 1]; }
    else { out[d] = data[s]; out[d + 1] = data[s + 1]; out[d + 2] = data[s + 2]; out[d + 3] = ch === 4 ? data[s + 3] : 255; }
  }
  return out;
}
