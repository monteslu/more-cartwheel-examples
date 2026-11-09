// The "aurora" shading part on a band around the night sky (skyring.c):
// curtains that ripple along the horizon (value noise over azimuth and time),
// a sharp lower edge that wanders, light fading upward, fine vertical rays,
// green at the base turning violet at the top. Additive.
//   ext.auroraParams  vec4: time, strength, (unused), (unused)
export const name = 'aurora';
export async function apply({ TSL, THREE, mat, props, sentinel }) {
  const { Fn, uniform, sharedUniformGroup, uv, vec2, vec3, vec4, float, floor, fract, sin, dot, mix, smoothstep, exp, max, clamp, materialColor, materialOpacity } = TSL;
  const frame = sharedUniformGroup('skyFrame', 0, THREE.NodeUpdateType.RENDER);
  const s = [sentinel(), sentinel(), sentinel(), sentinel()];
  const prm = uniform(new THREE.Vector4(...s)).setName('auroraParams');
  prm.setGroup(frame);
  props['ext.auroraParams'] = { kind: 'vec4', value: s };
  const hash2 = Fn(([p]) => fract(sin(dot(p, vec2(127.1, 311.7))).mul(43758.5453)))
    .setLayout({ name: 'auroraHash', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const vnoise = Fn(([p]) => {
    const i = floor(p), f = fract(p), w = f.mul(f).mul(float(3).sub(f.mul(2)));
    return mix(mix(hash2(i), hash2(i.add(vec2(1, 0))), w.x), mix(hash2(i.add(vec2(0, 1))), hash2(i.add(vec2(1, 1))), w.x), w.y);
  }).setLayout({ name: 'auroraNoise', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const t = prm.x;
  const u = uv().x, v = float(1).sub(uv().y); // u: azimuth 0..1 (wraps); the band's row 0 is its top, so v: 0 at the bottom, 1 at the top
  // wrap the azimuth noise: sample at u and u-1 and blend across the seam
  const n = (freq, speed, off) => {
    const a = vnoise(vec2(u.mul(freq).add(t.mul(speed)), off)), b = vnoise(vec2(u.sub(1).mul(freq).add(t.mul(speed)), off));
    return mix(a, b, smoothstep(0.85, 1.0, u));
  };
  const band = n(5, 0.03, 2.1).mul(0.6).add(n(13, 0.07, 7.7).mul(0.4)); // where curtains hang
  // a curtain is a folded sheet, not a row of bars: its lower edge rises and falls along the horizon (the folds), the
  // rays inside lean and bend with height (the sheet's sway), and the bright hem runs along that lower edge
  // the sheet's foot follows a long slow S across the sky that drifts sideways (a few degrees in 3 s), with smaller folds on it
  const nAt = (uu, freq, speed, off) => mix(vnoise(vec2(uu.mul(freq).add(t.mul(speed)), off)), vnoise(vec2(uu.sub(1).mul(freq).add(t.mul(speed)), off)), smoothstep(0.85, 1.0, uu));
  const foldAt = (du) => nAt(u.add(du), 2.2, 0.012, 4.4).mul(0.6).add(nAt(u.add(du), 9, 0.04, 9.1).mul(0.4));
  const fold = foldAt(0);
  const base = float(0.03).add(fold.mul(0.34));                            // the lower edge wanders: folds up and down
  // where the sheet turns toward the eye it is seen edge-on: a brighter knot running up the curtain at each fold
  const turn = foldAt(0.004).sub(foldAt(-0.004)).abs().mul(60);
  const knot = smoothstep(0.15, 0.9, turn).mul(0.9);
  const h = v.sub(base);
  const lower = smoothstep(0, 0.02, h);
  const falloff = exp(h.mul(-4.5)).mul(float(1).sub(smoothstep(0.55, 0.95, v))); // gone well before the band's top
  const hem = exp(h.mul(-28)).mul(0.9);                                     // the bright fold line at the foot
  const warp = n(4, 0.02, 1.7).sub(0.5).mul(0.05).add(n(11, 0.04, 6.2).sub(0.5).mul(0.015));
  const uw = u.add(warp.mul(float(1).add(h.mul(3))));                       // rays lean more the higher they climb
  // rays: soft streaks of uneven width and brightness drifting along the sheet (noise, not a regular comb of bars)
  const ray1 = vnoise(vec2(uw.mul(240).add(t.mul(0.35)), t.mul(0.08)));
  const ray2 = vnoise(vec2(uw.mul(620).sub(t.mul(0.5)), float(5.3).add(t.mul(0.1))));
  const rays = float(0.45).add(ray1.mul(ray1).mul(0.7)).add(ray2.mul(0.15)).add(hem).add(knot);
  const flicker = float(0.85).add(sin(t.mul(1.7).add(u.mul(30))).mul(0.15));
  // curtains hang in a few places, dark sky between; one always hangs ahead, down the course (u 0), where the racer looks
  const du = u.sub(float(1).mul(u.greaterThan(0.5))).abs(); // azimuth distance from straight ahead, 0..0.5
  const ahead = float(1).sub(smoothstep(0.03, 0.12, du)).mul(float(0.5).add(n(2, 0.02, 1.3).mul(0.5)));
  const slow = float(0.35).add(n(2.5, 0.015, 3.9).mul(0.9)); // brightness breathes along the horizon
  const mask = max(smoothstep(0.42, 0.62, band), ahead).mul(slow);
  const strength = mask.mul(lower).mul(falloff).mul(rays).mul(flicker).mul(prm.y);
  const green = vec3(0.05, 0.9, 0.3), violet = vec3(0.6, 0.12, 0.85);
  const col = mix(green, violet, smoothstep(0.08, 0.45, h));
  // stars: a sparse hash over a fine grid, twinkling, dimmer toward the horizon's haze
  const grid = vec2(u.mul(1440), v.mul(560)); // cells about a quarter degree each way
  const cell = floor(grid);
  const sh = hash2(cell), sj = hash2(cell.add(vec2(3.3, 7.1)));
  const dcen = fract(grid).sub(vec2(float(0.3).add(sj.mul(0.4)), float(0.3).add(sh.mul(0.4))));
  const dot_ = smoothstep(0.4, 0.08, dcen.length());
  const star = smoothstep(0.982, 0.99, sh).mul(dot_).mul(float(0.6).add(sin(t.mul(2.5).add(sj.mul(40))).mul(0.4))).mul(smoothstep(0, 0.15, v));
  const starCol = mix(vec3(0.9, 0.95, 1.1), vec3(1.1, 0.9, 0.7), sj);
  // (times the material's colour and opacity: the program keeps the state's uniforms)
  const rgb = col.mul(strength).mul(3.2).add(starCol.mul(star).mul(7.0));
  mat.colorNode = vec4(rgb.mul(materialColor), 1);
  mat.opacityNode = clamp(strength.mul(3).add(star), 0, 1).mul(materialOpacity);
}
