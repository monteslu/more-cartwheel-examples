// Generates every PBR texture set the game uses into assets/textures/ (albedo, normal, occlusion-roughness-metal,
// emissive where the surface glows). All of it is made here, by our own code; nothing is downloaded. Run:
//   node tools/make-textures.mjs [name ...]
// Material functions fill an `o` record per pixel: r g b (sRGB albedo), h (height), ro (roughness), me (metal), ao,
// and er eg eb (emissive, sRGB). bake() turns one into a Tex and writeSet() saves it.
import { Tex, writeSet, fbm, cells, hash2, smooth, clamp, lerp, hex, save } from './texlib.mjs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

export const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'textures');
const MAIN = process.argv[1] && import.meta.url.endsWith(process.argv[1].split('/').pop());
const only = MAIN ? process.argv.slice(2) : ['none'];
const want = (n) => MAIN && (only.length === 0 || only.includes(n));

export function bake(size, px, w2) {
  const t = new Tex(size, w2 ?? size);
  const o = { r: 0, g: 0, b: 0, h: 0, ro: 0.8, me: 0, ao: 1, er: 0, eg: 0, eb: 0, em: false };
  let emi = null;
  t.each((u, v, i) => {
    o.r = o.g = o.b = 0; o.h = 0; o.ro = 0.8; o.me = 0; o.ao = 1; o.er = o.eg = o.eb = 0; o.em = false;
    px(u, v, o, i);
    t.a[0][i] = o.r; t.a[1][i] = o.g; t.a[2][i] = o.b; t.height[i] = o.h; t.rough[i] = o.ro; t.metal[i] = o.me; t.ao[i] = o.ao;
    if (o.em || o.er > 0 || o.eg > 0 || o.eb > 0) { emi ||= t.emissive(); emi[0][i] = o.er; emi[1][i] = o.eg; emi[2][i] = o.eb; }
  });
  return t;
}
const mixc = (o, c, k) => { o.r = lerp(o.r, c[0], k); o.g = lerp(o.g, c[1], k); o.b = lerp(o.b, c[2], k); };
const setc = (o, c, k = 1) => { o.r = c[0] * k; o.g = c[1] * k; o.b = c[2] * k; };
const mulc = (o, k) => { o.r *= k; o.g *= k; o.b *= k; };

