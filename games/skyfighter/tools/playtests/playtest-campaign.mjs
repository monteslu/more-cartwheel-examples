import{resolve}from'node:path';import{mkdirSync,writeFileSync}from'node:fs';import assert from'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/campaign');mkdirSync(out,{recursive:true});const headers={'content-type':'application/json','x-romdev-session':'sky-fighter-eight-missions'};
async function t(n,a){const r=await(await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)})).json();if(r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value,write=(name,value)=>t('wasm',{op:'write',name,value});
async function pulse(button){await input({[button]:true});await step(2);await input({});await step(2);}
async function set(v){for(const[n,value]of Object.entries(v))await write(n,value);}
async function shot(name){await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,name+'.png')});}
async function place(x,y,z){await input({});await set({'jet.x':x,'jet.altitude':y,'jet.z':z,'jet.heading':0,'jet.pitch':0,'jet.roll':0,'jet.speed_mps':205,'mission.health':100,'mission.spawn':10000});for(let i=0;i<7;i++)await write(`enemy.${i}.active`,0);await step(2);}
async function safe(){assert.equal(await read('cw.gpu.errors'),0);assert.equal(await read('render.skipped'),0);}
await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),deterministicSeed:17107,deterministicStepMs:1000/60});await step(25);await shot('00-mission-select');await pulse('south');assert.equal(await read('mission.level'),0);await step(35);await shot('01-country-fair');await safe();
const report={};
await place(await read('air.0.x')+12,await read('air.0.basket_y')+4,await read('air.0.z')+170);await step(8);await shot('01a-modern-balloon-occupied-basket');

