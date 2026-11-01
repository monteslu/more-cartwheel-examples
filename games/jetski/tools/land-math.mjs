// The small sums the course tools share: smoothing and noise, all pure functions of their arguments.
export const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
export function hash(x, y, s) { let h = (x * 374761393 + y * 668265263 + s * 1274126177) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }
function vnoise(x, y, s) {
  const xi = Math.floor(x), yi = Math.floor(y), u = x - xi, v = y - yi, su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v);
  return lerp(lerp(hash(xi, yi, s), hash(xi + 1, yi, s), su), lerp(hash(xi, yi + 1, s), hash(xi + 1, yi + 1, s), su), sv);
}
export const fbm = (x, y, oct, s) => { let a = 0.5, f = 1, sum = 0, n = 0; for (let i = 0; i < oct; i++) { sum += a * vnoise(x * f, y * f, s + i); n += a; a *= 0.5; f *= 2.03; } return sum / n; };
export const ridged = (x, y, oct, s) => { let a = 0.5, f = 1, sum = 0, n = 0; for (let i = 0; i < oct; i++) { sum += a * (1 - Math.abs(2 * vnoise(x * f, y * f, s + i) - 1)); n += a; a *= 0.5; f *= 2.1; } return sum / n; };
