// The "snow" shading part (cartwheel/shading.h), composed on the terrain's
// material after the terrain surface part and the engine's csm shadows
// (states x terrain_csm_snow, terrainlow_csm_snow, ...). three.js r186 TSL,
// captured into GLSL and WGSL by the engine's tools/programs.mjs --extend.
//
// What it adds, on top of the colour, roughness and view normal the terrain
// part computed from the original tiles:
//   - snow glints: sparse micro-facet cells (two fixed cell sizes, faded with
//     distance) whose random facet normal lines up with the sun / eye half
//     vector for a moment as the camera moves: they twinkle, they do not slide
//   - ice: a glassy roughness, a cyan depth and dark crack veins
//   - cloud shadows: a slow noise field drifting over sunny courses
// The surface kind is read off the tile colour (snow is bright, ice is a
// bluish grey, rock is dark): the terrain part keeps its layer weights to
// itself.
//
// Names after "ext." are what the C fill hook sets (src/textures.c):
//   ext.snowSun     xyz: unit direction TO the sun (world), w: glint strength (0 = off)
//   ext.snowParams  x: time (s), y: cloud shadow strength, z: cloud drift, w: ice sheen strength
export const name = 'snow';

export async function apply({ TSL, THREE, mat, props, sentinel, kind }) {
  if (kind === 'depth') return; /* a caster carries nothing of this */
  const {
    Fn, uniform, sharedUniformGroup, positionWorld, positionView, cameraViewMatrix,
    vec2, vec3, vec4, float, floor, fract, sin, dot, normalize, mix, max, min, smoothstep, abs, length, pow, clamp,
  } = TSL;
  // in the terrain part's own uniform group: WebGPU allows 4 bind groups a pipeline and the terrain's
  // states use all 4 (a group of this part's own made 5: every terrain pipeline failed validation)
  const frame = sharedUniformGroup('terrainFrame', 0, THREE.NodeUpdateType.RENDER);
  const v4 = (name) => {
    const s = [sentinel(), sentinel(), sentinel(), sentinel()];
    const u = uniform(new THREE.Vector4(...s)).setName(name);
    u.setGroup(frame);
    props['ext.' + name] = { kind: 'vec4', value: s };
    return u;
  };
  const sun = v4('snowSun');
  const prm = v4('snowParams');
  const fx = v4('snowFx'); // x glints, y surface detail, z trail, w cloud shadows (each 0 or 1): the "Snow detail" lever
  const trailPrm = v4('snowTrailBox');
  const detailTex = new THREE.DataTexture(new Uint8Array(16), 2, 2, THREE.RGBAFormat, THREE.UnsignedByteType);
  detailTex.name = 'ext.snowDetail';
  detailTex.minFilter = THREE.LinearMipmapLinearFilter; detailTex.magFilter = THREE.LinearFilter;
  detailTex.wrapS = detailTex.wrapT = THREE.RepeatWrapping;
  detailTex.generateMipmaps = true;
  detailTex.needsUpdate = true; // x y: the trail window's origin (world x, z), z: 1 / its size, w: strength (0 = none); (a name of its own: the texture is ext.snowTrail)
  // the trail: where his belly pressed the snow (red = depth), a window that follows him (src/textures.c)
  const trailTex = new THREE.DataTexture(new Uint8Array(16), 2, 2, THREE.RGBAFormat, THREE.UnsignedByteType);
  trailTex.name = 'ext.snowTrail';
  trailTex.minFilter = trailTex.magFilter = THREE.LinearFilter;
  trailTex.wrapS = trailTex.wrapT = THREE.ClampToEdgeWrapping;
  trailTex.generateMipmaps = false;
  trailTex.needsUpdate = true;

  // ── noise ──
  const hash2 = Fn(([p]) => fract(sin(dot(p, vec2(127.1, 311.7))).mul(43758.5453)))
    .setLayout({ name: 'snowHash', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const vnoise = Fn(([p]) => {
    const i = floor(p), f = fract(p), w = f.mul(f).mul(float(3).sub(f.mul(2)));
    return mix(mix(hash2(i), hash2(i.add(vec2(1, 0))), w.x), mix(hash2(i.add(vec2(0, 1))), hash2(i.add(vec2(1, 1))), w.x), w.y);
  }).setLayout({ name: 'snowNoise', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });
  const vgrad = Fn(([p]) => { // value noise and its gradient (per unit of p) from the same four hashes
    const i = floor(p), f = fract(p), w = f.mul(f).mul(float(3).sub(f.mul(2))), dw = f.mul(float(1).sub(f)).mul(6);
    const a = hash2(i), b = hash2(i.add(vec2(1, 0))), c = hash2(i.add(vec2(0, 1))), d = hash2(i.add(vec2(1, 1)));
    const v = mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
    const dx = mix(b.sub(a), d.sub(c), w.y).mul(dw.x);
    const dy = mix(c.sub(a), d.sub(b), w.x).mul(dw.y);
    return vec3(v, dx, dy);
  }).setLayout({ name: 'snowGrad', type: 'vec3', inputs: [{ name: 'p', type: 'vec2' }] });
  const fbm = Fn(([p]) => vnoise(p).mul(0.55).add(vnoise(p.mul(2.03).add(19.1)).mul(0.28)).add(vnoise(p.mul(4.11).add(7.7)).mul(0.17)))
    .setLayout({ name: 'snowFbm', type: 'float', inputs: [{ name: 'p', type: 'vec2' }] });

  const base = mat.colorNode ? mat.colorNode.rgb : TSL.materialColor.rgb;
  const rough0 = mat.roughnessNode ? mat.roughnessNode : TSL.materialRoughness;
  const nV0 = mat.normalNode ? mat.normalNode : TSL.normalView; // view-space surface normal
  // ── the slide trail: a groove pressed into the snow, its normal tilted along the depth gradient ──
  const tuv = positionWorld.xz.sub(vec2(trailPrm.x, trailPrm.y)).mul(trailPrm.z);
  const tstep = float(1 / 256);
  const trailV = Fn(() => {
    const o = vec4(0, 0, 0, 0).toVar(); // xyz: the tilt (world), w: the depth
    TSL.If(fx.z.greaterThan(0.5).and(tuv.x.greaterThan(0)).and(tuv.x.lessThan(1)).and(tuv.y.greaterThan(0)).and(tuv.y.lessThan(1)), () => {
      const g = TSL.texture(trailTex, tuv).r.mul(trailPrm.w);
      const gx = TSL.texture(trailTex, tuv.add(vec2(tstep, 0))).r.sub(TSL.texture(trailTex, tuv.sub(vec2(tstep, 0))).r).mul(trailPrm.w);
      const gz = TSL.texture(trailTex, tuv.add(vec2(0, tstep))).r.sub(TSL.texture(trailTex, tuv.sub(vec2(0, tstep))).r).mul(trailPrm.w);
      o.assign(vec4(gx.mul(3.5), 0, gz.mul(3.5), g));
    });
    return o;
  })();
  const g = trailV.w;
  const tiltW = trailV.xyz;                                         // toward the groove's centre
  const tiltV = cameraViewMatrix.mul(vec4(tiltW, 0)).xyz;
  // ── rock: the original tile is a soft brown blur; carve it. Strata from a low fbm (crevices darkened,
  // the normal tilted along its gradient), a cooler grey, and snow dusting on the faces that look up ──
  const rockM = smoothstep(0.42, 0.26, dot(base, vec3(0.2126, 0.7152, 0.0722))).toVar();
  const rp = positionWorld.xz.mul(0.9).add(positionWorld.y.mul(0.35)); // strata slant with height: not just a floor pattern
  const rh = float(0.08);
  const strata = Fn(() => { // (a branch lives inside a function block)
    const o = vec3(0.5, 0, 0).toVar();
    TSL.If(rockM.greaterThan(0.01), () => {
      const a = fbm(rp).toVar();
      o.assign(vec3(a, fbm(rp.add(vec2(rh, 0))).sub(a), fbm(rp.add(vec2(0, rh))).sub(a)));
    });
    return o;
  })();
  const r0 = strata.x, rx = strata.y, rz = strata.z;
  const crevice = smoothstep(0.5, 0.36, r0);
  const rockTiltW = vec3(rx, 0, rz).mul(-9.0).mul(rockM);
  const rockTiltV = cameraViewMatrix.mul(vec4(rockTiltW, 0)).xyz;
  // ── fine snow surface: wind ripples (sastrugi, long and low, across the wind) and granular micro-bumps,
  // from noise gradients, strongest within 25 m: the original tile is a 25-year-old blur up close ──
  const wd = positionWorld.xz;
  const near = float(1).sub(smoothstep(5, 16, length(positionView)));
  // ripples: two stretched octaves (long along x: they run across the slope); gradients analytic, in world units
  const snowTiltW = Fn(() => {
    const o = vec3(0, 0, 0).toVar();
    TSL.If(fx.y.greaterThan(0.5).and(near.greaterThan(0.001)), () => {
      // one read of the detail map (an 8 m tile): R, G the ripple gradient, B the grain
      const dmap = TSL.texture(detailTex, wd.mul(1 / 8));
      const rpx = dmap.r.sub(0.5).mul(2.0), rpz = dmap.g.sub(0.5).mul(2.0);
      const gmap = TSL.texture(detailTex, wd.mul(1 / 0.9).add(vec2(0.37, 0.61))); // the grain channel at a finer repeat
      const gn = gmap.b.sub(0.5);
      o.assign(vec3(rpx.mul(-0.33).add(gn.mul(-0.05)), 0, rpz.mul(-0.33).add(gn.mul(-0.05))).mul(near).mul(smoothstep(0.62, 0.8, dot(base, vec3(0.2126, 0.7152, 0.0722)))));
    });
    return o;
  })();
  const snowTiltV = cameraViewMatrix.mul(vec4(snowTiltW, 0)).xyz;
  const nV = normalize(nV0.add(tiltV).add(rockTiltV).add(snowTiltV));
  const upV = normalize(cameraViewMatrix.mul(vec4(0, 1, 0, 0)).xyz);
  const faceUp = clamp(dot(nV, upV), 0, 1);
  const dust = smoothstep(0.5, 0.85, faceUp).mul(float(0.6).add(fbm(positionWorld.xz.mul(2.3)).mul(0.6))).mul(rockM);
  const rough = mix(mix(rough0, rough0.mul(0.75), g), float(0.95), rockM.mul(float(1).sub(dust)));

  // ── which surface (off the tile colour, linear) ──
  const lum = dot(base, vec3(0.2126, 0.7152, 0.0722));
  const snowM = smoothstep(0.62, 0.8, lum).toVar();
  const blue = base.b.sub(base.r);
  const iceM = smoothstep(0.3, 0.46, lum).mul(float(1).sub(snowM)).mul(smoothstep(0.03, 0.09, blue)).mul(prm.w).toVar();

  const pw = positionWorld.xz;
  const dist = length(positionView);

  // ── cloud shadows (drifting, sunny only) ──
  const drift = vec2(prm.x.mul(0.014), prm.x.mul(0.005)).mul(prm.z); // ~0.5 m/s over the ground
  const cloudShade = Fn(() => {
    const o = float(1).toVar();
    TSL.If(fx.w.greaterThan(0.5).and(prm.y.greaterThan(0.001)), () => {
      const cloud = TSL.texture(detailTex, pw.mul(0.028 / 4).add(drift.mul(1 / 4))).a; // the tile holds 4 cloud cells: features of 10-35 m
      o.assign(float(1).sub(smoothstep(0.36, 0.68, cloud).mul(prm.y)));
    });
    return o;
  })();

  // ── ice: cracks and depth ──
  const vein = Fn(() => {
    const v = float(0).toVar();
    TSL.If(iceM.greaterThan(0.01), () => { v.assign(fbm(pw.mul(1.7).add(vec2(3.1, 8.7)))); });
    return v;
  })();
  const crack = smoothstep(0.045, 0.0, abs(vein.sub(0.5))).mul(0.45);
  const iceCol = base.mul(vec3(0.82, 0.93, 1.0)).mul(float(1).sub(crack));
  const stone = base.mul(vec3(0.72, 0.76, 0.84)).mul(float(1).sub(crevice.mul(0.55)));
  const dusted = mix(stone, vec3(0.86, 0.9, 0.97), dust);
  let col = mix(mix(base, iceCol, iceM), dusted, rockM).mul(cloudShade).mul(float(1).sub(g.mul(0.12))); // packed, a shade darker

  // ── snow glints ──
  const sunV = normalize(cameraViewMatrix.mul(vec4(sun.xyz, 0)).xyz);
  const eyeV = normalize(positionView.negate());
  const H = normalize(sunV.add(eyeV));
  const lit = clamp(dot(nV, sunV).mul(3), 0, 1); // the sun above the surface
  const glintAt = (cells, fadeIn0, fadeIn1, fadeOut0, fadeOut1) => Fn(() => {
    const out = float(0).toVar();
    TSL.If(fx.x.greaterThan(0.5).and(dist.greaterThan(fadeIn0)).and(dist.lessThan(fadeOut1)), () => {
    const q = pw.mul(cells);
    const c = floor(q);
    const h0 = hash2(c), h1 = hash2(c.add(vec2(5.3, 9.1))), h2 = hash2(c.add(vec2(13.7, 2.9))), h3 = hash2(c.add(vec2(21.1, 17.3)));
    // every cell is a crystal facing a random way (view space, as the half vector is): only the few
    // whose facet lies along the half vector light up, whatever the sun and the eye are doing
    const facet = normalize(vec3(h1, h2, h3).mul(2).sub(1));
    const spec = pow(max(dot(H, facet), 0), 110);
    // a small dot somewhere in the cell, not the whole square
    const centre = vec2(h2, h3).mul(0.5).add(0.25);
    const dotShape = smoothstep(0.22, 0.08, length(fract(q).sub(centre)));
    const fade = smoothstep(fadeIn0, fadeIn1, dist).mul(float(1).sub(smoothstep(fadeOut0, fadeOut1, dist)));
    out.assign(spec.mul(dotShape).mul(smoothstep(0.3, 0.32, h0)).mul(fade));
    });
    return out;
  })();
  const glint = glintAt(float(70), float(0), float(0.1), float(3), float(7))
    .add(glintAt(float(28), float(2), float(5), float(9), float(17)))
    .add(glintAt(float(9), float(8), float(14), float(28), float(48)))
    .mul(snowM).mul(lit).mul(sun.w).mul(cloudShade);
  const sunCol = vec3(1.0, 0.96, 0.88).mul(10); // HDR: AgX compresses it, bloom catches the brightest

  mat.colorNode = vec4(col, 1);
  mat.roughnessNode = mix(rough, float(0.09), iceM);
  mat.normalNode = nV; // the groove's tilt (the terrain part's normal otherwise)
  mat.emissiveNode = (mat.emissiveNode ? mat.emissiveNode : TSL.materialEmissive).add(sunCol.mul(glint));
}
