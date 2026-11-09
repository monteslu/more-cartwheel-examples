// A small vector rasterizer for the sign and logo art (tools/make-signs.mjs): TrueType glyph outlines read from the
// game's own fonts (simple and composite glyphs, cmap format 4), paths filled with the nonzero rule at 4 sub-rows a
// pixel with exact span ends across the row, onto a linear-light RGBA float canvas. Deterministic, no dependencies.
import { readFileSync } from 'node:fs';

/** a TrueType font: glyph(char) -> { contours: [[{x, y, on}]], advance }, units per em */
export function loadFont(path) {
  const b = readFileSync(path);
  const u16 = (o) => b.readUInt16BE(o), i16 = (o) => b.readInt16BE(o), u32 = (o) => b.readUInt32BE(o);
  const tables = {};
  for (let i = 0, n = u16(4); i < n; i++) { const o = 12 + i * 16; tables[b.toString('latin1', o, o + 4)] = u32(o + 8); }
  const head = tables.head, upem = u16(head + 18), longLoca = i16(head + 50) === 1;
  const numGlyphs = u16(tables.maxp + 4), numH = u16(tables.hhea + 34);
  const loca = (g) => (longLoca ? u32(tables.loca + g * 4) : u16(tables.loca + g * 2) * 2);
  const advance = (g) => u16(tables.hmtx + Math.min(g, numH - 1) * 4);
  // cmap: the Windows Unicode BMP subtable, format 4
  let sub = 0;
  for (let i = 0, n = u16(tables.cmap + 2); i < n; i++) {
    const o = tables.cmap + 4 + i * 8;
    if ((u16(o) === 3 && u16(o + 2) === 1) || u16(o) === 0) sub = tables.cmap + u32(o + 4);
  }
  if (!sub || u16(sub) !== 4) throw new Error(`${path}: no cmap format 4`);
  const segX2 = u16(sub + 6), ends = sub + 14, starts = ends + segX2 + 2, deltas = starts + segX2, offs = deltas + segX2;
  const index = (code) => {
    for (let s = 0; s < segX2 / 2; s++) {
      if (u16(ends + s * 2) < code) continue;
      if (u16(starts + s * 2) > code) return 0;
      const ro = u16(offs + s * 2);
      if (!ro) return (code + i16(deltas + s * 2)) & 0xffff;
      const g = u16(offs + s * 2 + ro + (code - u16(starts + s * 2)) * 2);
      return g ? (g + i16(deltas + s * 2)) & 0xffff : 0;
    }
    return 0;
  };
  const outline = (g, depth = 0) => {
    if (g >= numGlyphs) return [];
    const at = tables.glyf + loca(g);
    if (loca(g + 1) === loca(g)) return [];
    const nc = i16(at);
    if (nc < 0) { // composite: its parts, each moved by its offset (scale flags are rare in display fonts: honoured when uniform)
      const out = [];
      let o = at + 10, more = true;
      while (more && depth < 4) {
        const flags = u16(o), gi = u16(o + 2);
        o += 4;
        let dx, dy;
        if (flags & 1) { dx = i16(o); dy = i16(o + 2); o += 4; } else { dx = b.readInt8(o); dy = b.readInt8(o + 1); o += 2; }
        let sc = 1;
        if (flags & 8) { sc = i16(o) / 16384; o += 2; } else if (flags & 0x40) o += 4; else if (flags & 0x80) o += 8;
        for (const c of outline(gi, depth + 1)) out.push(c.map((p) => ({ x: p.x * sc + dx, y: p.y * sc + dy, on: p.on })));
        more = !!(flags & 0x20);
      }
      return out;
    }
    const endPts = [];
    for (let i = 0; i < nc; i++) endPts.push(u16(at + 10 + i * 2));
    const np = nc ? endPts[nc - 1] + 1 : 0;
    let o = at + 10 + nc * 2;
    o += 2 + u16(o);
    const fl = [];
    while (fl.length < np) { const f = b[o++]; fl.push(f); if (f & 8) { let r = b[o++]; while (r--) fl.push(f); } }
    const xs = [], ys = [];
    for (let i = 0, v = 0; i < np; i++) { const f = fl[i]; if (f & 2) { v += f & 16 ? b[o] : -b[o]; o++; } else if (!(f & 16)) { v += i16(o); o += 2; } xs.push(v); }
    for (let i = 0, v = 0; i < np; i++) { const f = fl[i]; if (f & 4) { v += f & 32 ? b[o] : -b[o]; o++; } else if (!(f & 32)) { v += i16(o); o += 2; } ys.push(v); }
    const out = [];
    for (let c = 0, s = 0; c < nc; c++) { const pts = []; for (let i = s; i <= endPts[c]; i++) pts.push({ x: xs[i], y: ys[i], on: !!(fl[i] & 1) }); out.push(pts); s = endPts[c] + 1; }
    return out;
  };
  return { upem, glyph: (ch) => { const g = index(ch.codePointAt(0)); return { contours: outline(g), advance: advance(g) }; } };
}