// ── surfaces ───────────────────────────────────────────────────────────
/** asphalt with aggregate, cracks, patches, oil and puddles; wet is 0..1 */
function asphalt({ base = [0.2, 0.2, 0.21], wet = 0, seed = 1, stripe = false }) {
  return (u, v, o, i) => {
    const w = 1024;
    const g = fbm(u, v, 3, 5, seed);
    const x = Math.floor(u * w), y = Math.floor(v * w);
    const grain = hash2(x, y, seed + 7), grain2 = hash2(x >> 1, y >> 1, seed + 9);
    const [f1, , pid] = cells(u, v, 110, seed + 3, 1);
    const rad = 0.12 + 0.34 * pid;
    const peb = smooth(rad, rad * 0.35, f1) * (0.3 + 0.7 * grain2) * 0.75;
    const wu = (u + 0.06 * (fbm(u, v, 9, 3, seed + 71) - 0.5) + 1) % 1, wv = (v + 0.06 * (fbm(u, v, 9, 3, seed + 72) - 0.5) + 1) % 1;
    const [c1, c2] = cells(wu, wv, 4, seed + 11, 0.9);
    const crackMask = smooth(0.48, 0.62, fbm(u, v, 6, 3, seed + 21));
    const crack = (1 - smooth(0.0, 0.035 + 0.02 * g, c2 - c1)) * crackMask;
    const patch = smooth(0.55, 0.6, fbm(u, v, 2, 3, seed + 31));
    const stain = smooth(0.6, 0.8, fbm(u, v, 5, 4, seed + 41));
    const k = 0.78 + 0.5 * g + 0.22 * (grain - 0.5) + peb * 0.35;
    setc(o, base, k);
    mulc(o, 1 - patch * 0.22);
    mulc(o, 1 - stain * 0.35);
    mulc(o, 1 - crack * 0.8);
    o.h = grain * 0.3 + peb * 0.5 + g * 0.25 - crack * 1.1 - patch * 0.1;
    o.ro = clamp(0.88 - peb * 0.15 + stain * 0.08 - patch * 0.05, 0.4, 1);
    o.ao = 1 - crack * 0.6;
    if (wet > 0) {
      const pud = smooth(0.58, 0.66, fbm(u, v, 3, 4, seed + 51)) * wet;
      mulc(o, 1 - pud * 0.5); o.ro = lerp(o.ro, 0.06, pud); o.h = lerp(o.h, 0.15, pud);
      o.ro = lerp(o.ro, 0.55, (1 - pud) * wet * 0.3);
    }
    if (stripe) { // a faded painted line across the tile
      const d = Math.abs(v - 0.5);
      const wear = smooth(0.35, 0.65, fbm(u, v, 10, 3, seed + 61));
      const s = (1 - smooth(0.012, 0.02, d)) * (0.35 + 0.65 * wear);
      mixc(o, [0.75, 0.7, 0.45], s * 0.9); o.ro = lerp(o.ro, 0.55, s);
    }
  };
}
function slabs({ base = [0.5, 0.5, 0.48], n = 2, seed = 2, tint = [1, 1, 1] }) {
  return (u, v, o) => {
    const sx = u * n, sy = v * n, fx = sx - Math.floor(sx), fy = sy - Math.floor(sy);
    const id = hash2(Math.floor(sx), Math.floor(sy), seed);
    const e = Math.min(fx, 1 - fx, fy, 1 - fy);
    const joint = 1 - smooth(0.008, 0.028, e);
    const edge = 1 - smooth(0.02, 0.08, e);
    const g = fbm(u, v, 6, 5, seed), dirt = smooth(0.5, 0.85, fbm(u, v, 3, 4, seed + 5));
    const grain = hash2(Math.floor(u * 512), Math.floor(v * 512), seed + 3);
    const k = 0.78 + 0.45 * g + 0.2 * (id - 0.5) + 0.1 * (grain - 0.5);
    setc(o, [base[0] * tint[0], base[1] * tint[1], base[2] * tint[2]], k);
    mulc(o, 1 - dirt * 0.3); mulc(o, 1 - joint * 0.65); mulc(o, 1 - edge * 0.08);
    const gum = smooth(0.82, 0.9, fbm(u, v, 24, 2, seed + 9));
    mulc(o, 1 - gum * 0.4);
    o.h = g * 0.25 + (grain - 0.5) * 0.12 - joint * 0.9 - edge * 0.15;
    o.ro = clamp(0.8 - g * 0.1 + dirt * 0.1 + joint * 0.15, 0.3, 1);
    o.ao = 1 - joint * 0.55;
  };
}
function brick({ cols = [[0.55, 0.26, 0.2], [0.5, 0.22, 0.17], [0.42, 0.2, 0.17]], mortar = [0.4, 0.38, 0.35], seed = 3, rows = 12, nx = 4, soot = 0.4 }) {
  return (u, v, o) => {
    const ry = v * rows, row = Math.floor(ry), fy = ry - row;
    const rx = u * nx + (row & 1 ? 0.5 : 0), col = Math.floor(rx), fx = rx - col;
    const id = hash2(((col % nx) + nx) % nx, row, seed);
    const dx = Math.min(fx, 1 - fx) * (1 / nx) * 3.2, dy = Math.min(fy, 1 - fy) * (1 / rows) * 3.2;
    const e = Math.min(dx * nx, dy * rows) * 0.5;
    const inb = smooth(0.012, 0.045, Math.min(fx * 1, 1 - fx, fy * 2.5, (1 - fy) * 2.5) * 0.9);
    const g = fbm(u, v, 8, 4, seed), g2 = fbm(u * 3, v * 3, 8, 3, seed + 4);
    const c = cols[Math.floor(id * cols.length) % cols.length];
    const grain = hash2(Math.floor(u * 512), Math.floor(v * 512), seed + 1);
    const k = 0.72 + 0.45 * g2 + 0.25 * (grain - 0.5);
    const bc = [c[0] * k, c[1] * k, c[2] * k];
    const mc = [mortar[0] * (0.75 + 0.3 * g), mortar[1] * (0.75 + 0.3 * g), mortar[2] * (0.75 + 0.3 * g)];
    o.r = lerp(mc[0], bc[0], inb); o.g = lerp(mc[1], bc[1], inb); o.b = lerp(mc[2], bc[2], inb);
    const streak = smooth(0.5, 0.9, fbm(u * 6, v * 0.8, 6, 4, seed + 7));
    mulc(o, 1 - streak * soot);
    const chip = smooth(0.78, 0.9, fbm(u, v, 30, 2, seed + 13)) * inb;
    mulc(o, 1 - chip * 0.35);
    o.h = inb * (0.8 + 0.2 * g2 + 0.3 * (grain - 0.5)) - chip * 0.2;
    o.ro = lerp(0.95, 0.8, inb) - streak * 0.05;
    o.ao = lerp(0.55, 1, inb);
  };
}
function plaster({ base = [0.62, 0.58, 0.5], seed = 4, stain = 0.35 }) {
  return (u, v, o) => {
    const g = fbm(u, v, 12, 5, seed), g2 = fbm(u, v, 4, 4, seed + 3);
    const grain = hash2(Math.floor(u * 512), Math.floor(v * 512), seed);
    const streak = smooth(0.45, 0.85, fbm(u * 7, v * 0.7, 7, 4, seed + 5));
    setc(o, base, 0.8 + 0.3 * g2 + 0.1 * (grain - 0.5));
    mulc(o, 1 - streak * stain);
    const [c1, c2] = cells(u, v, 3, seed + 8, 0.9);
    const crack = (1 - smooth(0, 0.03, c2 - c1)) * smooth(0.55, 0.65, fbm(u, v, 5, 3, seed + 9));
    mulc(o, 1 - crack * 0.7);
    o.h = g * 0.7 + (grain - 0.5) * 0.2 - crack * 1.0;
    o.ro = clamp(0.9 - g2 * 0.1 + streak * 0.05, 0.5, 1);
    o.ao = 1 - crack * 0.5;
  };
}
function corrugated({ color = [0.3, 0.38, 0.42], ribs = 16, seed = 5, rust = 0.5, paint = true }) {
  return (u, v, o) => {
    const s = Math.sin(u * ribs * Math.PI * 2);
    const g = fbm(u, v, 5, 5, seed), g2 = fbm(u * 2, v * 0.4, 6, 4, seed + 3);
    const rustm = smooth(1 - rust - 0.05, 1 - rust + 0.12, g * 0.7 + g2 * 0.4);
    const scr = smooth(0.8, 0.95, fbm(u * 40, v * 3, 40, 2, seed + 7));
    setc(o, color, 0.8 + 0.18 * s + 0.15 * (g - 0.5));
    mulc(o, 1 - 0.2 * (1 - smooth(0, 0.9, Math.abs(s))) * 0.3);
    mixc(o, [0.45 + 0.1 * g, 0.22 + 0.06 * g, 0.1], rustm * 0.85);
    mixc(o, [0.75, 0.75, 0.72], scr * 0.4 * (1 - rustm));
    o.h = s * 0.9 - rustm * 0.15 + g * 0.1;
    o.ro = lerp(0.42, 0.88, clamp(rustm + (paint ? 0.2 : 0)));
    o.me = (1 - rustm) * (paint ? 0.2 : 0.75);
    o.ao = 0.85 + 0.15 * s;
  };
}
function plate({ color = [0.3, 0.31, 0.33], seed = 6, rust = 0.4, diamonds = 8, panels = 2 }) {
  return (u, v, o) => {
    const dx = u * diamonds, dy = v * diamonds;
    const lx = dx - Math.floor(dx) - 0.5, ly = dy - Math.floor(dy) - 0.5;
    const alt = (Math.floor(dx) + Math.floor(dy)) & 1;
    const ax = alt ? lx : ly, ay = alt ? ly : lx;
    const rib = smooth(0.34, 0.1, Math.abs(ax) * 0.45 + Math.abs(ay) * 1.4);
    const px = u * panels, py = v * panels, fx = px - Math.floor(px), fy = py - Math.floor(py);
    const seam = 1 - smooth(0.006, 0.02, Math.min(fx, 1 - fx, fy, 1 - fy) / panels);
    const g = fbm(u, v, 6, 5, seed), rm = smooth(1 - rust, 1 - rust + 0.2, fbm(u, v, 4, 5, seed + 5) * 0.8 + g * 0.3);
    const worn = smooth(0.55, 0.8, fbm(u, v, 9, 3, seed + 8));
    setc(o, color, 0.7 + 0.35 * g + rib * 0.15);
    mulc(o, 1 - seam * 0.55);
    mixc(o, [0.3, 0.2, 0.14], rm * 0.6);
    mixc(o, [0.6, 0.6, 0.62], worn * rib * 0.35 * (1 - rm));
    o.h = rib * 0.9 - seam * 0.9 - rm * 0.2;
    o.ro = lerp(0.38, 0.9, clamp(rm + (1 - rib) * 0.25));
    o.me = (1 - rm) * 0.75;
    o.ao = 1 - seam * 0.5;
  };
}
function planks({ color = [0.42, 0.28, 0.16], n = 6, seed = 7, wet = 0, grey = 0, knots = true }) {
  return (u, v, o) => {
    const py = v * n, row = Math.floor(py), fy = py - row;
    const id = hash2(row, 0, seed), off = hash2(row, 1, seed);
    const gap = 1 - smooth(0.012, 0.035, Math.min(fy, 1 - fy));
    const grainv = fbm(u * 1 + off, v * n * 0.15, 4, 4, seed + row) * 0.5 + fbm(u * 24 + off * 7, v * 1, 24, 3, seed + 2) * 0.5;
    const joint = 1 - smooth(0.004, 0.012, Math.abs(((u + off) * 2) % 1 - 0.5) * 0.02 + 0.002 * 0 + Math.abs((u * 2 + off) % 1 - 0.0) * 0);
    const k = 0.7 + 0.5 * grainv + 0.25 * (id - 0.5);
    setc(o, color, k);
    if (grey) mixc(o, [0.45, 0.43, 0.4], grey * (0.5 + 0.5 * fbm(u, v, 5, 3, seed + 3)));
    mulc(o, 1 - gap * 0.7);
    if (knots) { const [f1] = cells(u, v * n, 3, seed + 4, 1); const kn = smooth(0.2, 0.05, f1) * smooth(0.55, 0.8, hash2(Math.floor(u * 3), row, seed + 6)); mulc(o, 1 - kn * 0.4); o.h -= kn * 0.3; }
    const nail = (Math.abs(((u * 6) % 1) - 0.5) < 0.03 && Math.abs(fy - 0.5) < 0.2) ? 0.5 : 0;
    mulc(o, 1 - nail * 0.5);
    o.h += grainv * 0.4 - gap * 0.9 - nail * 0.4 + joint * 0;
    o.ro = clamp(0.78 - wet * 0.35 + gap * 0.2, 0.2, 1);
    o.ao = 1 - gap * 0.6;
  };
}
function tiles({ a = [0.08, 0.08, 0.1], b = [0.12, 0.1, 0.16], n = 4, seed = 8, inlay = [0.5, 0.2, 0.7] }) {
  return (u, v, o) => {
    const sx = u * n, sy = v * n, fx = sx - Math.floor(sx), fy = sy - Math.floor(sy);
    const chk = (Math.floor(sx) + Math.floor(sy)) & 1;
    const e = Math.min(fx, 1 - fx, fy, 1 - fy);
    const grout = 1 - smooth(0.01, 0.03, e);
    const g = fbm(u, v, 8, 5, seed), scuff = smooth(0.5, 0.8, fbm(u * 2, v * 9, 8, 4, seed + 2));
    setc(o, chk ? a : b, 0.85 + 0.5 * g);
    const diag = Math.abs((fx - fy) * 0.7);
    mixc(o, inlay, (1 - smooth(0.0, 0.02, Math.abs(diag - 0.2) - 0.0)) * 0.4 * (hash2(Math.floor(sx), Math.floor(sy), seed) > 0.7 ? 1 : 0));
    mixc(o, [0.04, 0.04, 0.05], grout * 0.9);
    mulc(o, 1 - scuff * 0.2);
    o.h = -grout * 0.8 + g * 0.06;
    o.ro = clamp(0.1 + grout * 0.5 + scuff * 0.35 + 0.1 * g, 0.05, 1);
    o.ao = 1 - grout * 0.5;
  };
}
/** a building face: a wall with a 4 x 4 grid of windows, lit or dark, frames, sills, glass. style picks wall and look */
function facade({ wall, cells: nc = 4, seed = 9, lit = 0.5, lamps = [[1, 0.78, 0.45], [1, 0.62, 0.3], [0.7, 0.85, 1]], glass = [0.04, 0.06, 0.09], frame = [0.18, 0.17, 0.17], win = [0.2, 0.16, 0.8, 0.84], neon = false, bars = false, slit = false }) {
  return (u, v, o) => {
    wall(u, v, o);
    const cx = u * nc, cy = v * nc, ci = Math.floor(cx), cj = Math.floor(cy), fx = cx - ci, fy = cy - cj;
    let [x0, y0, x1, y1] = win;
    if (slit) { x0 = 0.38; x1 = 0.62; y0 = 0.1; y1 = 0.9; }
    const inx = fx > x0 && fx < x1, iny = fy > y0 && fy < y1;
    const fw = 0.045;
    const idc = hash2(ci, cj, seed);
    // sill and lintel (raised courses of stone)
    const sill = fx > x0 - 0.05 && fx < x1 + 0.05 && fy > y1 && fy < y1 + 0.05;
    const lint = fx > x0 - 0.05 && fx < x1 + 0.05 && fy > y0 - 0.06 && fy <= y0;
    if (sill || lint) { setc(o, [0.55, 0.53, 0.5], 0.9 + 0.2 * hash2(ci, cj, seed + 1)); o.h = 1.2; o.ro = 0.85; o.ao = 1; return; }
    if (!(inx && iny)) return;
    const gx = (fx - x0) / (x1 - x0), gy = (fy - y0) / (y1 - y0);
    const edge = Math.min(fx - x0, x1 - fx) < fw || Math.min(fy - y0, y1 - fy) < fw * 0.7;
    const mull = Math.abs(gx - 0.5) < 0.03 || (!slit && Math.abs(gy - 0.42) < 0.025);
    if (edge || mull) { setc(o, frame, 0.8 + 0.4 * hash2(ci, cj, seed + 2)); o.h = 0.9; o.ro = 0.6; o.me = 0.1; o.ao = 1; return; }
    // glass
    const isLit = idc < lit;
    const dirty = fbm(u, v, 16, 3, seed + 3);
    setc(o, glass, 0.8 + 0.5 * dirty);
    o.h = -0.5; o.ro = 0.06 + 0.12 * dirty; o.me = 0; o.ao = 0.9;
    // reflection of the street on dark glass: a soft diagonal sheen
    mixc(o, [0.2, 0.25, 0.32], smooth(0.2, 0.0, Math.abs(gx - gy * 0.8 - 0.2)) * 0.25);
    if (bars) { const b = Math.abs(((gx * 4) % 1) - 0.5) < 0.07; if (b) { setc(o, [0.1, 0.1, 0.1], 1); o.h = 0.6; o.ro = 0.7; return; } }
    if (isLit) {
      const lc = lamps[Math.floor(hash2(ci, cj, seed + 4) * lamps.length) % lamps.length];
      const room = 0.4 + 0.35 * hash2(ci, cj, seed + 5);
      // light falls from a ceiling lamp, darker at the floor; curtain at one side, a shape on the sill
      let k = room * (0.6 + 0.4 * (1 - gy)) * (0.75 + 0.25 * smooth(0.0, 0.5, 0.5 - Math.abs(gx - 0.5)));
      const curtain = (gx < 0.2 + 0.1 * hash2(ci, cj, seed + 6) || gx > 0.82 + 0.1 * hash2(ci, cj, seed + 7)) ? 0.55 : 1;
      const shape = gy > 0.75 && Math.abs(gx - (0.3 + 0.4 * hash2(ci, cj, seed + 8))) < 0.18 ? 0.35 : 1;
      k *= curtain * shape;
      o.er = lc[0] * k; o.eg = lc[1] * k; o.eb = lc[2] * k; o.em = true;
      mixc(o, [lc[0] * 0.5, lc[1] * 0.5, lc[2] * 0.5], 0.5);
    }
  };
}
/** neon signage over a dark painted wall: emissive strips and boxes */
function neonWall({ seed = 10, hues = [[1, 0.2, 0.7], [0.2, 0.9, 1], [1, 0.7, 0.2]] }) {
  const base = plaster({ base: [0.1, 0.1, 0.14], seed, stain: 0.5 });
  return (u, v, o) => {
    base(u, v, o);
    const row = Math.floor(v * 4), fy = v * 4 - row, col = Math.floor(u * 4), fx = u * 4 - col;
    const id = hash2(col, row, seed + 11);
    if (id > 0.55) { // a sign box with a neon border and bars
      const h = hues[Math.floor(hash2(col, row, seed + 12) * hues.length) % hues.length];
      const inb = fx > 0.1 && fx < 0.9 && fy > 0.22 && fy < 0.78;
      if (inb) {
        const e = Math.min(fx - 0.1, 0.9 - fx, fy - 0.22, 0.78 - fy);
        const border = e < 0.035;
        const bar = Math.abs(fy - 0.5) < 0.05 && fx > 0.2 && fx < 0.8;
        const dot = Math.hypot(fx - 0.3, fy - 0.5) < 0.07 && hash2(col, row, seed + 13) > 0.5;
        setc(o, [0.05, 0.05, 0.07], 1); o.ro = 0.35; o.me = 0; o.h = 0.4;
        if (border || bar || dot) { o.er = h[0] * 0.85; o.eg = h[1] * 0.85; o.eb = h[2] * 0.85; o.em = true; setc(o, h, 0.4); o.ro = 0.2; }
      }
    }
    // vertical neon tube between sign rows
    if (Math.abs(fx - 0.5) < 0.012 && hash2(col, 0, seed + 14) > 0.6) { const h = hues[col % hues.length]; o.er = h[0] * 0.6; o.eg = h[1] * 0.6; o.eb = h[2] * 0.6; o.em = true; }
  };
}

