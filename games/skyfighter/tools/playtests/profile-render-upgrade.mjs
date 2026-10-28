// Controlled CPU/render counters on romdev, not a portable GPU benchmark.
import {resolve} from 'node:path';
import {mkdirSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/engine-upgrade');mkdirSync(out,{recursive:true});const report={};
for(const wgpu of [false,true]){
 const backend=wgpu?'webgpu':'gl',headers={'content-type':'application/json','x-romdev-session':'sky-fighter-upgrade-profile-'+backend};
 async function t(name,args){const res=await fetch('http://127.0.0.1:7331/tool/'+name,{method:'POST',headers,body:JSON.stringify(args)}),r=await res.json();if(!res.ok||r.error)throw Error(name+': '+JSON.stringify(r));return r;}
 const read=async name=>(await t('wasm',{op:'read',name})).value,write=(name,value)=>t('wasm',{op:'write',name,value}),step=frames=>t('frame',{op:'step',frames}),command=text=>t('wasm',{op:'command',text});
 report[backend]=[];
 for(const config of [{grid:1,cull:false,prepass:false},{grid:2,cull:true,prepass:false},{grid:4,cull:true,prepass:false},{grid:2,cull:true,prepass:true}]){
  await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu,deterministicStepMs:16,deterministicSeed:17107});await step(15);await write('forest.grid',config.grid);await write('mission.level',0);
  await t('input',{op:'set',ports:[{south:true}]});await step(2);await t('input',{op:'set',ports:[{}]});await step(3);
  for(const [name,value] of Object.entries({'jet.x':0,'jet.z':0,'jet.altitude':600,'jet.heading':0,'jet.pitch':0,'jet.roll':0,'mission.spawn':10000}))await write(name,value);
  for(let i=0;i<7;i++)await write(`enemy.${i}.active`,0);
  const toggles=[await command(`set SceneryVisibility frustum_cull ${config.cull}`),await command(`set Coast/Post depth_prepass ${config.prepass}`)];
  await step(12);await command('perf reset');await step(24);const perf=await command('perf 20');
  const observation={...config,perf,toggles,meshes:await read('scenery.meshes'),visible:await read('scenery.visible'),culled:await read('scenery.culled'),missing:await read('cw.three.missing'),errors:await read('cw.gpu.errors')};
  report[backend].push(observation);console.log(backend,JSON.stringify(observation));assert.equal(observation.missing,0);assert.equal(observation.errors,0);await t('host',{op:'unload'});
 }
}
writeFileSync(resolve(out,'profile-report.json'),JSON.stringify(report,null,2));