/** a quadratic contour (TrueType on/off points) flattened to a polygon */
function flatten(pts, steps = 8) {
  const n = pts.length, poly = [];
  if (!n) return poly;
  let start = pts.findIndex((p) => p.on);
  const P = (i) => pts[(i + n) % n];
  let first;
  if (start < 0) { first = { x: (pts[0].x + pts[1 % n].x) / 2, y: (pts[0].y + pts[1 % n].y) / 2 }; start = 0; } else first = P(start);
  poly.push([first.x, first.y]);
  let cur = first;
  for (let k = 1; k <= n; k++) {
    const p = P(start + k);
    if (p.on) { poly.push([p.x, p.y]); cur = p; continue; }
    const nx = P(start + k + 1), end = nx.on ? nx : { x: (p.x + nx.x) / 2, y: (p.y + nx.y) / 2 };
    for (let s = 1; s <= steps; s++) { const t = s / steps, a = (1 - t) * (1 - t), m = 2 * (1 - t) * t, c = t * t; poly.push([a * cur.x + m * p.x + c * end.x, a * cur.y + m * p.y + c * end.y]); }
    cur = end;
    if (nx.on) k++;
  }
  return poly;
}

/** text as polygons in pixels: size = the em in px, baseline at (x, y), shear (italic, x per y), squeeze (x scale), track (px between letters) */
export function textPolys(font, text, { x = 0, y = 0, size = 100, shear = 0, squeeze = 1, track = 0 } = {}) {
  const s = size / font.upem, polys = [];
  let pen = 0;
  for (const ch of text) {
    const g = font.glyph(ch);
    for (const c of g.contours) polys.push(flatten(c).map(([gx, gy]) => [x + (pen + gx * squeeze) * s + gy * s * shear, y - gy * s]));
    pen += g.advance * squeeze + track / s;
  }
  return { polys, width: pen * s };
}
export function textWidth(font, text, o = {}) { return textPolys(font, text, o).width; }

/** coverage (0..1 per pixel) of polygons, nonzero winding */
export function coverage(w, h, polys) {
  const cov = new Float32Array(w * h), SUB = 4;
  const edges = [];
  for (const p of polys) for (let i = 0; i < p.length; i++) {
    const [x0, y0] = p[i], [x1, y1] = p[(i + 1) % p.length];
    if (y0 !== y1) edges.push(y0 < y1 ? [x0, y0, x1, y1, 1] : [x1, y1, x0, y0, -1]);
  }
  let ymin = h, ymax = 0;
  for (const e of edges) { ymin = Math.min(ymin, e[1]); ymax = Math.max(ymax, e[3]); }
  const r0 = Math.max(0, Math.floor(ymin * SUB)), r1 = Math.min(h * SUB, Math.ceil(ymax * SUB));
  for (let r = r0; r < r1; r++) {
    const sy = (r + 0.5) / SUB, row = Math.floor(r / SUB) * w, xs = [];
    for (const [x0, y0, x1, y1, d] of edges) if (sy >= y0 && sy < y1) xs.push([x0 + ((sy - y0) / (y1 - y0)) * (x1 - x0), d]);
    xs.sort((a, b) => a[0] - b[0]);
    let wind = 0;
    for (let i = 0; i < xs.length - 1; i++) {
      wind += xs[i][1];
      if (!wind) continue;
      const a = Math.max(0, xs[i][0]), bb = Math.min(w, xs[i + 1][0]);
      if (bb <= a) continue;
      const ia = Math.floor(a), ib = Math.floor(bb);
      if (ia === ib) { cov[row + ia] += (bb - a) / SUB; continue; }
      cov[row + ia] += (ia + 1 - a) / SUB;
      for (let x = ia + 1; x < ib && x < w; x++) cov[row + x] += 1 / SUB;
      if (ib < w) cov[row + ib] += (bb - ib) / SUB;
    }
  }
  for (let i = 0; i < cov.length; i++) cov[i] = Math.min(1, cov[i]);
  return cov;
}