/** dark painted wall with neon tubes under each window row and glowing sign panels */
function neonStrips({ seed = 10, hues = [[1, 0.2, 0.7], [0.2, 0.9, 1], [1, 0.7, 0.2]] }) {
  const base = plaster({ base: [0.1, 0.1, 0.14], seed, stain: 0.5 });
  return (u, v, o) => {
    base(u, v, o);
    const row = Math.floor(v * 4), fy = v * 4 - row, col = Math.floor(u * 4), fx = u * 4 - col;
    const h = hues[Math.floor(hash2(0, row, seed) * hues.length) % hues.length];
    if (fy > 0.9 && fy < 0.95) { o.er = h[0] * 0.8; o.eg = h[1] * 0.8; o.eb = h[2] * 0.8; o.em = true; setc(o, h, 0.35); o.ro = 0.2; o.h = 0.5; }
    if (fx < 0.02 && hash2(col, row, seed + 3) > 0.45) { const h2 = hues[(col + row) % hues.length]; o.er = h2[0] * 0.7; o.eg = h2[1] * 0.7; o.eb = h2[2] * 0.7; o.em = true; }
    // a panel of tiles at street level
    if (row === 3 && fy > 0.5 && fy < 0.9) { const k = (Math.floor(fx * 8) + Math.floor(fy * 8)) & 1; mulc(o, 1 + (k ? 0.25 : -0.1)); o.ro = 0.3; }
  };
}

