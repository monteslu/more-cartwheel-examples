// The "feather" shading part on Tux's skinned plumage (tux_skin.c): fine
// feather striations running head to tail (a normal perturbation from a
// stretched noise over the body's local coordinates), only on the dark
// plumage (the vertex colour decides), and a cool rim light so his
// silhouette reads against the snow.
export const name = 'feather';
export async function apply({ TSL, mat, kind }) {
  if (kind === 'depth') return;
  const { Fn, vec2, vec3, vec4, float, floor, fract, sin, dot, mix, smoothstep, normalize, max, positionLocal, normalView, positionViewDirection, vertexColor, cameraViewMatrix, materialEmissive } = TSL;
  const hash2 = Fn(([p]) => fract(sin(dot(p, vec2(127.1, 311.7))).mul(43758.5453)))
    .setLayout({ name: 'featherHash', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const vnoise = Fn(([p]) => {
    const i = floor(p), f = fract(p), w = f.mul(f).mul(float(3).sub(f.mul(2)));
    return mix(mix(hash2(i), hash2(i.add(vec2(1, 0))), w.x), mix(hash2(i.add(vec2(0, 1))), hash2(i.add(vec2(1, 1))), w.x), w.y);
  }).setLayout({ name: 'featherNoise', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const vc = vertexColor();
  const dark = float(1).sub(smoothstep(0.2, 0.5, vc.r)); // the black plumage, not the white front
  // striations: fine across the body (x, z), long along it (y)
  const q = vec2(positionLocal.x.mul(90).add(positionLocal.z.mul(70)), positionLocal.y.mul(9));
  const h = float(0.015);
  const n0 = vnoise(q), nx = vnoise(q.add(vec2(h.mul(90), 0))).sub(n0), ny = vnoise(q.add(vec2(0, h.mul(9)))).sub(n0);
  const tiltW = vec3(nx.mul(0.22), ny.mul(0.08), nx.mul(0.17)).mul(dark);
  const tiltV = cameraViewMatrix.mul(vec4(tiltW, 0)).xyz;
  const nV = normalize(normalView.add(tiltV));
  mat.normalNode = nV;
  const rim = smoothstep(0.55, 1.0, float(1).sub(max(dot(nV, positionViewDirection), 0)));
  mat.emissiveNode = materialEmissive.add(vec3(0.06, 0.08, 0.12).mul(rim).mul(dark));
}
