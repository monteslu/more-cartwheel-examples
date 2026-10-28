import assert from 'node:assert/strict';import {resolve} from 'node:path';import {mkdirSync,writeFileSync} from 'node:fs';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/retry');mkdirSync(out,{recursive:true});const h={'content-type':'application/json','x-romdev-session':'sky-fighter-santa-retry-input-release'};
const t=async(n,a)=>{const r=await(await fetch('http://localhost:7331/tool/'+n,{method:'POST',headers:h,body:JSON.stringify(a)})).json();if(r.error)throw Error(JSON.stringify(r));return r;};const step=frames=>t('frame',{op:'step',frames}),write=(name,value)=>t('wasm',{op:'write',name,value}),read=async name=>(await t('wasm',{op:'read',name})).value,input=p=>t('input',{op:'set',ports:[p]});const report=[];
for(const wgpu of [true,false]){
 await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu,deterministicSeed:17107,deterministicStepMs:1000/60});await step(10);await write('mission.level',6);await input({south:true});await step(2);await input({});await step(2);
 for(const held of [false,true]){
  await write('jet.pitch',-1.45);await write('jet.altitude',32);await step(12);assert.equal(await read('mission.state'),6);await input(held?{axes:{ly:1}}:{});await step(2);await write('crash.age',5.45);await step(6);assert.equal(await read('mission.state'),4);
  await input(held?{south:true,axes:{ly:1}}:{south:true});await step(2);await input(held?{axes:{ly:1}}:{});await step(90);assert.equal(await read('mission.state'),1);assert(Math.abs(await read('jet.pitch'))<.001);assert.equal(await read('jet.altitude'),350);assert.equal(await read('jet.intact_visible'),1);assert.equal(await read('jet.wait_neutral'),held?2:0);
  if(!held){await input({});await step(3);assert.equal(await read('jet.wait_neutral'),0);}await input({axes:{ly:-1}});await step(18);assert.equal(await read('jet.wait_neutral'),0);assert(Math.abs(await read('jet.pitch'))>.12,'fresh stick input must pitch the jet after release');await input({});await step(12);assert.equal(await read('mission.state'),1);assert.equal(await read('cw.gpu.errors'),0);assert.equal(await read('render.skipped'),0);
  report.push({backend:wgpu?'webgpu':'gl',heldMenuDirection:held,retryAltitude:350,levelRetry:true,pitchControlRecovered:true});console.log('PASS',report.at(-1));
 }
 await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,(wgpu?'webgpu':'gl')+'-retry-controlled.png')});
}
writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));
