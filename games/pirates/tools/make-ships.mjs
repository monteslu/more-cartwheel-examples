// Original ship geometry. Regenerate with node games/pirates/tools/make-ships.mjs.
import {mkdirSync} from 'node:fs';
import {Glb, Geo} from '../../jetski/tools/gltf-writer.mjs';
const out = new URL('../assets/models/', import.meta.url);
mkdirSync(out, {recursive:true});
const glb = new Glb(), groups = new Map();
const colors = {
  oak:[.23,.105,.045], plank:[.46,.28,.13], plankLight:[.55,.36,.19],
  trim:[.105,.055,.027], brass:[.58,.39,.13], iron:[.045,.052,.055],
  canvas:[.81,.74,.57], rope:[.22,.17,.105], glass:[.13,.29,.32]
};
for(const [name,tint] of Object.entries(colors)) {
  const g=new Geo();
  const mat=glb.pbr(name,null,null,{tint,rough:name==='brass'?.38:.85,metal:['brass','iron'].includes(name)?.65:0,doubleSided:name==='canvas'});
  groups.set(name,{g,mat});
}
function geom(name){return groups.get(name).g;}
function box(mat,c,x,y,z){geom(mat).box(c,x,y,z,[1,1,1],0);}
function beam(mat,a,b,r=.025,r2=r,n=8){geom(mat).limb(a,b,r,r2,[1,1,1],0,n);}
function quad(mat,a,b,c,d){const g=geom(mat);g.ctr=null;g.quad(a,b,c,d,[1,1,1],0);}
// Longitudinal frames: stern is negative Z, sharp bow positive Z.
const frames=[[-3.1,.63],[-2.7,1.02],[-2,1.27],[-1,1.38],[0,1.4],[1,1.26],[2,.92],[2.8,.43],[3.35,.025]];
const sections=[[-.65,.16],[-.43,.55],[-.08,.86],[.35,1],[.75,.95]];
for(let j=0;j<sections.length-1;j++)for(let i=0;i<frames.length-1;i++)for(const side of [-1,1]){
  const point=(f,s)=>[side*f[1]*s[1],s[0]+.12*Math.pow(Math.abs(f[0])/3.35,3),f[0]];
  const a=point(frames[i],sections[j]),b=point(frames[i+1],sections[j]),c=point(frames[i+1],sections[j+1]),d=point(frames[i],sections[j+1]);
  if(side===1)quad(j===0?'trim':'oak',a,d,c,b);else quad(j===0?'trim':'oak',a,b,c,d);
}
// Planks follow the hull's tapered outline instead of a rectangular deck.
for(let i=0;i<frames.length-1;i++){
 const [z0,w0]=frames[i],[z1,w1]=frames[i+1];
 for(let k=0;k<10;k++){
  const u0=-.91+k*.182+.006,u1=u0+.17;
  quad(k%3?'plank':'plankLight',[u0*w0,.64,z0],[u0*w1,.64,z1],[u1*w1,.64,z1],[u1*w0,.64,z0]);
 }
 for(const side of [-1,1]){
  for(const y of [.25,.78,1.02])beam(y===.25?'brass':'trim',[side*w0*.95,y,z0],[side*w1*.95,y,z1],y===1.02?.055:.035);
  beam('oak',[side*w0*.95,.65,z0],[side*w0*.95,1.03,z0],.045);
 }
}
// Transom, raised quarterdeck, glazed captain's cabin, carved stern rails.
box('oak',[0,.82,-2.66],1.83,.75,.74);
box('plankLight',[0,1.22,-2.6],1.98,.09,1.05);
for(const x of [-.62,-.31,0,.31,.62]){
 box('brass',[x,.92,-3.045],.255,.34,.035);
 box('glass',[x,.92,-3.07],.19,.25,.018);
 beam('brass',[x,.77,-3.09],[x,1.07,-3.09],.014);
}
for(const x of [-.91,.91])beam('trim',[x,1.46,-3.08],[x,1.46,-2.1],.045);
beam('brass',[-.93,1.46,-3.08],[.93,1.46,-3.08],.04);
// Cannon carriages, barrels, muzzle rings and hull gunport surrounds.
for(const side of [-1,1])for(const z of [-1.65,-.55,.55,1.55]){
 box('trim',[side*1.15,.78,z],.35,.33,.42);
 box('plank',[side*.94,.73,z],.4,.16,.36);
 beam('iron',[side*.84,.87,z],[side*1.59,.87,z],.105,.085,12);
 beam('brass',[side*1.49,.87,z],[side*1.56,.87,z],.113,.113,12);
 for(const dz of [-.16,.16])beam('trim',[side*.96,.69,z+dz],[side*1.18,.69,z+dz],.11,.11,10);
}
// Two masts, square sails with belly and scalloped lower edge, seams and ropes.
for(const [z,h,w] of [[-.75,5.5,2.9],[1.35,4.5,2.3]]){
 beam('oak',[0,.64,z],[0,h,z],.095,.045,12);
 for(const [top,height,width] of [[h-.45,1.65,w],[h-2.25,1.25,w*1.04]]){
  beam('trim',[-width*.56,top,z],[width*.56,top,z],.045);
  const p=(u,v)=>[(u-.5)*width*(1-.12*v),top-v*height+.12*Math.sin(u*Math.PI)*v,z+.36*Math.sin(u*Math.PI)*Math.sin(v*Math.PI)+.05];
  for(let v=0;v<8;v++)for(let u=0;u<16;u++)quad('canvas',p(u/16,v/8),p((u+1)/16,v/8),p((u+1)/16,(v+1)/8),p(u/16,(v+1)/8));
  for(let u=0;u<=8;u++)for(let v=0;v<8;v++)beam('rope',p(u/8,v/8),p(u/8,(v+1)/8),.006,.006,4);
  for(const u of [0,1])beam('rope',p(u,1),[u?1.15:-1.15,.9,z+.75],.012,.012,5);
 }
 for(const side of [-1,1])for(const dz of [-.55,0,.55])beam('rope',[0,h-.65,z],[side*1.16,.85,z+dz],.014,.014,5);
 for(let k=0;k<9;k++){
  const t=k/10;
  for(const side of [-1,1])beam('rope',[side*1.16*(1-t),.85+(h-1.5)*t,z-.55*(1-t)],[side*1.16*(1-t),.85+(h-1.5)*t,z+.55*(1-t)],.009,.009,4);
 }
}
beam('oak',[0,.75,2.3],[0,1.6,4.5],.075,.025,10);
beam('rope',[0,4.1,1.35],[0,1.6,4.5],.014);
quad('canvas',[0,3.7,1.5],[0,1.7,4.15],[0,1.4,2.35],[0,2.2,1.6]);
beam('rope',[0,5.4,-.75],[0,4.4,1.35],.013);
// Hatch grates, deck barrels, capstan and helm.
box('trim',[0,.68,.1],.65,.08,.8);
for(let i=-3;i<=3;i++)beam('plankLight',[i*.085,.74,-.28],[i*.085,.74,.48],.022,.022,4);
for(const x of [-.55,.55]){
 beam('oak',[x,.65,-1.85],[x,1.04,-1.85],.16,.16,12);
 for(const y of [.7,.95])beam('iron',[x,y,-1.85],[x,y+.035,-1.85],.168,.168,12);
}
beam('oak',[0,1.25,-2.45],[0,1.7,-2.45],.05);
for(let i=0;i<12;i++){
 const a=i*Math.PI/6,b=(i+1)*Math.PI/6;
 beam('brass',[.24*Math.sin(a),1.7+.24*Math.cos(a),-2.45],[.24*Math.sin(b),1.7+.24*Math.cos(b),-2.45],.023);
 beam('oak',[0,1.7,-2.45],[.29*Math.sin(a),1.7+.29*Math.cos(a),-2.45],.014);
}
const prims=[];
for(const {g,mat} of groups.values()) {delete g.col;delete g.joints;delete g.weights;if(g.idx.length)prims.push([g,mat]);}
glb.sceneNodes.push(glb.node({name:'Brigantine',mesh:glb.mesh('Brigantine',prims)}));
glb.write(new URL('brigantine.glb',out).pathname);
console.log('Wrote brigantine.glb');