// ── fighter stage extras ────────────────────────────────────────────────
/** clay roof tiles: overlapping rounded rows with dark gaps, moss in the joints */
function roofTiles({ seed = 84 }) {
  return (u, v, o) => {
    const rows = 8, cols = 8, ry = v * rows, row = Math.floor(ry), fy = ry - row, cx = u * cols + (row & 1) * 0.5, fx = cx - Math.floor(cx);
    const round = Math.sin(fx * Math.PI), lap = smooth(0.0, 0.25, fy) * (1 - smooth(0.85, 1.0, fy));
    const g = fbm(u, v, 8, 4, seed), id = hash2(Math.floor(cx), row, seed), moss = smooth(0.6, 0.8, fbm(u, v, 5, 4, seed + 3)) * (1 - round);
    setc(o, [0.2, 0.21, 0.24], 0.75 + 0.3 * g + 0.2 * (id - 0.5));
    mixc(o, [0.22, 0.28, 0.16], moss * 0.6);
    mulc(o, 0.55 + 0.45 * lap * (0.5 + 0.5 * round));
    o.h = round * 0.8 + lap * 0.5 - (1 - lap) * 0.4; o.ro = 0.45 + 0.3 * (1 - round) + moss * 0.3; o.ao = 0.6 + 0.4 * lap; o.me = 0;
  };
}
/** a paper screen in a wooden lattice; the paper glows a little (lit from inside) */
function shoji({ seed = 85 }) {
  return (u, v, o) => {
    const gx = u * 4, gy = v * 6, fx = gx - Math.floor(gx), fy = gy - Math.floor(gy);
    const bar = Math.min(fx, 1 - fx) < 0.04 || Math.min(fy, 1 - fy) < 0.03;
    const fib = fbm(u * 3, v * 3, 24, 3, seed), g = fbm(u, v, 6, 3, seed + 2);
    if (bar) { setc(o, [0.32, 0.22, 0.13], 0.8 + 0.3 * g); o.h = 1; o.ro = 0.55; o.ao = 1; return; }
    setc(o, [0.94, 0.9, 0.8], 0.92 + 0.1 * fib); o.h = 0.1 * fib; o.ro = 0.9; o.ao = 0.95;
    const k = 0.55 + 0.25 * (1 - Math.abs(v - 0.45) * 1.6) + 0.1 * fib; o.er = 1.0 * k; o.eg = 0.72 * k; o.eb = 0.42 * k; o.em = true;
  };
}
/** tree bark: vertical ridges and cracks */
function bark({ seed = 87 }) {
  return (u, v, o) => {
    const r = fbm(u * 1, v * 0.25, 10, 4, seed), r2 = fbm(u, v, 24, 3, seed + 1);
    const ridge = Math.pow(1 - Math.abs(2 * fbm(u + 0.1 * r2, v * 0.3, 14, 3, seed + 2) - 1), 2);
    setc(o, [0.27, 0.2, 0.15], 0.6 + 0.6 * ridge + 0.2 * (r - 0.5));
    o.h = ridge * 1.2 + r2 * 0.2; o.ro = 0.85 + 0.1 * (1 - ridge); o.ao = 0.55 + 0.45 * ridge;
  };
}
/** a mass of small leaves (for foliage clumps): cells shaded as leaves with light and dark sides */
function leaves({ seed = 88 }) {
  return (u, v, o) => {
    const [f1, f2, id] = cells(u, v, 40, seed, 1), g = fbm(u, v, 4, 4, seed + 1);
    const edge = smooth(0.0, 0.12, f2 - f1), lit = 0.55 + 0.45 * id;
    setc(o, [1, 1, 1], (0.5 + 0.5 * lit) * (0.35 + 0.65 * edge) * (0.8 + 0.4 * g));
    o.h = edge * (1 - f1) * 0.8; o.ro = 0.6 + 0.2 * id; o.ao = 0.4 + 0.6 * edge;
  };
}
/** weathered rock for far mountains and garden stones */
function rock({ seed = 89 }) {
  return (u, v, o) => {
    const g = fbm(u, v, 4, 6, seed), [c1, c2] = cells(u, v, 6, seed + 2, 1), crack = 1 - smooth(0, 0.04, c2 - c1);
    const strata = 0.5 + 0.5 * Math.sin(v * 40 + g * 6);
    setc(o, [0.46, 0.45, 0.43], 0.7 + 0.5 * g + 0.08 * strata); mulc(o, 1 - crack * 0.5);
    o.h = g * 1.0 - crack * 0.8 + strata * 0.1; o.ro = 0.85; o.ao = 1 - crack * 0.5;
  };
}
// ── characters ──────────────────────────────────────────────────────────
/** soft cloth wrinkles: ridges stretched across the garment (u runs around a limb or body, v along it), two scales */
function wrinkles(u, v, seed) {
  const r1 = 1 - Math.abs(2 * fbm(u * 1, v * 1, 2, 3, seed + 40) - 1), r2 = 1 - Math.abs(2 * fbm(u, v * 2, 3, 3, seed + 41) - 1);
  const wa = fbm(u, v, 2, 2, seed + 42) - 0.5, wb = fbm(u, v, 3, 2, seed + 45) - 0.5;
  const w1 = fbm(u + 0.2 * wb, v + 0.35 * wa, 6, 2, seed + 43), w2 = fbm(u - 0.3 * wa, v + 0.3 * wb, 9, 2, seed + 44);
  return 0.7 * Math.pow(1 - Math.abs(2 * w1 - 1), 3) * (0.4 + 0.6 * r1) + 0.4 * Math.pow(1 - Math.abs(2 * w2 - 1), 4) * (0.3 + 0.7 * r2);
}
function fabric({ seed = 11, knit = false }) {
  return (u, v, o) => {
    const N = knit ? 64 : 112, x = u * N, y = v * N;
    const wx = Math.sin(x * Math.PI * 2), wy = Math.sin(y * Math.PI * 2);
    const over = ((Math.floor(x) + Math.floor(y)) & 1) ? wx : wy;
    const g = fbm(u, v, 12, 3, seed), fz = hash2(Math.floor(u * 512), Math.floor(v * 512), seed);
    const wr = wrinkles(u, v, seed);
    const rib = knit ? Math.sin((x + Math.sin(y * 3) * 0.2) * Math.PI * 2) : over;
    setc(o, [1, 1, 1], 0.86 + 0.035 * rib + 0.1 * (g - 0.5) + 0.03 * (fz - 0.5) + 0.06 * wr);
    o.h = 0.12 * rib + 1.0 * wr + 0.05 * (fz - 0.5);
    o.ro = 0.88 + 0.06 * (fz - 0.5) - 0.06 * wr; o.ao = 0.93 + 0.05 * rib - 0.0 + 0.07 * (wr - 0.3);
  };
}
/** heavy woven cotton (a martial arts jacket): a coarse basket weave, quilted rows, deep soft creases */
function giCloth({ seed = 17 }) {
  return (u, v, o) => {
    const N = 96, x = u * N, y = v * N;
    const bx = Math.floor(x / 2), by = Math.floor(y / 2), over = ((bx + by) & 1) ? Math.sin(x * Math.PI) : Math.sin(y * Math.PI);
    const quilt = 0.25 * Math.pow(Math.abs(Math.sin(v * 12 * Math.PI)), 12);
    const g = fbm(u, v, 10, 3, seed), fz = hash2(Math.floor(u * 512), Math.floor(v * 512), seed), wr = wrinkles(u, v, seed);
    setc(o, [1, 1, 1], 0.86 + 0.05 * Math.abs(over) + 0.07 * (g - 0.5) + 0.03 * (fz - 0.5) + 0.08 * wr - 0.06 * quilt);
    o.h = 0.25 * Math.abs(over) - 0.25 * quilt + 1.2 * wr + 0.05 * (fz - 0.5);
    o.ro = 0.9 + 0.05 * (fz - 0.5) - 0.05 * wr; o.ao = 0.9 + 0.06 * Math.abs(over) + 0.08 * (wr - 0.3) - 0.08 * quilt;
  };
}
function denim({ seed = 12 }) {
  return (u, v, o) => {
    const N = 110, d = (u + v) * N, t = Math.sin(d * Math.PI * 2);
    const g = fbm(u, v, 12, 4, seed), wear = smooth(0.55, 0.8, fbm(u, v, 3, 4, seed + 3));
    const fz = hash2(Math.floor(u * 512), Math.floor(v * 512), seed), wr = wrinkles(u, v, seed);
    setc(o, [1, 1, 1], 0.74 + 0.05 * t + 0.22 * g + 0.04 * (fz - 0.5) + wear * 0.16 + 0.12 * wr);
    o.h = t * 0.12 + 1.0 * wr + (fz - 0.5) * 0.05; o.ro = 0.88 - wear * 0.08; o.ao = 0.95 + 0.04 * t + 0.05 * (wr - 0.3);
  };
}
function leather({ seed = 13 }) {
  return (u, v, o) => {
    const [f1, f2] = cells(u, v, 72, seed, 1), g = fbm(u, v, 5, 4, seed + 2), scr = smooth(0.82, 0.95, fbm(u * 30, v * 2, 30, 2, seed + 4));
    const crease = 1 - smooth(0, 0.1, f2 - f1), wr = wrinkles(u, v, seed);
    setc(o, [1, 1, 1], 0.66 + 0.24 * g + 0.04 * (1 - crease) - crease * 0.08 + scr * 0.2 + 0.1 * wr);
    o.h = (1 - crease) * 0.15 - scr * 0.05 + 0.9 * wr; o.ro = clamp(0.42 + crease * 0.12 + (1 - g) * 0.15 - 0.12 * wr, 0.2, 1); o.ao = 1 - crease * 0.15;
  };
}
function skinTex({ seed = 14 }) {
  return (u, v, o) => {
    const [f1, f2] = cells(u, v, 90, seed, 1), g = fbm(u, v, 6, 4, seed + 2), fz = hash2(Math.floor(u * 512), Math.floor(v * 512), seed);
    const pore = smooth(0.16, 0.0, f1) * 0.7;
    const blotch = fbm(u, v, 9, 4, seed + 5);
    setc(o, [1, 1, 1], 0.9 + 0.12 * (blotch - 0.5) + 0.04 * (fz - 0.5) - pore * 0.1);
    o.r *= 1 + 0.04 * (blotch - 0.5); o.b *= 1 - 0.05 * (blotch - 0.5);
    o.h = -pore + 0.2 * g + 0.15 * (fz - 0.5); o.ro = clamp(0.58 + 0.15 * pore + 0.15 * (g - 0.5), 0.3, 0.9); o.ao = 1 - pore * 0.25;
  };
}
function hairTex({ seed = 15 }) {
  return (u, v, o) => {
    const s = fbm(u * 40, v * 1.2, 40, 4, seed), s2 = hash2(Math.floor(u * 256), 0, seed);
    setc(o, [1, 1, 1], 0.45 + 0.7 * s + 0.2 * (s2 - 0.5));
    o.h = s * 1.0; o.ro = 0.45 + 0.3 * s; o.ao = 0.7 + 0.3 * s;
  };
}
function metalTex({ seed = 16, rust = 0.1 }) {
  return (u, v, o) => {
    const g = fbm(u, v, 6, 5, seed), scr = smooth(0.7, 0.95, fbm(u * 50, v * 2, 50, 3, seed + 3)), rm = smooth(1 - rust, 1 - rust + 0.2, fbm(u, v, 4, 5, seed + 5));
    setc(o, [1, 1, 1], 0.7 + 0.3 * g + scr * 0.25);
    mixc(o, [0.7, 0.35, 0.15], rm * 0.7);
    o.h = g * 0.2 + scr * 0.1; o.ro = clamp(0.35 + (1 - scr) * 0.15 + rm * 0.5, 0.1, 1); o.me = 0.9 * (1 - rm);
  };
}

