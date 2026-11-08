// Pixel measures for skyfighter's look checks: crops, contact sheets and how well a thing stands out from what is
// around it (WCAG relative luminance and contrast ratio).
import { readImage as readPng, writePng } from '../../deps/cartwheel-engine/tools/lib/png.mjs';
export const readImage = (p) => { const i = readPng(p); return { width: i.w, height: i.h, data: i.rgba }; };
const lin = (c) => { c /= 255; return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };
export const lum = (img, x, y) => { const i = (y * img.width + x) * 4, d = img.data; return 0.2126 * lin(d[i]) + 0.7152 * lin(d[i + 1]) + 0.0722 * lin(d[i + 2]); };
/* a box (cx, cy, w, h) of img: background = the median luminance of the box's border, footprint = pixels whose
 * luminance is off it by over `off` (relative), contrast = WCAG ratio of the 12 most different pixels to the background */
export function standOut(img, cx, cy, w, h, off = 0.03) {
  const x0 = Math.max(0, Math.round(cx - w / 2)), y0 = Math.max(0, Math.round(cy - h / 2)), x1 = Math.min(img.width - 1, x0 + w), y1 = Math.min(img.height - 1, y0 + h);
  const border = [];
  for (let x = x0; x <= x1; x++) border.push(lum(img, x, y0), lum(img, x, y1));
  for (let y = y0; y <= y1; y++) border.push(lum(img, x0, y), lum(img, x1, y));
  border.sort((a, b) => a - b);
  const bg = border[border.length >> 1], diffs = [];
  let footprint = 0;
  for (let y = y0 + 1; y < y1; y++) for (let x = x0 + 1; x < x1; x++) { const l = lum(img, x, y); if (Math.abs(l - bg) > off) footprint++; diffs.push(l); }
  diffs.sort((a, b) => Math.abs(b - bg) - Math.abs(a - bg));
  const top = diffs.slice(0, 12), t = top.reduce((a, b) => a + b, 0) / top.length;
  const contrast = (Math.max(t, bg) + 0.05) / (Math.min(t, bg) + 0.05);
  return { bg, target: t, contrast, footprint };
}
export function crop(img, cx, cy, w, h) {
  const out = new Uint8Array(w * h * 4), x0 = Math.round(cx - w / 2), y0 = Math.round(cy - h / 2);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const sx = Math.min(img.width - 1, Math.max(0, x0 + x)), sy = Math.min(img.height - 1, Math.max(0, y0 + y));
    for (let c = 0; c < 4; c++) out[(y * w + x) * 4 + c] = img.data[(sy * img.width + sx) * 4 + c];
  }
  return { width: w, height: h, data: out };
}
/* tiles in a grid (each scaled by `zoom`, nearest), separated by 4 px of grey */
export function sheet(path, tiles, cols, zoom = 1) {
  const tw = Math.max(...tiles.map((t) => t.width)) * zoom, th = Math.max(...tiles.map((t) => t.height)) * zoom, rows = Math.ceil(tiles.length / cols);
  const W = cols * (tw + 4) + 4, H = rows * (th + 4) + 4, data = new Uint8Array(W * H * 4).fill(64);
  tiles.forEach((t, i) => {
    const ox = 4 + (i % cols) * (tw + 4), oy = 4 + Math.floor(i / cols) * (th + 4);
    for (let y = 0; y < t.height * zoom; y++) for (let x = 0; x < t.width * zoom; x++) {
      const s = (Math.floor(y / zoom) * t.width + Math.floor(x / zoom)) * 4, d = ((oy + y) * W + ox + x) * 4;
      for (let c = 0; c < 4; c++) data[d + c] = t.data[s + c];
    }
  });
  writePng(path, W, H, data);
}
