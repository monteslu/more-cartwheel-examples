// Arthur's pompadour as ONE combed surface: a grid running front to back (u, the comb lines) and ear to ear (s), its height
// over the skull a function of where it is, its hairline cut in the texture's alpha so the hair ends in strands. Shared by
// make-characters.mjs (the mesh) and make-textures.mjs (pomp_shell, the strands, whose alpha needs the same hairline).
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const AM = 1.45, B0 = 0.3, B1 = Math.PI + 0.8; // how far toward the ears the grid reaches; from just over the brow, over the top, to the nape
/** the direction from the head's centre of grid point s (-1 .. 1), u (0 .. 1) */
export function pompDir(s, u) {
  const a = s * AM, b = B0 + (B1 - B0) * u, c = Math.cos(a);
  return [Math.sin(a), c * Math.sin(b), c * Math.cos(b)];
}
/** the head's loft coordinates of a direction: height t and ring angle a (+z is PI/2) */
export const pompTA = (d) => [d[1], Math.atan2(d[2], d[0])];
/** the hairline: the ring height t it sits at, by ring angle: the forehead, the temples, over the ears, down to the nape.
 * The same curve the face texture paints its fading band of strands under (make-characters.mjs pompEdge) on the front and sides */
export function pompEdge(a) {
  let df = Math.abs(a - Math.PI / 2); if (df > Math.PI) df = 2 * Math.PI - df;
  return 0.04 + 0.48 * smooth(1.35, 0.8, df) - 0.5 * smooth(1.9, 2.7, df); // the forehead is bare up to about the middle of its height
}
const PROFILE = [[0.3, 0], [0.5, 0.03], [0.68, 0.066], [0.95, 0.08], [1.3, 0.066], [1.7, 0.042], [2.25, 0.021], [2.9, 0.011], [3.95, 0.008]];
/** the hair's height over the skull in metres (a 1.0 head): tall at the front, leaning out past the brow, lying back over the
 * crown, flat and slick on the sides, in soft combed furrows along the comb lines */
export function pompHeight(s, u) {
  const b = B0 + (B1 - B0) * u; let i = 0;
  while (i < PROFILE.length - 2 && b > PROFILE[i + 1][0]) i++;
  const [b0, h0] = PROFILE[i], [b1, h1] = PROFILE[i + 1], k = Math.min(1, Math.max(0, (b - b0) / (b1 - b0))), f = k * k * (3 - 2 * k), H = h0 + (h1 - h0) * f;
  const a = Math.abs(s * AM), w = smooth(1.12, 0.45, a), furrow = 1 + 0.045 * Math.sin(s * 19 + u * 2.5) + 0.02 * Math.sin(s * 47 - u * 4);
  return (0.007 + Math.max(0, H - 0.008) * w) * furrow;
}