/** a linear-light RGBA float canvas */
export function canvas(w, h) {
  const px = new Float32Array(w * h * 4);
  const c = {
    w, h, px,
    /** paint where cov > 0 with color(x, y) -> [r, g, b, a] (linear), source over */
    paint(cov, color) {
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const i = y * w + x, k = cov[i];
        if (k <= 0) continue;
        const [r, g, b, a = 1] = typeof color === 'function' ? color(x, y) : color;
        const sa = a * k, da = px[i * 4 + 3], na = sa + da * (1 - sa);
        if (na <= 0) continue;
        for (let j = 0; j < 3; j++) px[i * 4 + j] = ([r, g, b][j] * sa + px[i * 4 + j] * da * (1 - sa)) / na;
        px[i * 4 + 3] = na;
      }
      return c;
    },
    fill(polys, color) { return c.paint(coverage(w, h, polys), color); },
    /** sRGB 8-bit RGBA */
    bytes() {
      const out = new Uint8Array(w * h * 4), g = (v) => Math.round(Math.min(1, Math.max(0, v)) ** (1 / 2.2) * 255);
      for (let i = 0; i < w * h; i++) { out[i * 4] = g(px[i * 4]); out[i * 4 + 1] = g(px[i * 4 + 1]); out[i * 4 + 2] = g(px[i * 4 + 2]); out[i * 4 + 3] = Math.round(Math.min(1, px[i * 4 + 3]) * 255); }
      return out;
    },
  };
  return c;
}

/** a coverage blurred (box, radius r, twice: near gaussian), for glows, shadows and outlines */
export function blur(cov, w, h, r) {
  const tmp = new Float32Array(cov.length);
  let a = Float32Array.from(cov);
  for (let pass = 0; pass < 2; pass++) {
    for (let y = 0; y < h; y++) { let s = 0; for (let x = -r; x <= r; x++) s += a[y * w + Math.min(w - 1, Math.max(0, x))]; for (let x = 0; x < w; x++) { tmp[y * w + x] = s / (2 * r + 1); s += a[y * w + Math.min(w - 1, x + r + 1)] - a[y * w + Math.max(0, x - r)]; } }
    for (let x = 0; x < w; x++) { let s = 0; for (let y = -r; y <= r; y++) s += tmp[Math.min(h - 1, Math.max(0, y)) * w + x]; for (let y = 0; y < h; y++) { a[y * w + x] = s / (2 * r + 1); s += tmp[Math.min(h - 1, y + r + 1) * w + x] - tmp[Math.max(0, y - r) * w + x]; } }
  }
  return a;
}
/** a coverage grown by r pixels (an outline: grow, then paint under the original) */
export function grow(cov, w, h, r) { const b = blur(cov, w, h, Math.max(1, Math.round(r / 2))); return b.map((v) => Math.min(1, v * 3)); }
/** a coverage moved by (dx, dy) whole pixels */
export function shift(cov, w, h, dx, dy) {
  const out = new Float32Array(cov.length);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const sx = x - dx, sy = y - dy; if (sx >= 0 && sy >= 0 && sx < w && sy < h) out[y * w + x] = cov[sy * w + sx]; }
  return out;
}
