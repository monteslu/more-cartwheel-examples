// Run through cartwheel-engine/tools/with-gpu-suite.mjs on the configured test GPU.
// ROMDEV_ROOT points to the romdev checkout. ENGINE_ROOT optionally overrides deps/cartwheel-engine.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve,dirname,join} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import assert from 'node:assert/strict';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const engine=resolve(process.env.ENGINE_ROOT||join(root,'deps/cartwheel-engine'));
if(!process.env.ROMDEV_ROOT)throw Error('Set ROMDEV_ROOT to the romdev checkout');
const {wgpuPin}=await import(pathToFileURL(join(engine,'tools/lib/wgpu-pin.mjs')));
const pin=wgpuPin();Object.assign(process.env,pin.env);
const {WasmcartHost}=await import(pathToFileURL(join(process.env.ROMDEV_ROOT,'packages/romdevtools/src/host/WasmcartHost.js')));
const out=join(root,'out/pirates-review');mkdirSync(out,{recursive:true});
const results=[];
const reviewGates=JSON.parse(readFileSync(join(root,"tests/pirates-review-gates.json"),"utf8"));
async function command(h,text){
 const f=h.readDebugState().find(f=>f.name==='dbg.cmd');
 h.writeMemory(f.valuePtr,Buffer.from(text+'\0'));
 const seq=h.readDebugValue('dbg.cmd_seq').value+1;h.writeDebugValue('dbg.cmd_seq',seq);
 for(let i=0;i<600;i++) {await h.stepFrames(1);if(h.readDebugValue('dbg.reply_seq').value===seq)break;}
 assert.equal(h.readDebugValue('dbg.reply_seq').value,seq,'command timeout: '+text);
 const r=JSON.parse(Buffer.from(h.readDebugValue('dbg.reply').bytes).toString().split('\0')[0]);
 assert.ok(!r.error,JSON.stringify(r));return r;
}
const field=(h,k)=>{const v=h.readDebugValue(k);return v.values??v.value;};
function clean(h){assert.equal(field(h,'cw.gpu.errors'),0);assert.equal(field(h,'render.skipped'),0);}
function shot(h,name){writeFileSync(join(out,name+'.png'),Buffer.from(h.screenshot().pngBase64,'base64'));}
async function load(gpu,auto=false){const h=new WasmcartHost();const status=await h.loadMedia({platform:'wasmcart',path:join(root,'build/cart-wgpu/carts',auto?'pirates_auto.wasc':'pirates.wasc'),wgpu:gpu==='webgpu',gpuPower:pin.power,deterministic:{seed:1,stepMs:1000/60}});if(gpu==='webgpu')pin.check('Pirates review',JSON.stringify(status.gpuAdapter));return h;}
for(const gpu of ['gl','webgpu']) {
 let h=await load(gpu);
 try {
  await h.stepFrames(30);shot(h,gpu+'-title');
  // Join through four separate controller ports, not a debug match command.
  h.setInput({ports:[{}, {start:true}, {start:true}, {start:true}]});await h.stepFrames(1);
  h.setInput({ports:[{},{},{},{}]});await h.stepFrames(1);
  h.setInput({ports:[{start:true},{},{},{}]});await h.stepFrames(1);
  h.setInput({ports:[{},{},{},{}]});await h.stepFrames(2);
  assert.equal(field(h,'pirates.humans'),4);assert.equal(field(h,'pirates.state'),1);
  shot(h,gpu+'-four-controller-join');
  for(const review of reviewGates) {
   const map=review.map;
   await command(h,'pirates_map '+map);await command(h,'pirates_match 1 4');await h.stepFrames(8);
   const placement=await command(h,'pirates_placement');
   assert.ok(placement.props>50);assert.ok(placement.cargo>0);assert.equal(placement.ships,6);
   for(const [k,v] of Object.entries(review.expectPlacement))assert.equal(placement[k],v,JSON.stringify(placement));
   for(const q of review.presets) {
    await command(h,'quality '+q);await h.stepFrames(16);clean(h);shot(h,`${gpu}-map${map}-${q}`);
   }
   // Idle captains may learn controls at their home anchorage without patrol camping.
   await command(h,'quality high');await h.stepFrames(review.idleFrames);
   assert.deepEqual(field(h,'pirates.hp'),review.expectIdleHull);
   const late=await command(h,'pirates_placement');assert.equal(late.overlaps,0);
   results.push({gpu,map,placement,idleHp:field(h,'pirates.hp')});
  }
  const hull=await command(h,'pirates_test_hull');assert.equal(hull.spawns_clear,1);assert.equal(hull.no_penetration,1);
  assert.equal(hull.blocked,hull.cases,JSON.stringify(hull));assert.equal(hull.reversed_clear,hull.cases,JSON.stringify(hull));
  for(const text of ['pirates_test_escape','pirates_test_route'])assert.equal((await command(h,text)).banked,3);
  clean(h);console.log(gpu+': placement, four presets on both maps, harbor safety, hull and routes passed');
 } finally {h.destroy();}
 h=await load(gpu,true);
 try {
  const gates=JSON.parse(readFileSync(join(root,'tests/pirates-gates.json'),'utf8')).slice(0,2);
  let frames=1;
  for(const gate of gates) {
   if(gpu==='gl'&&gate.frames>1500)continue;
   const timing=[];
   while(frames<gate.frames){const n=Math.min(600,gate.frames-frames),t=performance.now();await h.stepFrames(n);frames+=n;timing.push({frame:frames,msPerFrame:(performance.now()-t)/n,rssMiB:process.memoryUsage().rss/1048576});if(frames%3600===0||frames===gate.frames)console.log(gpu,"replay tick",frames);}
   const state=Object.fromEntries(gate.fields.map(k=>[k,field(h,k)]));clean(h);
   if(!process.argv.includes("--capture"))assert.deepEqual(state,gate.expect,gate.name);
   // --capture writes evidence for independent native comparison before updating expectations.
   writeFileSync(join(out,`${gpu}-${gate.name}.json`),JSON.stringify({state,timing},null,2));
   shot(h,`${gpu}-${gate.name}`);console.log(gpu,gate.name,JSON.stringify(state));
  }
 } finally {h.destroy();}
}
writeFileSync(join(out,'checks.json'),JSON.stringify(results,null,2));
