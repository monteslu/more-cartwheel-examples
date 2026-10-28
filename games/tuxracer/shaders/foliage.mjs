// The "foliage" shading part on the solid trees' leaf material (sky-mesa's
// spruce meshes with its snow-dusted needle texture, assets/textures/tree-snow-v1.jpg):
// a coherent crown sway plus a local flutter in the vertex stage (GPU wind:
// the instance matrices never change for it), photographic dark gaps cut
// through the needle surface, and a restrained translucency so sunlit
// backs are not black. After sky-mesa's shaders/foliage.mjs.
//   ext.forestTime  f32: seconds
//   ext.forestWind  f32: sway scale (1 calm, more on a windy race)
export const name = 'foliage';
export async function apply({ TSL, mat, props, sentinel, kind }) {
  const { uniform, float, vec3, positionLocal, normalWorld, instanceIndex, sin, cos, clamp, pow, max, dot, materialColor, materialEmissive, materialOpacity, texture, uv, smoothstep } = TSL;
  const seed = sentinel(), time = uniform(seed).setName('forestTime');
  props['ext.forestTime'] = { kind: 'f32', value: [seed] };
  const wseed = sentinel(), wind = uniform(wseed).setName('forestWind');
  props['ext.forestWind'] = { kind: 'f32', value: [wseed] };
  const phase = float(instanceIndex).mul(2.399963), height = clamp(positionLocal.y, 0, 1), weight = height.mul(height);
  const gust = sin(time.mul(0.85).add(phase));
  const flutter = sin(time.mul(3.1).add(positionLocal.x.mul(43)).add(positionLocal.z.mul(37)).add(phase));
  mat.positionNode = positionLocal.add(vec3(gust.mul(0.007).add(flutter.mul(0.0012)).mul(weight).mul(wind), float(0), cos(time.mul(0.67).add(phase)).mul(weight).mul(0.004).mul(wind)));
  if (kind !== 'depth' && !(mat.alphaTest > 0)) {
    // the solid needle surface: photographic dark gaps cut through it (a card that already
    // alpha-tests its sprig keeps its own test: a second alphaTestNode breaks the capture)
    const sample = texture(mat.map, uv());
    const coverage = smoothstep(0.018, 0.055, sample.g);
    mat.opacityNode = materialOpacity.mul(coverage);
    mat.alphaTestNode = float(0.35);
  }
  if (kind !== 'depth') {
    // crown interiors a little darker; the sunlit needle detail keeps the map
    mat.colorNode = materialColor.mul(height.mul(0.25).add(0.78));
    // light through the needles on the far side
    const back = max(dot(normalWorld, vec3(0.45, -0.8, 0.4)), 0);
    mat.emissiveNode = materialEmissive.add(vec3(0.018, 0.026, 0.009).mul(pow(back, 2)));
  }
}
