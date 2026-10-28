// The "clouds" shading part on the cloud deck (skyring.c): a procedural
// cumulus layer in the fragment, four octaves of value noise over the world
// xz drifting with the wind, with a cheap lighting trick (density sampled a
// little toward the sun decides the lit and shadowed sides), soft edges, and
// a fade toward the far edge of the deck.
//   ext.cloudParams  vec4: time, wind x, wind z, coverage (0..1)
//   ext.cloudSun     vec4: direction to the sun (world) xyz, w: sun brightness
//   ext.cloudCam     vec4: camera xyz, w: the fade distance
export const name = 'clouds';
export async function apply({ TSL, THREE, mat, props, sentinel }) {
  const { Fn, uniform, sharedUniformGroup, positionWorld, vec2, vec3, vec4, float, floor, fract, sin, dot, mix, smoothstep, length, max, clamp, pow, materialColor, materialOpacity } = TSL;
  const frame = sharedUniformGroup('skyFrame', 0, THREE.NodeUpdateType.RENDER);
  const v4 = (name) => {
    const s = [sentinel(), sentinel(), sentinel(), sentinel()];
    const u = uniform(new THREE.Vector4(...s)).setName(name);
    u.setGroup(frame);
    props['ext.' + name] = { kind: 'vec4', value: s };
    return u;
  };
  const prm = v4('cloudParams'), sun = v4('cloudSun'), cam = v4('cloudCam');
  const hash2 = Fn(([p]) => fract(sin(dot(p, vec2(127.1, 311.7))).mul(43758.5453)))
    .setLayout({ name: 'cloudHash', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const vnoise = Fn(([p]) => {
    const i = floor(p), f = fract(p), w = f.mul(f).mul(float(3).sub(f.mul(2)));
    return mix(mix(hash2(i), hash2(i.add(vec2(1, 0))), w.x), mix(hash2(i.add(vec2(0, 1))), hash2(i.add(vec2(1, 1))), w.x), w.y);
  }).setLayout({ name: 'cloudNoise', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const fbm = Fn(([p]) => vnoise(p).mul(0.5).add(vnoise(p.mul(2.02).add(13.7)).mul(0.25)).add(vnoise(p.mul(4.05).add(31.1)).mul(0.125)).add(vnoise(p.mul(8.1).add(7.3)).mul(0.0625)))
    .setLayout({ name: 'cloudFbm', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const scale = float(1 / 520); // metres per noise cell
  const wind = vec2(prm.y, prm.z).mul(prm.x);
  const p = positionWorld.xz.add(wind).mul(scale);
  const cover = prm.w;
  const dens = (q) => smoothstep(float(0.62).sub(cover.mul(0.42)), float(0.92).sub(cover.mul(0.3)), fbm(q)); // cover 1: an overcast with thin spots
  const d = dens(p).toVar();
  // toward the sun the cloud thins: that side is lit, the other side shaded
  const toSun = vec2(sun.x, sun.z).mul(0.035);
  const dSun = dens(p.add(toSun));
  const lit = clamp(d.sub(dSun).mul(2.5).add(0.55), 0, 1);
  const shade = mix(vec3(0.62, 0.68, 0.8), vec3(0.42, 0.45, 0.52), smoothstep(0.7, 1.0, cover)), bright = vec3(1.0, 0.99, 0.97).mul(sun.w);
  const col = mix(shade, bright, lit).mul(float(0.85).add(d.mul(0.15)));
  const dist = length(positionWorld.xz.sub(vec2(cam.x, cam.z)));
  const far = float(1).sub(smoothstep(cam.w.mul(0.55), cam.w, dist));
  mat.colorNode = vec4(col.mul(materialColor), 1);
  mat.opacityNode = d.mul(far).mul(0.92).mul(materialOpacity);
}