// The same cannon hits a small basket without touching the envelope.
await place(await read('air.0.x'),await read('air.0.basket_y'),await read('air.0.z')+400);await pulse('south');await step(30);assert.equal(await read('air.0.state'),1,'basket hit releases an intact envelope');assert.equal(await read('air.0.hull_hp'),1);assert.equal(await read('air.0.basket_hp'),0);const y=await read('air.0.y');await step(60);assert(await read('air.0.y')>y+55,'released envelope must surge upward');report.basketRelease={state:await read('air.0.state'),rise:await read('air.0.y')-y};await shot('02-basket-destroyed-envelope-away');
// A hit to the envelope starts a falling rigid body for its surviving basket.
await place(await read('air.1.x'),await read('air.1.y'),await read('air.1.z')+400);const by=await read('air.1.basket_y');await pulse('south');await step(35);assert.equal(await read('air.1.state'),2);assert.equal(await read('air.1.hull_hp'),0);assert.equal(await read('air.1.basket_hp'),1);await step(60);assert(await read('air.1.basket_y')<by-30,'low basket must fall quickly under physics');assert.equal(await read('air.1.state'),3,'low basket must visibly reach the ground');report.envelopeRelease={state:await read('air.1.state'),drop:by-await read('air.1.basket_y')};await shot('03-envelope-burst-basket-falling');
for(let i=2;i<12;i++){await place(await read(`air.${i}.x`),await read(`air.${i}.y`),await read(`air.${i}.z`)+400);await pulse('south');await step(32);}
assert.equal(await read('mission.balloons'),12);assert.equal(await read('mission.state'),3,'all balloons complete Country Fair');await shot('04-country-fair-clear');await pulse('south');assert.equal(await read('mission.level'),1);await step(100);await shot('05-time-warp');await safe();
// All six dirigibles can be destroyed with the actual cannon.
for(let i=12;i<18;i++){await place(await read(`air.${i}.x`),await read(`air.${i}.y`),await read(`air.${i}.z`)+450);await input({south:true});await step(60);await input({});await step(20);assert.equal(await read(`air.${i}.hull_hp`),0);}
assert.equal(await read('mission.blimps'),6);
for(let i=0;i<8;i++){await place(-1000,850,300);await set({'enemy.0.x':-1000,'enemy.0.y':850,'enemy.0.z':-300,'enemy.0.heading':Math.PI,'enemy.0.hp':1,'enemy.0.active':1,'enemy.0.fire_cool':1000});await input({south:true});await step(45);await input({});await step(5);}
assert.equal(await read('mission.kills'),8);assert.equal(await read('mission.state'),3);await shot('06-time-warp-clear');await pulse('south');assert.equal(await read('mission.level'),2);await step(25);await shot('07-fuel-chain');await safe();
// Existing fuel-depot destruction is separately covered by playtest-combat;
// here verify its mission counter drives progression, rather than air kills.
await write('mission.kills',30);await step(2);assert.equal(await read('mission.state'),1);
for(let i=44;i<48;i++){await place(await read(`building.${i}.x`),await read(`building.${i}.y`),await read(`building.${i}.z`)+450);await input({south:true});await step(90);await input({});await step(10);assert.equal(await read(`building.${i}.hp`),0);}
assert.equal(await read('mission.depots'),4);await step(2);assert.equal(await read('mission.state'),3);await pulse('south');assert.equal(await read('mission.level'),3);await step(25);await shot('08-coastal-scramble');await write('mission.kills',10);await step(2);assert.equal(await read('mission.state'),3);await pulse('south');assert.equal(await read('mission.level'),4);await step(25);await shot('09-sky-fortress');await safe();
// Firing through the shield cannot damage the core.
await place(-900,1198,-350);await input({south:true});await step(70);await input({});assert.equal(await read('air.22.hull_hp'),24);assert.equal(await read('mission.shields'),4);await shot('10-core-protected');
for(let i=18;i<22;i++){await place(await read(`air.${i}.x`),await read(`air.${i}.y`),await read(`air.${i}.z`)+450);await input({south:true});await step(60);await input({});await step(10);assert.equal(await read(`air.${i}.hull_hp`),0,'shield generator must be destructible');}
assert.equal(await read('mission.shields'),0);await shot('11-shield-down');await place(-900,1198,-350);await input({south:true});await step(170);await input({});await step(15);assert.equal(await read('air.22.hull_hp'),0);assert.equal(await read('mission.state'),8,'core destruction starts the celebration');assert.equal(await read('mothership.fragments'),8);await shot('12-mothership-destroyed');await step(100);await shot('12a-capital-ship-secondary-blasts');await step(200);await shot('12b-capital-ship-smoke-and-wreckage');await step(800);assert.equal(await read('mission.state'),3);await safe();
// All remaining ground objectives must respond to the real main cannon.
for(const [level,first,count,label] of [[5,68,4,'red-fortress'],[6,84,3,'north-pole'],[7,88,4,'steel-tide']]){
  await pulse('south');assert.equal(await read('mission.level'),level);await step(30);await shot(`${13+level}-`+label);await safe();
  for(let i=first;i<first+count;i++){
    await place(await read(`building.${i}.x`),await read(`building.${i}.y`),await read(`building.${i}.z`)+(i===71?1000:450));
    await input({south:true});await step(i===71?170:90);await input({});await step(10);assert.equal(await read(`building.${i}.hp`),0,`${label} objective ${i} must be destructible`);
  }
  if(level===6){assert.equal(await read('mission.state'),1);await place(await read('air.23.x'),await read('air.23.y'),await read('air.23.z')+850);await input({south:true});await step(180);await input({});assert.equal(await read('air.23.hull_hp'),0);assert.equal(await read('mission.state'),8);await step(850);}
  assert.equal(await read('mission.state'),3,`${label} must clear`);await safe();await shot(`${23+level}-`+label+'-clear');
}
assert.equal((await read('campaign.progress'))&255,255,'all eight cleared markers must be saved');
const saved=await t('wasm',{op:'save'});assert.equal(saved.length,16);
await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc')});await step(30);assert.equal((await read('campaign.progress'))&255,255,'clear markers survive fresh host');
// Every scenario is selectable without unlocking; wrap selection and launch.
await write('mission.level',7);await pulse('up');await pulse('left');await pulse('left');await pulse('left');assert.equal(await read('mission.level'),0);await pulse('south');assert.equal(await read('mission.level'),0);assert.equal(await read('mission.balloons'),0);assert.equal(await read('air.0.hull_hp'),1);assert.equal(await read('mission.state'),1);await safe();report.complete=true;writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));console.log('PASS: eight missions, separate balloon components, blimps, goal progression, shield immunity and exposed core',report);
