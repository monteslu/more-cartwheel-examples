// The "flag" shading part on the flag cards (course_objects.c): the pennant
// (the card's right side; the pole is at u ~0.37 of the original flag.png)
// ripples up and down in the wind, each flag on its own phase.
//   ext.flagTime  f32: seconds
//   ext.flagWind  f32: 1 calm, more on a windy race
export const name = 'flag';
export async function apply({ TSL, mat, props, sentinel }) {
  const { uniform, float, vec3, positionLocal, instanceIndex, sin, smoothstep, uv } = TSL;
  const seed = sentinel(), time = uniform(seed).setName('flagTime');
  props['ext.flagTime'] = { kind: 'f32', value: [seed] };
  const wseed = sentinel(), wind = uniform(wseed).setName('flagWind');
  props['ext.flagWind'] = { kind: 'f32', value: [wseed] };
  const u = uv().x;
  const phase = float(instanceIndex).mul(1.7);
  const free = smoothstep(0.38, 0.75, u); // the pennant, from the pole to its tip
  const wave = sin(time.mul(7.5).add(u.mul(14)).add(phase)).add(sin(time.mul(3.1).add(u.mul(5)).add(phase.mul(0.7))).mul(0.5));
  mat.positionNode = positionLocal.add(vec3(float(0), wave.mul(0.045).mul(free).mul(wind), wave.mul(0.02).mul(free).mul(wind)));
}
