// Small coherent crown motion plus local flutter, captured for WebGPU and GL.
// Geometry stays fully three-dimensional; no camera-facing tree cards.
export const name='foliage';
export async function apply({TSL,mat,props,sentinel,kind}){
 const {uniform,float,vec3,positionLocal,normalLocal,normalWorld,instanceIndex,sin,cos,clamp,pow,max,dot,materialColor,materialEmissive,materialOpacity,texture,uv,smoothstep}=TSL;
 const seed=sentinel(),time=uniform(seed).setName('forestTime');props['ext.forestTime']={kind:'f32',value:[seed]};
 const phase=float(instanceIndex).mul(2.399963),height=clamp(positionLocal.y,0,1),weight=height.mul(height);
 const gust=sin(time.mul(.85).add(phase));
 const flutter=sin(time.mul(3.1).add(positionLocal.x.mul(43)).add(positionLocal.z.mul(37)).add(phase));
 mat.positionNode=positionLocal.add(vec3(gust.mul(.007).add(flutter.mul(.0012)).mul(weight),float(0),cos(time.mul(.67).add(phase)).mul(weight).mul(.004)));
 // Photographic dark gaps cut tiny holes through the crown surface.
 // Every crown remains a solid, irregular 3D mesh, viewed from any angle.
 if(kind!=='depth'){
 const sample=texture(mat.map,uv());
 const coverage=smoothstep(.018,.055,sample.g);
 mat.opacityNode=materialOpacity.mul(coverage);
 mat.alphaTestNode=float(.35);
  // Canopy interiors remain darker; sun-facing leaf detail retains the PBR map.
  mat.colorNode=materialColor.mul(height.mul(.25).add(.78));
  // A restrained green transmission term prevents black backs on sunlit leaves.
  const back=max(dot(normalWorld,vec3(.45,-.8,.4)),0);
  mat.emissiveNode=materialEmissive.add(vec3(.018,.026,.009).mul(pow(back,2)));
 }
}
