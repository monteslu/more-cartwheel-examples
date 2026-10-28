import {resolve} from 'node:path';import {mkdirSync,writeFileSync} from 'node:fs';import assert from 'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/destruction');mkdirSync(out,{recursive:true});
const headers={'content-type':'application/json','x-romdev-session':'sky-fighter-santa-pole-mothership-breakup'};
async function t(n,a){const r=await(await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)})).json();if(r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value,arr=async name=>(await t('wasm',{op:'read',name})).values,write=(name,value)=>t('wasm',{op:'write',name,value});
async function set(v){for(const[n,value]of Object.entries(v))await write(n,value);}
async function shot(name){await t('frame',{op:'screenshot',scale:.6,outputPath:resolve(out,name+'.png')});}
async function place(x,y,z){await input({});await set({'jet.x':x,'jet.altitude':y,'jet.z':z,'jet.heading':0,'jet.pitch':0,'jet.roll':0,'jet.speed_mps':205,'mission.spawn':10000});for(let i=0;i<7;i++)await write(`enemy.${i}.active`,0);await step(2);}
const report=[];
for(const wgpu of [true,false])for(const level of [6,4]){
 const backend=wgpu?'webgpu':'gl',name=backend+(level===6?'-santa':'-mothership');
 await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu,deterministicStepMs:1000/30,deterministicSeed:17107});await step(8);await write('mission.level',level);await input({south:true});await step(2);await input({});await step(2);
 if(level===6){
  await place(0,180,500);await input({south:true});await step(32);await input({});assert.equal(await read('destruction.pieces'),8,'pole must split into seven shaft sections and the gold cap');const p=await arr('destruction.7.position');await step(25);assert(Math.hypot(...(await arr('destruction.7.position')).map((x,i)=>x-p[i]))>5);await shot(name+'-pole-collapse');
  for(let i=84;i<=86;i++)await write(`building.${i}.hp`,0);await write('air.23.hull_hp',1);await place(-160,await read('air.23.y'),400);
 }else{await write('mission.shields',0);await write('air.22.hull_hp',1);await place(-900,1198,-350);}
 await input({south:true});for(let i=0;i<65&&await read('mission.state')===1;i++)await step(1);await input({});assert.equal(await read('mission.state'),8,'results must wait for the destruction');
 const pieces=level===6?await read('destruction.pieces'):await read('mothership.fragments');assert.equal(pieces,level===6?14:8);await step(20);await shot(name+'-breakup');
 const p=await arr(level===6?'destruction.9.position':'mothership.fragment_position');await step(85);assert.equal(await read('mission.state'),8,'no menu during the early aftermath');await shot(name+'-aftermath');const q=await arr(level===6?'destruction.9.position':'mothership.fragment_position');assert(Math.hypot(...q.map((x,i)=>x-p[i]))>20,'head/hull must move physically');
 const seconds=level===6?14:18;while(await read('victory.age')<seconds-.15)await step(Math.max(1,Math.min(90,Math.floor((seconds-.15-await read('victory.age'))*30))));assert.equal(await read('mission.state'),8);await step(8);assert.equal(await read('mission.state'),3);assert((await read('campaign.progress'))&(1<<level));await shot(name+'-results');
 assert.equal(await read('cw.gpu.errors'),0);assert.equal(await read('render.skipped'),0);assert.equal(await read('cw.three.missing'),0);
 // Replay must restore the detached pieces and remove their old physics bodies.
 await input({down:true});await step(2);await input({});await step(2);await input({south:true});await step(2);await input({});await step(2);assert.equal(await read('mission.state'),1);assert.equal(await read('destruction.pieces'),0);assert.equal(await read('mission.level'),level);await shot(name+'-replay-restored');
 report.push({backend,level,pieces,celebrationSeconds:seconds,physicsMovement:Math.hypot(...q.map((x,i)=>x-p[i])),replayRestored:true,gpuErrors:0});console.log('PASS',report.at(-1));
}
writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));