// ── a face (per character, 256 square): front-projected onto the head ─────
export function face(spec) {
  const N = spec.size ?? 512;
  const skin = spec.skin, eyes = spec.eyes ?? [0.25, 0.16, 0.08], brow = spec.brow ?? [0.12, 0.08, 0.05], lip = spec.lip ?? [skin[0] * 0.75, skin[1] * 0.5, skin[2] * 0.5];
  const t = bake(N, (u, v, o) => {
    // head front: x spans the texture, y 0..1 down; the face is centred at u 0.5, eyes at v 0.46
    const x = (u - 0.5) * 2, y = (v - 0.5) * 2; // -1..1
    const sk = fbm(u, v, 5, 4, 20 + (spec.seed ?? 0));
    setc(o, skin, 0.92 + 0.12 * (sk - 0.5));
    o.ro = 0.55; o.h = 0.0; o.ao = 1;
    const edge = smooth(0.7, 1.0, Math.hypot(x * 0.9, y * 0.8));
    mulc(o, 1 - edge * 0.12);
    const ellipse = (cx, cy, rx, ry) => Math.hypot((x - cx) / rx, (y - cy) / ry);
    // brow ridge shadow and brows
    for (const sd of [-1, 1]) {
      const bx = sd * 0.3, by = -0.2;
      const b = smooth(1.0, 0.7, ellipse(bx, by + 0.02 * sd * 0, 0.26, 0.055)) * (1 - 0.4 * (spec.thinBrow ? 1 : 0));
      mixc(o, brow, b * 0.95); o.h += b * 0.5; o.ro = lerp(o.ro, 0.8, b);
      // eye socket shadow
      mulc(o, 1 - smooth(1.0, 0.0, ellipse(bx, -0.07, 0.25, 0.13)) * 0.2);
      // eyeball
      const e = ellipse(bx, -0.06, 0.17, 0.075);
      if (e < 1) {
        const lid = smooth(0.7, 1.0, e);
        const iris = ellipse(bx + sd * -0.01, -0.06, 0.075, 0.075);
        setc(o, [0.92, 0.9, 0.88]); o.ro = 0.15; o.h = -0.3;
        if (iris < 1) { setc(o, eyes, 0.7 + 0.5 * (1 - iris)); mulc(o, 0.55 + 0.45 * iris); if (iris < 0.45) setc(o, [0.01, 0.01, 0.015]); }
        if (ellipse(bx - 0.03, -0.085, 0.022, 0.022) < 1) setc(o, [1, 1, 1]);
        mixc(o, [skin[0] * 0.4, skin[1] * 0.3, skin[2] * 0.3], lid * 0.9);
        const upper = smooth(0.0, -0.06, y - (-0.06 - 0.045));
        mulc(o, 1 - (y < -0.085 ? 0.45 * smooth(0, 1, 1 - e) : 0));
        void upper;
      }
      // cheek tint
      mixc(o, [skin[0] * 1.0 + 0.1, skin[1] * 0.8, skin[2] * 0.78], smooth(1, 0, ellipse(sd * 0.42, 0.28, 0.2, 0.14)) * 0.22);
    }
    // nose: bridge highlight, shadow under, nostrils
    const nose = smooth(1, 0.2, ellipse(0, 0.1, 0.075, 0.28));
    o.h += nose * 0.6; mulc(o, 1 + nose * 0.05);
    mulc(o, 1 - smooth(1, 0, ellipse(0, 0.33, 0.2, 0.06)) * 0.28);
    for (const sd of [-1, 1]) { if (ellipse(sd * 0.075, 0.33, 0.04, 0.025) < 1) { mulc(o, 0.4); o.h -= 0.3; } }
    // mouth: lips and the line between
    const mouth = ellipse(0, 0.55, 0.22, 0.07);
    if (mouth < 1) {
      const up = y < 0.55;
      mixc(o, lip, 0.85 * (1 - mouth * mouth)); o.ro = 0.4; o.h += 0.3 * (1 - mouth);
      if (Math.abs(y - 0.55 - (spec.frown ? 0.02 * Math.abs(x) : -0.01 * (1 - Math.abs(x) * 3))) < 0.012) { mulc(o, 0.45); o.h -= 0.5; }
      void up;
    }
    mulc(o, 1 - smooth(1, 0, ellipse(0, 0.68, 0.17, 0.05)) * 0.12);
    // stubble or scar
    if (spec.stubble) { const s = hash2(Math.floor(u * 256), Math.floor(v * 256), 3) > 0.45 ? 1 : 0; const zone = smooth(0.3, 0.55, y) * smooth(1.0, 0.55, Math.hypot(x, y - 0.1)); mixc(o, [0.1, 0.08, 0.06], s * zone * 0.35); }
    if (spec.scar && Math.abs((x - 0.38) * 0.7 + (y + 0.1) * 0.7 * 0.5) < 0.012 && y > -0.35 && y < 0.15) { mixc(o, [skin[0] * 1.1, skin[1] * 0.6, skin[2] * 0.6], 0.8); o.h += 0.6; }
    if (spec.war) { const s = Math.abs(y - 0.22) < 0.035 && Math.abs(x) > 0.15 && Math.abs(x) < 0.7; if (s) mixc(o, spec.war, 0.85); }
    // make the border blend to a flat skin colour so a clamped lookup is clean
    mixc(o, skin, smooth(0.85, 1.0, Math.max(Math.abs(x), Math.abs(y))));
  });
  return t;
}

