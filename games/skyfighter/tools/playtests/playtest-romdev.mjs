import {resolve} from 'node:path';
import {mkdirSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const root=resolve(import.meta.dirname,'../..');
const out=resolve(root,'../../out/skyfighter-review');mkdirSync(out,{recursive:true});
const session='sky-fighter-v12-review';
async function tool(name,args){
  const response=await fetch(`http://127.0.0.1:7331/tool/${name}`,{method:'POST',headers:{'content-type':'application/json','x-romdev-session':session},body:JSON.stringify(args)});
  const r=await response.json();if(!response.ok||r.error)throw new Error(`${name}: ${JSON.stringify(r)}`);return r;
}
const step=frames=>tool('frame',{op:'step',frames});
const input=pad=>tool('input',{op:'set',ports:[pad]});
const read=name=>tool('wasm',{op:'read',name});
const write=(name,value)=>tool('wasm',{op:'write',name,value});
async function pulse(button){await input({[button]:true});await step(2);await input({});await step(2);}
async function shot(name){return tool('frame',{op:'screenshot',scale:0.5,outputPath:resolve(out,name+'.png')});}
async function resetFlight(heading){
  for(const [name,value] of Object.entries({'jet.x':-65,'jet.z':550,'jet.altitude':600,'jet.heading':heading,'jet.pitch':0,'jet.roll':0,'jet.yaw_rate':0,'mission.health':100}))await write(name,value);
  await input({});await step(2);
  assert.equal((await read('mission.state')).value,1,'flight test must stay airborne');
}
const report={cart:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),orientation:[]};
await tool('loadMedia',{platform:'wasmcart',path:report.cart,gpuPower:'high-performance',deterministicSeed:17107,deterministicStepMs:1000/60});
await step(25);await write('mission.level',3);await shot('01-title');await pulse('south');
for(const heading of [0,Math.PI/2,Math.PI,-Math.PI/2]){
  await resetFlight(heading);const y=(await read('jet.altitude')).value;
  await input({axes:{ly:-0.75}});await step(45);
  const forward=(await read('jet.visual_forward')).values;
  const altitude=(await read('jet.altitude')).value;
  const error=(await read('jet.attitude_error')).value;
  assert(forward[1]>0.1,`nose must point up at heading ${heading}`);
  assert(altitude>y+5,`up must climb at heading ${heading}`);
  assert(error<1e-5,`render/flight direction mismatch: ${error}`);
  const camera=(await read('jet.camera_forward')).values;
  const visiblePitch=Math.acos(Math.max(-1,Math.min(1,forward.reduce((sum,v,i)=>sum+v*camera[i],0))));
  assert(visiblePitch>.025,'chase camera must leave a visible aircraft pitch cue');
  if(heading===0)await shot('34-visible-nose-up');
  report.orientation.push({heading,forward,climb:altitude-y,error,visiblePitch});
}
console.log('PASS: climb and rendered nose agree at all four cardinal headings');
await input({});await pulse('start');await pulse('north');
assert.equal((await read('mission.state')).value,5,'settings opens');
await shot('02-settings');report.saveBefore=await tool('wasm',{op:'save'});
await pulse('south');report.saveAfter=await tool('wasm',{op:'save'});
assert.equal(report.saveAfter.length,16,'saved preferences format');
assert.notEqual(report.saveBefore.hex.slice(8,10),report.saveAfter.hex.slice(8,10),'inversion must change saved preferences');
await pulse('east');await pulse('start');
for(const heading of [0,Math.PI/2,Math.PI,-Math.PI/2]){
  await resetFlight(heading);const y=(await read('jet.altitude')).value;
  await input({axes:{ly:-0.75}});await step(45);
  const forward=(await read('jet.visual_forward')).values;
  const altitude=(await read('jet.altitude')).value;
  assert(forward[1]<-0.1,`inverted nose must point down at heading ${heading}`);
  assert(altitude<y-5,`inverted up must dive at heading ${heading}`);
}
console.log('PASS: inverted pitch works at all four cardinal headings');
await input({});await pulse('start');await pulse('north');await pulse('south');await pulse('east');await pulse('start');
await resetFlight(0);await input({axes:{lx:0.25}});await step(30);
report.partialBank=Math.abs((await read('jet.roll')).value);
await resetFlight(0);await input({right:true});await step(30);
report.dpadBank=Math.abs((await read('jet.roll')).value);
assert(report.dpadBank>report.partialBank*2,'analog stick must retain proportional range');
report.steering=[];
for(const boost of [false,true])for(const direction of [-1,1]){
  await resetFlight(0);const x=(await read('jet.x')).value;
  await input({east:boost,axes:{lx:direction}});await step(120);
  const heading=(await read('jet.heading')).value,dx=(await read('jet.x')).value-x;
  assert(heading*direction>0.2,'left/right must change flight heading');
  assert(dx*direction>25,'left/right must move the aircraft sideways');
  assert(Math.abs((await read('jet.roll')).value)<1.2,'ordinary steering must bank without barrel rolling');
  report.steering.push({boost,direction,heading,dx});
}
console.log('PASS: left/right steer and change course with and without afterburner');
await resetFlight(0);
async function placeOpponent(i){
  for(let j=0;j<7;j++)await write(`enemy.${j}.active`,0);
  const y=(await read('jet.altitude')).value,z=(await read('jet.z')).value;
  for(const [key,value]of Object.entries({x:-65,y,z:z-700,heading:Math.PI,hp:3,active:1}))await write(`enemy.${i}.${key}`,value);
}
await placeOpponent(0);
report.combatStart={kills:(await read('mission.kills')).value};
await input({south:true});await step(100);await shot('03-combat');
report.gunKills=(await read('mission.kills')).value-report.combatStart.kills;
assert(report.gunKills>0,'cannon must destroy an approaching opponent');
await input({});await step(180);await resetFlight(0);await placeOpponent(1);
const missileStartKills=(await read('mission.kills')).value;await step(40);
report.missileLock=(await read('target.lock')).value;
assert(report.missileLock>0.6,'air target must lock');
await pulse('west');await step(100);
report.combatEnd={kills:(await read('mission.kills')).value,health:(await read('mission.health')).value};
report.missileKills=report.combatEnd.kills-missileStartKills;assert(report.missileKills>0,'missile must destroy air target');
await shot('04-dogfight');
for(const preset of ['low','medium','high']){
  await tool('wasm',{op:'command',text:`quality ${preset}`});await step(4);
}
await tool('wasm',{op:'command',text:'quality medium'});await step(4);
await resetFlight(0);await input({east:true});await step(120);
report.boostSpeed=(await read('jet.speed_mps')).value;
await input({l:true});await step(120);
report.brakeSpeed=(await read('jet.speed_mps')).value;
assert(report.boostSpeed>270,'afterburner must accelerate');
assert(report.brakeSpeed<170,'airbrake must slow the aircraft');
await input({});
await resetFlight(0);await input({});await pulse('start');await pulse('north');await pulse('down');await pulse('down');await pulse('south');
const expertSave=await tool('wasm',{op:'save'});assert.equal(expertSave.hex.slice(10,12),'01','Expert control mode must persist');
await pulse('east');await pulse('start');await resetFlight(0);
await input({axes:{lx:1}});await step(30);await input({axes:{ly:-1}});await step(60);
assert((await read('jet.heading')).value>.2,'Expert bank then pull must change course');
await input({});await pulse('start');await pulse('north');await pulse('south');await pulse('east');await pulse('start');
console.log('PASS: saved Expert mode rolls, then banks and pulls through a turn; Standard restored');
report.skipped=(await read('render.skipped')).value;
report.missing=(await read('cw.three.missing_states')).text;
report.gpuErrors=(await read('cw.gpu.errors')).value;
report.pipeline=await tool('wasm',{op:'command',text:'pipeline'});
writeFileSync(resolve(out,'romdev-report.json'),JSON.stringify(report,null,2)+'\n');
assert.equal(report.skipped,0,`skipped draws: ${report.missing}`);
assert.equal(report.gpuErrors,0,'WebGPU validation errors');
assert(report.combatEnd.kills>report.combatStart.kills,'combat must produce kills');
console.log('PASS: combat, graphics presets, zero missing draws and zero GPU errors');
console.log(JSON.stringify({out,...report.combatEnd,orientationCases:8,skipped:report.skipped,gpuErrors:report.gpuErrors}));
