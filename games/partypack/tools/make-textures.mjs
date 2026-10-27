// Procedural PBR texture sets for the 3D scenes: albedo (_c), normal (_n), occlusion-roughness-metal (_orm), emissive (_e).
// Everything tiles. node tools/make-textures.mjs [name ...]   -> assets/textures/
import { Tex, writeSet, fbm, cells, hash2, smooth, clamp, lerp, save } from './texlib.mjs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

export const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'textures');
const MAIN = process.argv[1] && import.meta.url.endsWith(process.argv[1].split('/').pop());
const only = MAIN ? process.argv.slice(2) : ['none'];
const want = (n) => MAIN && (only.length === 0 || only.includes(n));

/** per-pixel material: px(u, v, o) fills o.{r,g,b (sRGB), h, ro, me, ao, er,eg,eb (emissive sRGB)} */
function bake(size, px) {
  const t = new Tex(size);
  const o = { r: 0, g: 0, b: 0, h: 0, ro: 0.8, me: 0, ao: 1, er: 0, eg: 0, eb: 0 };
  let emi = null;
  t.each((u, v, i) => {
    o.r = o.g = o.b = 0; o.h = 0; o.ro = 0.8; o.me = 0; o.ao = 1; o.er = o.eg = o.eb = 0;
    px(u, v, o, i);
    t.a[0][i] = o.r; t.a[1][i] = o.g; t.a[2][i] = o.b; t.height[i] = o.h; t.rough[i] = o.ro; t.metal[i] = o.me; t.ao[i] = o.ao;
    if (o.er > 0 || o.eg > 0 || o.eb > 0) { emi ||= t.emissive(); emi[0][i] = o.er; emi[1][i] = o.eg; emi[2][i] = o.eb; }
  });
  return t;
}
const set = (o, c, k = 1) => { o.r = c[0] * k; o.g = c[1] * k; o.b = c[2] * k; };
const mixc = (o, c, k) => { o.r = lerp(o.r, c[0], k); o.g = lerp(o.g, c[1], k); o.b = lerp(o.b, c[2], k); };
const mul = (o, k) => { o.r *= k; o.g *= k; o.b *= k; };

