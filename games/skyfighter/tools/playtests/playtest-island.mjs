import {resolve} from 'node:path';
import {mkdirSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/island');mkdirSync(out,{recursive:true});
const headers={'content-type':'application/json','x-romdev-session':'sky-fighter-island-horizon'};
async function t(n,a){const response=await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)});const r=await response.json();if(!response.ok||r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value;
async function fixture(values){await input({});for(const [name,value] of Object.entries(values))await t('wasm',{op:'write',name,value});await step(24);}
async function shot(name){await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,name+'.png')});}
const report=[];
for(const wgpu of [true,false]){
 const backend=wgpu?'webgpu':'gl';
 await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu,deterministicSeed:17107,deterministicStepMs:1000/60});await step(25);await t('wasm',{op:'write',name:'mission.level',value:3});await input({south:true});await step(2);await input({});
 for(let i=0;i<7;i++)await t('wasm',{op:'write',name:`enemy.${i}.active`,value:0});await t('wasm',{op:'write',name:'mission.spawn',value:10000});
 await fixture({'jet.x':-1150,'jet.z':-150,'jet.altitude':7900,'jet.heading':0,'jet.pitch':-1.56,'jet.roll':0});await shot(backend+'-island-overview');
 for(let i=0;i<4;i++){
  await fixture({'jet.x':0,'jet.z':0,'jet.altitude':7900,'jet.heading':i*Math.PI/2,'jet.pitch':-.05,'jet.roll':0});await shot(backend+'-high-horizon-'+i);
 }
 await fixture({'jet.x':2300,'jet.z':0,'jet.altitude':100,'jet.heading':Math.PI/2,'jet.pitch':0,'jet.roll':0});await shot(backend+'-low-ocean');
 await fixture({'jet.x':-65,'jet.z':300,'jet.altitude':140,'jet.heading':0,'jet.pitch':0,'jet.roll':0});await step(30);await shot(backend+'-airfield');
 assert.equal(await read('mission.state'),1);assert.equal(await read('cw.gpu.errors'),0);assert.equal(await read('render.skipped'),0);
 report.push({backend,gpuErrors:await read('cw.gpu.errors'),skipped:await read('render.skipped')});
}
writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));console.log('PASS: island overview, four ceiling-altitude horizons, sea-level horizon and airfield rendered on both backends',report);
