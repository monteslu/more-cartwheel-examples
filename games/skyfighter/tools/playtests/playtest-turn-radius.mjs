import{resolve}from'node:path';import{writeFileSync}from'node:fs';import assert from'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review'),headers={'content-type':'application/json','x-romdev-session':'sky-fighter-tight-turn-review'};
async function t(n,a){const r=await(await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)})).json();if(r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value;
const pos=async()=>[await read('jet.x'),await read('jet.z')];
function circleRadius([a,b,c]){const d=(p,q)=>Math.hypot(p[0]-q[0],p[1]-q[1]);const area2=Math.abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]));return d(a,b)*d(b,c)*d(c,a)/(2*area2);}
const report={};
for(const [version,path]of [['before','../../out/skyfighter-review/sky-before-tighter-turns.wasc'],['after','../../build/cart-wgpu/carts/skyfighter.wasc']]){
 report[version]={};
 for(const [name,boost,stick]of [['cruise',false,1],['afterburner',true,1],['fineAim',false,.2]]){
  await t('loadMedia',{platform:'wasmcart',path:resolve(root,path),deterministicSeed:17107,deterministicStepMs:1000/60});await step(25);await input({south:true});await step(2);await input({});
  for(const[n,v]of Object.entries({'jet.x':0,'jet.z':1500,'jet.altitude':3500,'jet.heading':0,'jet.pitch':0,'jet.roll':0,'jet.yaw_rate':0,'jet.speed_mps':boost?420:205,'mission.spawn':10000}))await t('wasm',{op:'write',name:n,value:v});
  for(let i=0;i<7;i++)await t('wasm',{op:'write',name:`enemy.${i}.active`,value:0});
  await input({east:boost,axes:{lx:stick}});await step(90);const points=[await pos()];await step(30);points.push(await pos());await step(30);points.push(await pos());
  const result={radiusMetres:circleRadius(points),speed:await read('jet.speed_mps'),yawRate:await read('jet.yaw_rate'),points};assert.equal(await read('mission.state'),1);assert.equal(await read('cw.gpu.errors'),0);report[version][name]=result;console.log(version,name,'measured radius',result.radiusMetres);
 }
}
assert(report.after.cruise.radiusMetres<report.before.cruise.radiusMetres*.60,'full-stick cruise radius must be materially tighter');assert(report.after.afterburner.radiusMetres<report.before.afterburner.radiusMetres*.50,'boost turn must stay within a useful combat radius');assert(Math.abs(report.after.fineAim.radiusMetres/report.before.fineAim.radiusMetres-1)<.03,'fine aiming corrections must preserve their response');
writeFileSync(resolve(out,'turn-radius-report.json'),JSON.stringify(report,null,2));console.log('PASS: tighter cruise and boost turns; fine aim response preserved');
