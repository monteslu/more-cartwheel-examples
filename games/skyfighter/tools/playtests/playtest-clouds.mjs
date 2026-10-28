import{resolve}from'node:path';import{writeFileSync}from'node:fs';import assert from'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review'),headers={'content-type':'application/json','x-romdev-session':'sky-fighter-cloud-flight'};
async function t(n,a){const r=await(await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)})).json();if(r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=name=>t('wasm',{op:'read',name}),write=(name,value)=>t('wasm',{op:'write',name,value});
const shot=name=>t('frame',{op:'screenshot',scale:0.5,outputPath:resolve(out,name+'.png')});
const report={backends:[]};
for(const wgpu of [true,false]){
  await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu,deterministicStepMs:1000/60,deterministicSeed:17107});await step(25);await input({south:true});await step(2);await input({});
  assert.equal((await read('mission.missiles')).value,18,'sortie starts with eighteen missiles');
  for(const[n,v]of Object.entries({'jet.x':-65,'jet.z':-20,'jet.altitude':650,'jet.heading':0,'jet.pitch':0,'jet.roll':0}))await write(n,v);
  await step(30);await shot(wgpu?'20-cloud-approach':'23-gl-clouds');
  assert.equal((await read('cloud.active')).value,1,'native volumetric rendering active');assert.equal((await read('cloud.volumes')).value,16,'eight paired clouds rendered');
  await step(120);await shot(wgpu?'21-cloud-entry':'24-gl-cloud-entry');await step(120);await shot(wgpu?'22-cloud-exit':'25-gl-cloud-exit');
  assert.equal((await read('mission.state')).value,1,'fly through clouds without collision');
  const result={wgpu,volumes:(await read('cloud.volumes')).value,skipped:(await read('render.skipped')).value,errors:(await read('cw.gpu.errors')).value};assert.equal(result.skipped,0);assert.equal(result.errors,0);report.backends.push(result);
}
writeFileSync(resolve(out,'cloud-report.json'),JSON.stringify(report,null,2));console.log('PASS: clouds approached, entered and exited; eighteen missiles; both GPU backends',report);
