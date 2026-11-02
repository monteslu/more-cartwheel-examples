// Run through cartwheel-engine/tools/with-gpu-suite.mjs on the configured test GPU.
// ROMDEV_ROOT points to the romdev checkout. ENGINE_ROOT optionally overrides deps/cartwheel-engine.
import {readFileSync,writeFileSync,mkdirSync,openSync,closeSync} from 'node:fs';
import {spawn} from 'node:child_process';
import {createServer} from 'node:net';
import {resolve,dirname,join} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import assert from 'node:assert/strict';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const engine=resolve(process.env.ENGINE_ROOT||join(root,'deps/cartwheel-engine'));
if(!process.env.ROMDEV_ROOT)throw Error('Set ROMDEV_ROOT to the romdev checkout');
const {wgpuPin}=await import(pathToFileURL(join(engine,'tools/lib/wgpu-pin.mjs')));
const pin=wgpuPin();Object.assign(process.env,pin.env);
assert.ok(process.env.CW_SUITE_LOCK_HELD,'Run through with-gpu-suite.mjs so the server inherits GPU isolation');
const out=join(root,'out/pirates-review');mkdirSync(out,{recursive:true});
const listener=createServer();await new Promise(r=>listener.listen(0,'127.0.0.1',r));
const port=listener.address().port;await new Promise(r=>listener.close(r));
const log=openSync(join(out,'romdev-server.log'),'w');
const server=spawn(process.execPath,['--experimental-vm-modules','--no-wasm-code-gc',join(process.env.ROMDEV_ROOT,'packages/romdevtools/src/mcp/server.js'),'--port',String(port)],{env:process.env,stdio:['ignore',log,log]});
const base=`http://127.0.0.1:${port}`;
const headers={'content-type':'application/json','x-romdev-session':'pirates-review'};
async function call(name,args){
 const response=await fetch(base+'/tool/'+name,{method:'POST',headers,body:JSON.stringify(args)});
 const result=await response.json();assert.ok(response.ok&&!result.error,JSON.stringify(result));return result;
}
const results=[];
const reviewGates=JSON.parse(readFileSync(join(root,'tests/pirates-review-gates.json'),'utf8'));
async function command(h,text){const r=await call('wasm',{op:'command',text,maxFrames:600});assert.ok(!r.reply.error,JSON.stringify(r));return r.reply;}
async function field(h,name){const v=await call('wasm',{op:'read',name});return v.values??v.value;}
async function clean(h){assert.equal(await field(h,'cw.gpu.errors'),0);assert.equal(await field(h,'render.skipped'),0);}
async function shot(h,name){await call('frame',{op:'screenshot',path:join(out,name+'.png')});}
async function load(gpu,auto=false){
 const status=await call('loadMedia',{platform:'wasmcart',path:join(root,'build/cart-wgpu/carts',auto?'pirates_auto.wasc':'pirates.wasc'),wgpu:gpu==='webgpu',gpuPower:pin.power,deterministicSeed:1,deterministicStepMs:1000/60});
 if(gpu==='webgpu')pin.check('Pirates review server',JSON.stringify(status.gpuAdapter));
 return {stepFrames:frames=>call('frame',{op:'step',frames}),setInput:input=>call('input',{op:'set',...input}),destroy:()=>call('host',{op:'unload'})};
}
try {
 let ready=false;
 for(let i=0;i<100;i++){
  try{const r=await fetch(base+'/healthz');if(r.ok){ready=true;break;}}catch{}
  if(server.exitCode!==null)throw Error('romdev server exited; inspect romdev-server.log');
  await new Promise(r=>setTimeout(r,100));
 }
 assert.ok(ready,'romdev server did not start');
for(const gpu of ['gl','webgpu']) {
 let h=await load(gpu);
 try {
  await h.stepFrames(30);await shot(h,gpu+'-title');
  // Join through four separate controller ports, not a debug match command.
  await h.setInput({ports:[{}, {start:true}, {start:true}, {start:true}]});await h.stepFrames(1);
  await h.setInput({ports:[{},{},{},{}]});await h.stepFrames(1);
  await h.setInput({ports:[{start:true},{},{},{}]});await h.stepFrames(1);
  await h.setInput({ports:[{},{},{},{}]});await h.stepFrames(2);
  assert.equal(await field(h,'pirates.humans'),4);assert.equal(await field(h,'pirates.state'),1);
  await shot(h,gpu+'-four-controller-join');
  for(const review of reviewGates) {
   const map=review.map;
   await command(h,'pirates_map '+map);await command(h,'pirates_match 1 4');await h.stepFrames(8);
   const placement=await command(h,'pirates_placement');
   const control=await command(h,'pirates_placement 1');
   assert.ok(control.unsupported>0 && control.max_ground_gap>.1,'raised-prop control must fail: '+JSON.stringify(control));
   assert.equal((await command(h,'pirates_placement')).unsupported,0,'control must restore the prop');
   const rockControl=await command(h,'pirates_placement 2');
   assert.equal(rockControl.unsupported,1,'raised rock edge must fail: '+JSON.stringify(rockControl));
   assert.ok(rockControl.max_ground_gap>.01 && rockControl.control_bbox_gap<0,'buried lowest corner must not hide a raised edge: '+JSON.stringify(rockControl));
   assert.equal((await command(h,'pirates_placement')).unsupported,0,'rock control must restore the prop');
   assert.ok(placement.props>50);assert.ok(placement.cargo>0);assert.equal(placement.ships,6);
   for(const [k,v] of Object.entries(review.expectPlacement))assert.equal(placement[k],v,JSON.stringify(placement));
   for(const q of review.presets) {
    await command(h,'quality '+q);await h.stepFrames(16);await clean(h);await shot(h,`${gpu}-map${map}-${q}`);
   }
   // Idle captains may learn controls at their home anchorage without patrol camping.
   await command(h,'quality high');await h.stepFrames(review.idleFrames);
   assert.deepEqual(await field(h,'pirates.hp'),review.expectIdleHull);
   const late=await command(h,'pirates_placement');assert.equal(late.overlaps,0);
   results.push({gpu,map,placement,raisedPropControl:control,raisedRockEdgeControl:rockControl,idleHp:await field(h,'pirates.hp')});
   // Revisit the two raised rock edges identified in review at ground level.
   const [x,z]=map?[8.45,-8.46]:[-18.97,2.48];
   await command(h,`set Root/Camera position vec3(${x},3.2,${z+5})`);
   await command(h,'set Root/Camera rotation vec3(-0.55,0,0)');
   await command(h,'set Root/Camera fov 40');
   await h.stepFrames(2);await clean(h);await shot(h,`${gpu}-map${map}-rock-closeup`);
   await command(h,'set Root/Camera position vec3(0,68,24)');
   await command(h,`set Root/Camera rotation vec3(${-Math.atan2(68,22)},0,0)`);
   await command(h,'set Root/Camera fov 46');
  }
  const hull=await command(h,'pirates_test_hull');assert.equal(hull.spawns_clear,1);assert.equal(hull.no_penetration,1);
  assert.equal(hull.blocked,hull.cases,JSON.stringify(hull));assert.equal(hull.reversed_clear,hull.cases,JSON.stringify(hull));
  for(const text of ['pirates_test_escape','pirates_test_route'])assert.equal((await command(h,text)).banked,3);
  await clean(h);console.log(gpu+': placement, four presets on both maps, harbor safety, hull and routes passed');
 } finally {await h.destroy();}
 if(process.argv.includes('--checks-only'))continue;
 h=await load(gpu,true);
 try {
  const gates=JSON.parse(readFileSync(join(root,'tests/pirates-gates.json'),'utf8')).slice(0,2);
  let frames=1;
  for(const gate of gates) {
   if(gpu==='gl'&&gate.frames>1500)continue;
   const timing=[];
   while(frames<gate.frames){const n=Math.min(600,gate.frames-frames),t=performance.now();await h.stepFrames(n);frames+=n;timing.push({frame:frames,msPerFrame:(performance.now()-t)/n,transport:"romdev-server"});if(frames%3600===0||frames===gate.frames)console.log(gpu,"replay tick",frames);}
   const state=Object.fromEntries(await Promise.all(gate.fields.map(async k=>[k,await field(h,k)])));await clean(h);
   if(!process.argv.includes("--capture"))assert.deepEqual(state,gate.expect,gate.name);
   // --capture writes evidence for independent native comparison before updating expectations.
   writeFileSync(join(out,`${gpu}-${gate.name}.json`),JSON.stringify({state,timing},null,2));
   await shot(h,`${gpu}-${gate.name}`);console.log(gpu,gate.name,JSON.stringify(state));
  }
 } finally {await h.destroy();}
}
writeFileSync(join(out,'checks.json'),JSON.stringify(results,null,2));

} finally {
 server.kill('SIGTERM');
 await new Promise(resolve=>{if(server.exitCode!==null)resolve();else server.once('exit',resolve);});
 closeSync(log);
}