/** the skin of a sculpted head (facemesh.mjs), painted in the head's own mapping (u around the head, v from the crown
 * down; spec.frame(u, v) gives the unit-head point each texel lands on, so features line up with the geometry): skin
 * tone with blotches, warmer cheeks, nose, ears and lips, cooler eye sockets, a beard shadow, pores and fine lines in the
 * height (the normal map), lips in their own colour and gloss, a dense brow underlay, a painted hairline under the hair,
 * stubble, age lines, scars, war paint and makeup. */
export function faceSkin(spec) {
  const W = spec.w ?? 2048, H = spec.h ?? 1024, skin = spec.skin, seed = spec.seed ?? 1, L = spec.L;
  const lipCol = spec.lip ?? [skin[0] * 0.82, skin[1] * 0.5, skin[2] * 0.52];
  const arch = spec.browArch ?? 1, bthick = spec.browThick ?? 1;
  const G = (x, w) => Math.exp(-(x * x) / (w * w));
  return bake(W, (u, v, o) => {
    const P = spec.frame(u, v), x = P.x, y = P.y, ax = Math.abs(x), aphi = Math.abs(P.phi), front = Math.cos(P.phi);
    const fm = smooth(-0.1, 0.35, front); // the face's features are on the front only (the back of the head has the same x and y)
    const near = (cx, cy, rx, ry) => fm * G(Math.hypot((ax - cx) / rx, (y - cy) / ry), 1);
    const blot = fbm(u, v * 0.5, 9, 5, seed + 20), fine = fbm(u, v * 0.5, 64, 2, seed + 21), mott = fbm(u, v * 0.5, 30, 3, seed + 22);
    setc(o, skin, 0.95 + 0.1 * (blot - 0.5) + 0.05 * (mott - 0.5));
    // blood: cheeks, nose, ears, chin and lips warmer; under the eyes and the temples cooler; a darker beard zone
    const warm = 0.8 * near(0.042, -0.028, 0.022, 0.02) + 0.55 * near(0, -0.035, 0.012, 0.022) + 0.4 * near(0, -0.105, 0.016, 0.012) + 0.6 * G(aphi - 1.5, 0.25) * G(y + 0.016, 0.03);
    o.r *= 1 + 0.085 * warm; o.g *= 1 - 0.035 * warm; o.b *= 1 - 0.045 * warm;
    const cool = near(L.eyeX, -0.004, 0.022, 0.014) + 0.5 * G(aphi - 1.1, 0.25) * G(y - 0.03, 0.02);
    mulc(o, 1 - 0.07 * cool); o.b *= 1 + 0.03 * cool;
    const beard = smooth(-0.03, -0.055, y) * smooth(-0.135, -0.1, y) * smooth(1.75, 1.2, aphi) * (1 - G(ax / 0.02, 1) * smooth(-0.05, -0.04, y) * 0.5);
    if (spec.stubble) { o.r *= 1 - 0.07 * beard; o.g *= 1 - 0.05 * beard; o.b *= 1 - 0.02 * beard; }
    // pores and fine lines (the height for the normal map), denser on the nose and cheeks
    const [f1] = cells(u, v * 0.5, 720, seed + 3, 1), pore = smooth(0.24, 0.0, f1) * (0.45 + 0.55 * Math.min(1, warm + 0.3));
    const [g1, g2] = cells(u, v * 0.5, 160, seed + 4, 1), cracks = smooth(0.06, 0.0, g2 - g1);
    o.h = -0.5 * pore - 0.18 * cracks + 0.25 * (fine - 0.5);
    o.ro = 0.52 + 0.1 * pore + 0.06 * (blot - 0.5); o.ao = 1 - 0.12 * pore;
    o.ro -= 0.08 * (near(0, 0.04, 0.045, 0.02) + near(0, -0.025, 0.012, 0.03)); // the oilier T zone
    mulc(o, 1 - 0.04 * pore);
    // lips: their own colour, darker toward the line, glossier, with fine vertical lines
    const M = spec.lipMasks(x, y, L), lipm = Math.max(M.up, M.lo) * smooth(0.2, 0.6, front);
    if (lipm > 0) {
      const edge = M.up > M.lo ? 1 - Math.min(1, M.su) : 1 - Math.min(1, M.sl);
      const lc = lipCol.map((c, i) => c * (0.85 + 0.15 * (1 - edge)) * (1 - 0.12 * edge * edge));
      mixc(o, lc, lipm * 0.9);
      o.ro = lerp(o.ro, 0.36, lipm); o.h += lipm * (0.12 * Math.pow(Math.abs(Math.sin(x * 2600 + 2 * fine)), 3) - 0.06);
    }
    // brows: a dense underlay along the brow so skin never shows between the strands
    if (ax > 0.008 && ax < 0.06 && front > 0.3) {
      const bu = Math.min(1, Math.max(0, (ax - 0.0105) / 0.0445)), yc = 0.0175 + 0.0075 * Math.sin(Math.min(1, bu * 1.15) * Math.PI * 0.82) * arch - 0.0035 * bu * bu;
      const band = (0.0058 - 0.0034 * bu) * bthick * 0.5, k = smooth(band * 1.6, band * 0.6, Math.abs(y - yc)) * smooth(0.008, 0.012, ax) * smooth(0.058, 0.052, ax);
      const hairy = 0.5 + 0.5 * Math.pow(Math.abs(Math.sin((x * 900 + y * 1500) + fine * 6)), 2);
      mixc(o, spec.brow, 0.62 * k * hairy); o.ro = lerp(o.ro, 0.7, k);
    }
    // the hairline: the scalp under hair carries the hair's colour (roots between cards and clumps)
    if (spec.hair && spec.hairline) {
      let hl;
      if (spec.hairline === 'mohawk') { const line = 0.062 - 0.07 * smooth(1.0, 1.55, aphi) - 0.02 * smooth(1.9, 2.8, aphi); const scalp = smooth(line - 0.002, line + 0.006, y); const d = hash2(Math.floor(u * W * 0.5), Math.floor(v * H * 0.5), 9) > 0.5 ? 1 : 0.4; hl = scalp * (0.45 + 0.4 * d) * (1 - smooth(0.004, 0.0, ax) * 0); }
      else { const line = 0.066 - 0.012 * G(P.phi / 0.5, 1) * 0 + 0.004 * Math.cos(P.phi * 2) - 0.07 * smooth(1.0, 1.55, aphi) - 0.02 * smooth(1.9, 2.8, aphi); hl = smooth(line - 0.003, line + 0.006, y) * (1 - smooth(1.3, 1.48, aphi) * G(y + 0.015, 0.03)); }
      const strand = 0.55 + 0.45 * fbm(u * 3, v * 0.2, 60, 3, seed + 30);
      if (hl > 0) { mixc(o, spec.hair.map((c) => c * strand), hl * (spec.hairline === 'mohawk' ? 0.85 : 0.95)); o.ro = lerp(o.ro, 0.6, hl); o.h += hl * 0.2 * strand; }
    }
    // stubble: dots of hair over the beard zone (and the scalp on a shaved head)
    if (spec.stubble) {
      const d = hash2(Math.floor(u * W * 0.5), Math.floor(v * H * 0.5), 3), s = d > 0.55 ? 1 : 0, lipClear = 1 - smooth(0.0, 0.3, lipm);
      const k = beard * lipClear * (spec.stubble === true ? 1 : spec.stubble);
      mixc(o, [0.06, 0.045, 0.035], (0.18 + 0.4 * s) * k); o.h += 0.25 * s * k; o.ro = lerp(o.ro, 0.62, k * 0.5);
    }
    const age = spec.age ?? 0;
    if (age > 0) { // forehead lines, crow's feet, lines under the eyes
      const fh = Math.pow(Math.abs(Math.sin((y - 0.034) * 360 + 0.4 * Math.sin(x * 60))), 10) * smooth(0.032, 0.042, y) * smooth(0.08, 0.065, y) * smooth(0.06, 0.03, ax);
      const crow = Math.pow(Math.abs(Math.sin(Math.atan2(y - 0.001, ax - L.eyeX - 0.02) * 7)), 16) * near(L.eyeX + 0.03, 0.001, 0.014, 0.014);
      const under = Math.pow(Math.abs(Math.sin((y + 0.014) * 900)), 12) * near(L.eyeX, -0.015, 0.016, 0.006);
      const k = (fh * fm + crow + 0.6 * under) * age; mulc(o, 1 - 0.12 * k); o.h -= 0.9 * k;
    }
    if (spec.scar) { const sx = x - 0.034, d = Math.abs(sx * 0.92 + (y - 0.0) * 0.4); if (d < 0.0016 && y > -0.035 && y < 0.04 && front > 0.2) { const k = smooth(0.0016, 0.0004, d); mixc(o, [skin[0] * 1.1, skin[1] * 0.64, skin[2] * 0.62], 0.75 * k); o.h += 0.8 * k; o.ro = 0.36; } }
    if (spec.war && y > -0.024 && y < -0.015 && ax > 0.018 && ax < 0.068 && front > 0.1) { mixc(o, spec.war, 0.85); o.ro = 0.38; }
    if (spec.makeup) { // eyeshadow on the upper lids, a soft wing at the outer corner, blush on the cheekbones
      const mk = spec.makeup, lidz = near(L.eyeX, 0.008, 0.019, 0.008) * smooth(-0.002, 0.004, y);
      mixc(o, mk.shadow, 0.6 * lidz);
      mixc(o, [0.05, 0.035, 0.035], 0.55 * near(L.eyeX + 0.02, 0.003, 0.005, 0.0022));
      mixc(o, mk.blush, 0.28 * near(0.045, -0.024, 0.02, 0.013));
    }
  }, H);
}
/** hair cards: strands along v (roots at v 0, tips at 1) with gaps, so the alpha-tested card breaks into strands; the
 * alpha thins toward the tips */
