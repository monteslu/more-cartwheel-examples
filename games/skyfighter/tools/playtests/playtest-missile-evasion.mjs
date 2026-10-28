import {resolve} from 'node:path';
import {mkdirSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/missile-evasion');mkdirSync(out,{recursive:true});
const headers={'content-type':'application/json','x-romdev-session':'sky-fighter-predictable-missile-evasion'};
async function t(n,a){const r=await(await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)})).json();if(r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value,write=(name,value)=>t('wasm',{op:'write',name,value});
async function set(values){for(const[n,v]of Object.entries(values))await write(n,v);}
async function fresh(approach='rear'){
 await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),deterministicStepMs:1000/60,deterministicSeed:17107});await step(25);await write('mission.level',3);await input({south:true});await step(2);await input({});
 for(let i=0;i<7;i++)await write(`enemy.${i}.active`,0);
 await set({'mission.spawn':10000,'jet.x':1500,'jet.z':0,'jet.altitude':2500,'jet.heading':0,'jet.pitch':0,'jet.roll':0,'jet.speed_mps':205});await step(2);
 const z=await read('jet.z'),x=await read('jet.x');
 const [ex,ez,heading]=approach==='rear'?[x,z+1200,0]:approach==='front'?[x,z-1200,Math.PI]:[x+1200,z-400,Math.atan2(-1200,-400)];
 await set({'enemy.0.x':ex,'enemy.0.y':2500,'enemy.0.z':ez,'enemy.0.heading':heading,'enemy.0.hp':50,'enemy.0.fire_cool':0,'enemy.0.seeker':3,'enemy.0.active':1});await step(2);await write('enemy.0.active',0);
 assert.equal(await read('hostile.0.missile'),1,'fixture must launch a real AI missile');assert.equal(await read('defense.incoming'),1);assert.equal(await read('mission.health'),100);
}
async function outcome(){return{health:await read('mission.health'),evasions:await read('defense.evasions'),decoyed:await read('defense.decoyed'),incoming:await read('defense.incoming'),state:await read('mission.state')};}
const report={};
for(const approach of (process.env.SKY_FLARES_ONLY?[]:['rear','front'])){
 await fresh(approach);await step(260);report[approach+'Straight']=await outcome();assert.equal(report[approach+'Straight'].health,65,'flying straight into the missile should hit once');console.log(approach,'straight',report[approach+'Straight']);
 for(const sign of [-1,1]){
  await fresh(approach);
  // Start the turn on the visible terminal warning, not a hidden missile timer.
  let frames=0;while(await read('defense.eta')>1.6&&frames<200){await step(3);frames+=3;}
  await input({axes:{lx:sign}});await step(200);await input({});
  const result=report[approach+(sign<0?'Left':'Right')]=await outcome();result.waitFrames=frames;
  console.log(approach,sign,result);assert.equal(result.health,100,'a committed break on the warning must evade consistently');assert(result.evasions>=1);assert.equal(result.incoming,0);
 }
}
for(const binding of ['rstick','lt']){
 await fresh('front');
 if(binding==='lt')await input({axes:{lt:1}});else await input({r3:true});
 await step(2);await input({});
 const result=report['flare-'+binding]={cooldown:await read('defense.flare_cooldown'),ready:await read('defense.flares'),decoyed:await read('defense.decoyed')};console.log(binding,result);
 assert(result.cooldown>6.8,'binding must launch flares');assert.equal(result.ready,0);
 await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,'flares-'+binding+'.png')});
 await step(200);Object.assign(result,await outcome());assert.equal(result.health,100,'flared missile must pass without damage');assert.equal(result.decoyed,1);
 await step(230);assert.equal(await read('defense.flares'),1,'flares recharge');
}
if(!process.env.SKY_FLARES_ONLY){
 // A ballistic missile remains dangerous after losing tracking. The HUD must
 // keep warning until it has actually passed, without falsely reporting a win.
 await fresh('front');const z=await read('jet.z');
 await set({'hostile.0.x':1520,'hostile.0.y':2500,'hostile.0.z':z-60,'hostile.0.vx':0,'hostile.0.vy':0,'hostile.0.vz':8.8,'hostile.0.target':-1});
 await step(1);assert.equal(await read('defense.incoming'),1);assert.equal(await read('defense.evasions'),0);
 await step(30);report.cleanPass=await outcome();assert.equal(report.cleanPass.health,100);assert.equal(report.cleanPass.incoming,0);assert.equal(report.cleanPass.evasions,1);
 // A second bandit cannot stack another guided threat on the current one.
 await fresh('rear');await set({'enemy.1.x':1500,'enemy.1.y':2500,'enemy.1.z':1100,'enemy.1.heading':0,'enemy.1.fire_cool':0,'enemy.1.seeker':3,'enemy.1.active':1});
 await step(3);assert.equal(await read('hostile.1.active'),0);assert.equal(await read('defense.incoming'),1);
 await write('enemy.1.active',0);report.launchGap=await read('defense.launch_cooldown');assert(report.launchGap>11.8);
 // Sample every tick during a break to enforce the advertised angular limit.
 await input({axes:{lx:1}});let maxTurn=0,previous=await Promise.all(['vx','vy','vz'].map(v=>read('hostile.0.'+v)));
 for(let i=0;i<45;i++){await step(1);const v=await Promise.all(['vx','vy','vz'].map(v=>read('hostile.0.'+v)));const dot=v.reduce((n,x,j)=>n+x*previous[j],0)/(Math.hypot(...v)*Math.hypot(...previous));maxTurn=Math.max(maxTurn,Math.acos(Math.max(-1,Math.min(1,dot)))*180/Math.PI*60);previous=v;}
 report.maxTurnDegreesPerSecond=maxTurn;assert(maxTurn<=28.05);await input({});
 await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,'missile-warning.png')});
}
report.gpuErrors=await read('cw.gpu.errors');report.skipped=await read('render.skipped');assert.equal(report.gpuErrors,0);assert.equal(report.skipped,0);
writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));console.log('PASS: real AI missiles, straight-flight hits, symmetric timed dodges, R3/LT flares and recharge',report);
