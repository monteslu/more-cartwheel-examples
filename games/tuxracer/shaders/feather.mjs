// The "feather" shading part on Tux's skinned plumage (tux_skin.c): a cool
// rim light on the dark plumage only (the vertex colour decides), so his
// blue-black back reads against the snow. The plumage itself is smooth and
// matte: no normal perturbation, no sheen.
export const name = 'feather';
export async function apply({ TSL, mat, kind }) {
  if (kind === 'depth') return;
  const { vec3, float, smoothstep, max, dot, normalView, positionViewDirection, vertexColor, materialEmissive } = TSL;
  const vc = vertexColor();
  const dark = float(1).sub(smoothstep(0.2, 0.5, vc.r)); // the dark plumage, not the white front or the orange
  const rim = smoothstep(0.6, 1.0, float(1).sub(max(dot(normalView, positionViewDirection), 0)));
  mat.emissiveNode = materialEmissive.add(vec3(0.035, 0.05, 0.09).mul(rim).mul(dark));
}