const mats = {
  // soft woven vinyl for the party characters: tinted by the material factor, so it is near white
  velvet: () => bake(256, (u, v, o) => {
    const w = 64, x = u * w, y = v * w;
    const weave = 0.5 + 0.5 * Math.sin(x * Math.PI * 2) * Math.sin(y * Math.PI * 2);
    const n = fbm(u, v, 16, 3, 5);
    set(o, [0.9, 0.9, 0.92], 0.82 + 0.16 * n + 0.05 * weave);
    o.h = weave * 0.35 + n * 0.6; o.ro = 0.62 + 0.2 * n;
  }),
  // dark steel floor plates, 4 x 4 per tile, with bolts, brushed grain, worn edges and glowing cyan grooves
  steel: () => bake(512, (u, v, o) => {
    const n = 4, fx = (u * n) % 1, fy = (v * n) % 1;
    const id = hash2(Math.floor(u * n), Math.floor(v * n), 3);
    const ed = Math.min(fx, 1 - fx, fy, 1 - fy);
    const groove = 1 - smooth(0.0, 0.02, ed);
    const bevel = smooth(0.0, 0.05, ed);
    const grain = fbm(u * 1.0, v * 40, 8, 3, 2) * 0.5 + fbm(u * 40, v * 1.0, 8, 3, 6) * 0.5;
    const wear = smooth(0.55, 0.8, fbm(u, v, 5, 4, 9));
    const bolt = [[0.09, 0.09], [0.91, 0.09], [0.09, 0.91], [0.91, 0.91]].reduce((m, [bx, by]) => Math.max(m, smooth(0.045, 0.02, Math.hypot(fx - bx, fy - by))), 0);
    const k = 0.55 + 0.12 * id + 0.08 * grain + 0.05 * wear;
    set(o, [0.5, 0.55, 0.64], k);
    o.h = bevel * 0.6 + bolt * 0.5 + grain * 0.05 - groove * 0.2;
    o.ro = 0.4 + 0.22 * wear + 0.12 * grain; o.me = 0.12;
    o.ao = 0.55 + 0.45 * bevel;
    if (groove > 0.05) { o.er = 0.05 * groove; o.eg = 0.55 * groove; o.eb = 0.75 * groove; mixc(o, [0.02, 0.1, 0.12], groove * 0.7); }
  }),
  // basalt with glowing lava veins
  lava: () => bake(512, (u, v, o) => {
    const [f1, f2, id] = cells(u, v, 7, 4, 0.9);
    const vein = 1 - smooth(0.0, 0.2, f2 - f1);
    const flow = fbm(u, v, 6, 4, 8);
    const hot = clamp(vein * 1.2 + smooth(0.62, 0.8, flow) * 0.5);
    const crust = 0.1 + 0.08 * id + 0.07 * fbm(u, v, 24, 3, 5);
    set(o, [0.3, 0.2, 0.18], crust * 2.2);
    o.h = (1 - vein) * 0.8 + fbm(u, v, 20, 3, 2) * 0.3;
    o.ro = 0.9 - hot * 0.5;
    o.er = hot * (1.0 + 0.4 * flow); o.eg = hot * hot * (0.45 + 0.2 * flow); o.eb = hot * hot * hot * 0.08;
    mixc(o, [1, 0.45, 0.1], hot * 0.6);
  }),
  // rough warm planks with grain, knots and gaps (the raft deck, paddles, dock)
  wood: () => bake(512, (u, v, o) => {
    const n = 5, pu = u * n, pid = Math.floor(pu), fx = pu - pid;
    const grain = fbm(fx * 0.5, v * 3 + hash2(pid, 0, 1), 4, 4, 7 + pid % 3);
    const ring = 0.5 + 0.5 * Math.sin((fbm(fx * 0.6, v * 1.2, 4, 3, 3) * 14 + v * 18 * 0) * 6.283);
    const gap = 1 - smooth(0.0, 0.035, Math.min(fx, 1 - fx));
    const tone = hash2(pid, 3, 2);
    const knot = smooth(0.06, 0.02, Math.hypot(fx - 0.5, ((v * 3 + tone * 7) % 1) - 0.5)) * (tone > 0.6 ? 1 : 0);
    set(o, [0.62, 0.42, 0.24], 0.7 + 0.3 * tone + 0.18 * (ring - 0.5) + 0.2 * (grain - 0.5));
    mul(o, 1 - 0.5 * gap - 0.25 * knot);
    o.h = ring * 0.25 + (1 - gap) * 0.6 - knot * 0.2; o.ro = 0.78 - 0.1 * ring; o.ao = 1 - 0.7 * gap;
  }),
  // layered cliff rock
  rock: () => bake(512, (u, v, o) => {
    const [f1, f2, id] = cells(u, v, 6, 21, 1);
    const crack = 1 - smooth(0.0, 0.07, f2 - f1);
    const strata = 0.5 + 0.5 * Math.sin((v * 30 + fbm(u, v, 5, 3, 4) * 6) * 6.283 / 3);
    const g = 0.5 + 0.2 * id + 0.18 * fbm(u, v, 14, 4, 6);
    set(o, [0.5 * g + 0.15, 0.43 * g + 0.14, 0.36 * g + 0.12], 1.0 - 0.12 * strata);
    mul(o, 1 - 0.5 * crack);
    o.h = (1 - crack) * 0.7 + id * 0.25 + strata * 0.1 + fbm(u, v, 30, 3, 8) * 0.2; o.ro = 0.9; o.ao = 1 - 0.5 * crack;
  }),
  // wet sand with ripples (the lagoon floor)
  sand: () => bake(512, (u, v, o) => {
    const rip = 0.5 + 0.5 * Math.sin((u * 10 + fbm(u, v, 5, 3, 3) * 3 + v * 4) * 6.283);
    const g = fbm(u, v, 12, 4, 5), grain = hash2(u * 512, v * 512, 4);
    set(o, [0.78, 0.68, 0.46], 0.75 + 0.25 * g + 0.06 * (grain - 0.5));
    mul(o, 0.9 + 0.1 * rip);
    o.h = rip * 0.6 + g * 0.4; o.ro = 0.9;
  }),
  // grass with clumps and mown stripes
  grass: () => bake(512, (u, v, o) => {
    const g = fbm(u, v, 20, 4, 3), blade = hash2(u * 512, v * 512, 8);
    const stripe = (Math.floor(v * 8) & 1) ? 1 : 0.88;
    set(o, [0.22 + 0.1 * g, 0.5 + 0.18 * g, 0.18], (0.8 + 0.2 * blade) * stripe);
    o.h = g * 0.5 + blade * 0.4; o.ro = 0.9;
  }),
  // racing asphalt with a centre seam, aggregate and tyre marks
  asphalt: () => bake(512, (u, v, o) => {
    const g = fbm(u, v, 6, 5, 2), grain = hash2(u * 512, v * 512, 3), grain2 = hash2((u * 256) | 0, (v * 256) | 0, 9);
    const [f1, f2] = cells(u, v, 60, 5, 1);
    const peb = smooth(0.35, 0.1, f1) * 0.5;
    const marks = smooth(0.4, 0.7, fbm(u * 0.2, v, 2, 4, 11)) * smooth(0.3, 0.55, fbm(u * 0.15 + 0.5, v * 3, 3, 3, 12));
    set(o, [0.34, 0.34, 0.36], 0.7 + 0.35 * g + 0.2 * (grain - 0.5) + 0.2 * peb);
    mul(o, 1 - marks * 0.25);
    o.h = grain2 * 0.4 + peb * 0.6 + g * 0.3; o.ro = 0.78 - marks * 0.25; o.ao = 0.85 + 0.15 * g;
  }),
  // red and white rumble kerb, striped across the length
  kerb: () => bake(256, (u, v, o) => {
    const s = (Math.floor(u * 4) & 1) ? 1 : 0, edge = smooth(0, 0.04, Math.min((u * 4) % 1, 1 - ((u * 4) % 1)));
    const g = fbm(u, v, 10, 4, 5), grit = hash2(u * 256, v * 256, 5);
    set(o, s ? [0.86, 0.12, 0.1] : [0.92, 0.92, 0.9], 0.8 + 0.2 * g + 0.08 * (grit - 0.5));
    o.h = edge * 0.6 + g * 0.3; o.ro = 0.55; o.ao = 0.7 + 0.3 * edge;
  }),
  // crowd stand: rows of seats in muted colours
  stand: () => bake(512, (u, v, o) => {
    const rx = (u * 24) % 1, ry = (v * 8) % 1, ix = Math.floor(u * 24), iy = Math.floor(v * 8);
    const seat = smooth(0.0, 0.1, Math.min(rx, 1 - rx)) * smooth(0.0, 0.1, Math.min(ry, 1 - ry));
    const hue = hash2(ix, iy, 6), person = smooth(0.4, 0.3, Math.hypot(rx - 0.5, ry - 0.62)) * (hash2(ix, iy, 8) > 0.35 ? 1 : 0);
    const cols = [[0.85, 0.25, 0.25], [0.25, 0.5, 0.9], [0.95, 0.8, 0.25], [0.35, 0.8, 0.5], [0.9, 0.9, 0.95], [0.7, 0.4, 0.85]];
    set(o, [0.22, 0.22, 0.3], 1);
    mixc(o, cols[Math.floor(hue * 6)], person * 0.9);
    mul(o, 0.35 + 0.65 * seat);
    o.h = seat * 0.5 + person * 0.3; o.ro = 0.8; o.ao = 0.5 + 0.5 * seat;
  }),
  // board tile: glossy plastic game-board squares with a rounded bevel
  boardtile: () => bake(256, (u, v, o) => {
    const fx = u, fy = v, ed = Math.min(fx, 1 - fx, fy, 1 - fy), bev = smooth(0.0, 0.07, ed);
    const g = fbm(u, v, 6, 3, 4);
    set(o, [0.95, 0.95, 0.98], 0.88 + 0.08 * g);
    mul(o, 0.55 + 0.45 * bev);
    o.h = bev * 0.8 + g * 0.05; o.ro = 0.22 + 0.1 * g; o.ao = 0.6 + 0.4 * bev;
  }),
  // carpeted table cloth for the hub: dark teal felt with a diamond weave
  felt: () => bake(512, (u, v, o) => {
    const d = Math.abs(((u + v) * 16) % 1 - 0.5) + Math.abs(((u - v) * 16) % 1 - 0.5);
    const n = fbm(u, v, 40, 3, 9), fine = hash2(u * 512, v * 512, 2);
    set(o, [0.12, 0.42, 0.42], 0.75 + 0.2 * n + 0.1 * (fine - 0.5));
    mul(o, 0.92 + 0.08 * smooth(0.5, 0.8, d));
    o.h = n * 0.4 + fine * 0.3 + smooth(0.5, 0.8, d) * 0.2; o.ro = 0.95;
  }),
  // brushed metal trim for pillars and frames
  metal: () => bake(512, (u, v, o) => {
    const g = fbm(u * 0.5, v * 40, 4, 3, 3), wear = fbm(u, v, 6, 4, 8);
    set(o, [0.62, 0.65, 0.72], 0.7 + 0.3 * g);
    mul(o, 1 - 0.25 * smooth(0.6, 0.85, wear));
    o.h = g * 0.2; o.ro = 0.3 + 0.3 * smooth(0.55, 0.8, wear); o.me = 1.0;
  }),
};

if (MAIN) for (const [name, fn] of Object.entries(mats)) if (want(name)) { const t = fn(); writeSet(OUT, name, t, name === 'rock' || name === 'steel' || name === 'wood' || name === 'lava' ? 3 : 1.6); console.log('texture', name); }

// a tileable ripple normal map for Water3D (art/water_n.png): broad swell cells over fine chop
if (MAIN && want('water')) {
  const t = bake(512, (u, v, o) => { o.h = 0.7 * fbm(u, v, 6, 4, 11) + 0.5 * (1 - Math.abs(2 * fbm(u * 1, v * 1, 14, 3, 5) - 1)) + 0.25 * fbm(u, v, 40, 2, 3); });
  const n = t.normals(5);
  save(join(OUT, '..', 'art', 'water_n.png'), 512, 512, 3, n);
  console.log('texture water');
}
