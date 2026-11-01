// How much of a rock wall's brightness lies in big shapes and how much in fine grain, in one picture. A wall of one even grain keeps its contrast at the small scale and has
// next to none at the large. Rock pixels only: brown or grey and dim, no sky, no green, no sea.
//   scaleShares(image, x, y, w, h) gives { rock, fine, big, ratio }: log brightness, deviation under 5 px (fine) and between 24 and 96 px (big), and big over fine
export function scaleShares(im, X, Y, W, H) {
  const L = new Float32Array(W * H), R = new Uint8Array(W * H);
  let rock = 0;
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
    const s = 4 * ((Y + j) * im.w + X + i), r = im.rgba[s], g = im.rgba[s + 1], b = im.rgba[s + 2], k = j * W + i;
    if (r < 215 && r >= g * 0.92 && g >= b * 0.85 && b < 170) { R[k] = 1; rock++; }
    L[k] = Math.log(4 + 0.3 * r + 0.59 * g + 0.11 * b);
  }
  // box means over rock pixels, from a summed-area table
  const sat = (f) => { const T = new Float64Array((W + 1) * (H + 1)); for (let j = 0; j < H; j++) { let row = 0; for (let i = 0; i < W; i++) { row += f(j * W + i); T[(j + 1) * (W + 1) + i + 1] = T[j * (W + 1) + i + 1] + row; } } return T; };
  const SL = sat((k) => (R[k] ? L[k] : 0)), SN = sat((k) => R[k]);
  const box = (T, i, j, r) => { const x0 = Math.max(0, i - r), x1 = Math.min(W, i + r + 1), y0 = Math.max(0, j - r), y1 = Math.min(H, j + r + 1); return T[y1 * (W + 1) + x1] - T[y0 * (W + 1) + x1] - T[y1 * (W + 1) + x0] + T[y0 * (W + 1) + x0]; };
  const mean = (i, j, r) => box(SL, i, j, r) / Math.max(1, box(SN, i, j, r));
  let fine = 0, big = 0, n = 0;
  for (let j = 0; j < H; j += 2) for (let i = 0; i < W; i += 2) {
    const k = j * W + i;
    if (!R[k]) continue;
    const a = mean(i, j, 2), b = mean(i, j, 12), c = mean(i, j, 48);
    fine += (L[k] - a) ** 2; big += (b - c) ** 2; n++;
  }
  fine = Math.sqrt(fine / Math.max(1, n)), big = Math.sqrt(big / Math.max(1, n));
  return { rock: rock / (W * H), fine, big, ratio: big / Math.max(fine, 1e-6) };
}
