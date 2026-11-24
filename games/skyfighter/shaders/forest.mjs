// The tree crowns' surface, captured for WebGPU and GL: leaf gaps cut from the photo texture, a darker interior and a
// restrained back light. Geometry stays fully three-dimensional; no camera-facing tree cards. The wind is the engine's
// foliage part (cartwheel/foliage.h, set in src/forest.h), which moves the crowns in the colour and shadow passes. This
// part runs after it in the composed program, so it builds on the colour, opacity and emission nodes already there.
export const name='forest';
export async function apply({TSL,mat,kind}){
 if(kind==='depth')return;
 const {float,vec3,positionLocal,normalWorld,clamp,pow,max,dot,materialColor,materialEmissive,materialOpacity,texture,uv,smoothstep}=TSL;
 const height=clamp(positionLocal.y,0,1);
 // Photographic dark gaps cut tiny holes through the crown surface.
 // Every crown remains a solid, irregular 3D mesh, viewed from any angle.
 const sample=texture(mat.map,uv());
 const coverage=smoothstep(.018,.055,sample.g);
 mat.opacityNode=(mat.opacityNode??materialOpacity).mul(coverage);
 mat.alphaTestNode=float(.35);
 // Canopy interiors remain darker; sun-facing leaf detail retains the PBR map.
 mat.colorNode=(mat.colorNode??materialColor).mul(height.mul(.25).add(.78));
 // A restrained green transmission term prevents black backs on sunlit leaves.
 const back=max(dot(normalWorld,vec3(.45,-.8,.4)),0);
 mat.emissiveNode=(mat.emissiveNode??materialEmissive).add(vec3(.018,.026,.009).mul(pow(back,2)));
}
