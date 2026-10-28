import{resolve}from'node:path';import{mkdirSync,writeFileSync}from'node:fs';import assert from'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/scenarios');mkdirSync(out,{recursive:true});const headers={'content-type':'application/json','x-romdev-session':'sky-fighter-scenario-settings'};
async function t(n,a){const res=await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)});const r=await res.json();if(!res.ok||r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value,write=(name,value)=>t('wasm',{op:'write',name,value});
async function pulse(button){await input({[button]:true});await step(2);await input({});await step(2);}
async function shot(name){await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,name+'.png')});}
const report=[];for(const wgpu of [true,false]){
const backend=wgpu?'webgpu':'gl';await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu,deterministicSeed:17107,deterministicStepMs:1000/60});await step(25);
const levels=process.env.SKY_SCENARIOS?process.env.SKY_SCENARIOS.split(",").map(Number):[0,1,2,3,4,5,6,7];for(const level of levels){
 if(process.env.SKY_SCENARIOS)await write('mission.level',level);else if(level===4){await pulse('down');await pulse('left');await pulse('left');await pulse('left');}else if(level>0)await pulse('right');assert.equal(await read('mission.level'),level,'every mission is freely selectable');await pulse('south');assert.equal(await read('mission.state'),1);assert.equal(await read('map.style'),[1,2,3,0,4,5,6,7][level]);await step(120);assert.equal(await read('mission.state'),1,'safe launch '+level);await shot(`${backend}-${level}-launch`);
 assert.equal(await read('cw.gpu.errors'),0);assert.equal(await read('render.skipped'),0);assert.equal(await read('cw.three.missing'),0);report.push({backend,level,buildings:await read("map.district_buildings"),style:await read('map.style'),gpuErrors:0,skipped:0});
 if(level===5){for(const [label,x,y,z,pitch]of [['palace',-650,210,-450,-.1],['clock-tower',-360,135,380,0],['cathedral',500,170,-350,0]]){for(const [name,value]of Object.entries({'jet.x':x,'jet.z':z,'jet.altitude':y,'jet.heading':0,'jet.pitch':pitch,'jet.roll':0,'mission.spawn':10000}))await write(name,value);await step(12);await shot(`${backend}-kremlin-${label}`);}}
 if(level===4){for(const [name,value]of Object.entries({'jet.x':1000,'jet.z':2000,'jet.altitude':800,'jet.heading':-.5,'jet.pitch':-.15,'jet.roll':0,'mission.spawn':10000}))await write(name,value);await step(16);await shot(`${backend}-sky-fortress-city`);}
 if(level===6){await write('mission.spawn',0);await step(60);await shot(`${backend}-north-pole-sleighs`);
 // Approach a decorated workshop at low altitude, avoiding its roof.
 for(const [name,value]of Object.entries({'jet.x':-1100,'jet.z':-1200,'jet.altitude':125,'jet.heading':0,'jet.pitch':-.1,'jet.roll':0,'mission.spawn':10000}))await write(name,value);await step(20);await shot(`${backend}-north-pole-workshop`);}
 await pulse('start');assert.equal(await read('mission.state'),2);await pulse('down');await pulse('down');await pulse('south');assert.equal(await read('mission.state'),0,'visible pause menu returns to mission selector');
}
}
writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));console.log('PASS: all eight maps launch, remain playable, draw without skipped pipelines on WebGPU and GL, and return to selection',report);
