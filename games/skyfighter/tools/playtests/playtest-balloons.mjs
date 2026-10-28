import{resolve}from'node:path';import{mkdirSync,writeFileSync}from'node:fs';import assert from'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/balloons');mkdirSync(out,{recursive:true});const headers={'content-type':'application/json','x-romdev-session':'sky-fighter-modern-balloons'};
async function t(n,a){const res=await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)});const r=await res.json();if(!res.ok||r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value,write=(name,value)=>t('wasm',{op:'write',name,value});
async function pulse(button){await input({[button]:true});await step(2);await input({});await step(2);}
async function set(v){for(const[n,value]of Object.entries(v))await write(n,value);}
async function place(x,y,z,heading=0){await input({});await set({'jet.x':x,'jet.altitude':y,'jet.z':z,'jet.heading':heading,'jet.pitch':0,'jet.roll':0,'jet.speed_mps':135,'mission.health':100,'mission.spawn':10000});await step(2);}
async function shot(name){await t('frame',{op:'screenshot',scale:1,outputPath:resolve(out,name+'.png')});}
const report=[];for(const wgpu of [true,false]){
const backend=wgpu?'webgpu':'gl';await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu,deterministicSeed:17107,deterministicStepMs:1000/60});await step(25);await pulse('south');
for(let i=0;i<12;i++)assert.equal(await read(`air.${i}.hull_hp`),1);
const x=await read('air.0.x'),y=await read('air.0.y'),z=await read('air.0.z'),by=await read('air.0.basket_y');
await place(x+180,y,z+220,Math.atan2(-180,220));await step(8);await shot(backend+'-modern-envelope');
await place(x+6,by+2,z+90);await step(5);await shot(backend+'-two-passengers');
await place(x,by,z+400);await pulse('south');await step(30);assert.equal(await read('air.0.state'),1);assert.equal(await read('air.0.hull_hp'),1);assert.equal(await read('air.0.basket_hp'),0);const riseY=await read('air.0.y');await step(60);const rise=await read('air.0.y')-riseY;assert(rise>55);
const lowX=await read('air.1.x'),lowY=await read('air.1.y'),lowZ=await read('air.1.z'),lowBasket=await read('air.1.basket_y');
await place(lowX,lowY,lowZ+450);await pulse('south');await step(34);assert.equal(await read('air.1.state'),2);await shot(backend+'-basket-falling');await step(90);assert.equal(await read('air.1.state'),3);assert(await read('air.1.basket_y')<lowBasket-40);await shot(backend+'-basket-ground-impact');
assert.equal(await read('mission.state'),1);assert.equal(await read('cw.gpu.errors'),0);assert.equal(await read('render.skipped'),0);report.push({backend,balloons:12,passengersPerBasket:2,rise,basketGroundImpact:true,gpuErrors:0,skipped:0});
}
writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));console.log('PASS: twelve modern balloons, occupied open baskets, envelope release and visible low-basket ground impact on WebGPU and GL',report);