function hairCard(seed = 31) {
  const N = 512, t = bake(N, (u, v, o) => {
    const s = fbm(u * 1, v * 0.06, 90, 3, seed), s2 = hash2(Math.floor(u * 180), 0, seed);
    setc(o, [1, 1, 1], 0.55 + 0.45 * s + 0.25 * (s2 - 0.5));
    mulc(o, 0.75 + 0.25 * smooth(0, 0.25, v));
    o.h = s; o.ro = 0.72 + 0.15 * (1 - s); o.ao = 0.6 + 0.4 * smooth(0, 0.3, v);
  });
  t.alpha = new Float32Array(N * N);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = x / N, v = y / N, col = Math.floor(u * 110), w = hash2(col, 1, seed), len = 0.55 + 0.45 * hash2(col, 2, seed);
    const f = u * 110 - col, strand = smooth(0.5, 0.15, Math.abs(f - 0.5)) * (0.5 + 0.5 * w);
    const clump = 0.6 + 0.4 * fbm(u, 0.5, 14, 2, seed + 3), tip = smooth(len, len - 0.25, v);
    t.alpha[y * N + x] = clamp((strand * 1.7 * clump + 0.35 + 0.3 * smooth(0.3, 0, v)) * tip * smooth(1, 0.94, v));
  }
  return t;
}

// ── decals (RGBA, white or dark marks with alpha) ───────────────────────
function decalTex(name, size, fn) {
  const t = bake(size, (u, v, o) => fn(u, v, o));
  t.alpha = new Float32Array(size * size);
  const o = { r: 0, g: 0, b: 0, a: 0 };
  t.each((u, v, i) => { o.r = o.g = o.b = 0; o.a = 0; fn2(u, v, o); t.a[0][i] = o.r; t.a[1][i] = o.g; t.a[2][i] = o.b; t.alpha[i] = o.a; });
  function fn2(u, v, out) { const tmp = { r: 0, g: 0, b: 0, h: 0, ro: 0, me: 0, ao: 1, a: 0 }; fn(u, v, tmp); out.r = tmp.r; out.g = tmp.g; out.b = tmp.b; out.a = tmp.a; }
  const bytes = new Uint8Array(size * size * 4);
  for (let i = 0; i < size * size; i++) { bytes[i * 4] = Math.round(clamp(t.a[0][i]) * 255); bytes[i * 4 + 1] = Math.round(clamp(t.a[1][i]) * 255); bytes[i * 4 + 2] = Math.round(clamp(t.a[2][i]) * 255); bytes[i * 4 + 3] = Math.round(clamp(t.alpha[i]) * 255); }
  save(`${OUT}/${name}.png`, size, size, 4, bytes);
}
const scuff = (seed) => (u, v, o) => {
  const x = (u - 0.5) * 2, y = (v - 0.5) * 2; let a = 0;
  for (let k = 0; k < 7; k++) {
    const ang = (hash2(k, 1, seed) - 0.5) * 0.5, off = (hash2(k, 2, seed) - 0.5) * 0.9, len = 0.5 + 0.45 * hash2(k, 3, seed);
    const rx = x * Math.cos(ang) + y * Math.sin(ang), ry = -x * Math.sin(ang) + y * Math.cos(ang);
    const along = smooth(len, len * 0.5, Math.abs(rx));
    a = Math.max(a, along * smooth(0.05, 0.0, Math.abs(ry - off)) * (0.5 + 0.5 * hash2(Math.floor(u * 128), k, seed)));
  }
  const c = 0.08 + 0.1 * hash2(Math.floor(u * 64), Math.floor(v * 64), seed);
  o.r = c; o.g = c; o.b = c; o.a = a * 0.9;
};
const crack = (seed) => (u, v, o) => {
  const [c1, c2] = cells(u, v, 5, seed, 1);
  const d = Math.hypot(u - 0.5, v - 0.5) * 2;
  const line = (1 - smooth(0.0, 0.05, c2 - c1)) * smooth(1.0, 0.25, d) * smooth(0.4, 0.5, fbm(u, v, 7, 3, seed));
  o.r = o.g = o.b = 0.05; o.a = line;
};
const blob = (u, v, o) => { const d = Math.hypot(u - 0.5, v - 0.5) * 2; o.r = o.g = o.b = 0; o.a = Math.pow(smooth(1, 0.0, d), 1.6) * 0.85; };
const burst = (u, v, o) => {
  const x = (u - 0.5) * 2, y = (v - 0.5) * 2, d = Math.hypot(x, y), a = Math.atan2(y, x);
  const rays = Math.pow(Math.abs(Math.sin(a * 6.5 + 1.0)), 6) * 0.9 + Math.abs(Math.sin(a * 13)) * 0.2;
  const k = smooth(1.0, 0.1, d) * (0.5 + rays * smooth(0.0, 0.4, d));
  o.r = 0.05; o.g = 0.05; o.b = 0.05; o.a = Math.min(1, k * 0.9) * smooth(1.0, 0.8, d);
};

// ── the set ─────────────────────────────────────────────────────────────
const jobs = {
  // rooftop at dusk
  roof_deck: () => [bake(1024, slabs({ base: [0.5, 0.47, 0.44], n: 3, seed: 61, tint: [1.04, 1, 0.95] })), 1.8],
  roof_tar: () => [bake(512, asphalt({ base: [0.17, 0.165, 0.17], seed: 62, wet: 0.15 })), 2.0],
  concrete: () => [bake(512, plaster({ base: [0.52, 0.5, 0.47], seed: 63, stain: 0.45 })), 2.2],
  facade_dusk: () => [bake(1024, facade({ wall: brick({ seed: 64, rows: 24, nx: 8, soot: 0.45 }), seed: 64, lit: 0.42 })), 2.4],
  facade_office: () => [bake(1024, facade({ wall: plate({ color: [0.32, 0.34, 0.37], seed: 65, rust: 0.05, diamonds: 2, panels: 4 }), seed: 65, lit: 0.5, glass: [0.05, 0.07, 0.1], frame: [0.2, 0.21, 0.23], win: [0.06, 0.08, 0.94, 0.92], lamps: [[0.85, 0.92, 1], [1, 0.9, 0.7], [0.8, 0.9, 1]] })), 2.2],
  facade_tower: () => [bake(1024, facade({ wall: plaster({ base: [0.55, 0.52, 0.48], seed: 66 }), seed: 66, lit: 0.38, win: [0.25, 0.12, 0.75, 0.88] })), 2.4],
  // neon alley
  ground_wet: () => [bake(1024, asphalt({ base: [0.2, 0.2, 0.22], seed: 71, wet: 1.0 })), 2.0],
  wall_brick: () => [bake(512, brick({ seed: 72 })), 2.6],
  wall_brick_dark: () => [bake(512, brick({ cols: [[0.34, 0.2, 0.2], [0.3, 0.18, 0.18], [0.26, 0.17, 0.2]], mortar: [0.3, 0.3, 0.3], seed: 73, soot: 0.6 })), 2.6],
  facade_alley: () => [bake(1024, facade({ wall: brick({ seed: 74, rows: 24, nx: 8, soot: 0.55 }), seed: 74, lit: 0.4, bars: true })), 2.4],
  facade_neon: () => [bake(1024, facade({ wall: neonStrips({ seed: 75 }), seed: 75, lit: 0.7, lamps: [[1, 0.25, 0.7], [0.25, 0.9, 1], [1, 0.7, 0.25], [0.6, 0.4, 1]], glass: [0.06, 0.05, 0.1], frame: [0.1, 0.1, 0.14], win: [0.14, 0.14, 0.86, 0.82] })), 1.6],
  wall_metal: () => [bake(512, corrugated({ color: [0.26, 0.34, 0.4], seed: 76, rust: 0.45 })), 2.4],
  wall_plate: () => [bake(512, plate({ color: [0.28, 0.28, 0.3], seed: 77, rust: 0.3, diamonds: 6, panels: 2 })), 2.0],
  paint_metal: () => [bake(512, metalTex({ seed: 16, rust: 0.2 })), 2.0],
  wood: () => [bake(512, planks({ color: [0.45, 0.3, 0.17], n: 6, seed: 78 })), 2.2],
  // dojo courtyard
  deck: () => [bake(1024, planks({ color: [0.5, 0.36, 0.22], n: 8, seed: 81, knots: true })), 2.0],
  stone: () => [bake(1024, slabs({ base: [0.56, 0.54, 0.5], n: 3, seed: 82, tint: [1, 1, 0.97] })), 1.8],
  plaster_white: () => [bake(512, plaster({ base: [0.86, 0.83, 0.76], seed: 83, stain: 0.25 })), 2.0],
  roof_tiles: () => [bake(512, roofTiles({ seed: 84 })), 2.2],
  shoji: () => [bake(512, shoji({ seed: 85 })), 1.2],
  lacquer: () => [bake(512, metalTex({ seed: 86, rust: 0.0 })), 0.6],
  bark: () => [bake(512, bark({ seed: 87 })), 2.4],
  leaves: () => [bake(512, leaves({ seed: 88 })), 2.0],
  rock: () => [bake(512, rock({ seed: 89 })), 2.6],
  // characters
  fab_cloth: () => [bake(256, fabric({ seed: 11 })), 1.6],
  fab_knit: () => [bake(256, fabric({ seed: 12, knit: true })), 1.6],
  fab_gi: () => [bake(512, giCloth({ seed: 17 })), 1.8],
  fab_denim: () => [bake(256, denim({ seed: 13 })), 1.6],
  fab_leather: () => [bake(256, leather({ seed: 14 })), 1.4],
  fab_skin: () => [bake(256, skinTex({ seed: 15 })), 0.5],
  fab_hair: () => [bake(256, hairTex({ seed: 16 })), 1.6],
  hair_card: () => [hairCard(31), 1.2],
};
for (const [name, f] of Object.entries(jobs)) {
  if (!want(name)) continue;
  const [t, s] = f();
  writeSet(OUT, name, t, s);
  console.log(name, t.w);
}
if (want('decals')) {
  decalTex('decal_scuff1', 256, scuff(1)); decalTex('decal_scuff2', 256, scuff(2)); decalTex('decal_crack', 256, crack(3));
  decalTex('decal_blob', 128, blob); decalTex('decal_burst', 256, burst);
  console.log('decals');
}
