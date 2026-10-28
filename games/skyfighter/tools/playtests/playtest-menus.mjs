import{resolve}from'node:path';import{mkdirSync,writeFileSync}from'node:fs';import assert from'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/menus');mkdirSync(out,{recursive:true});const headers={'content-type':'application/json','x-romdev-session':'sky-fighter-simple-menus'};
async function t(n,a){const r=await(await fetch('http://127.0.0.1:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)})).json();if(r.error)throw Error(JSON.stringify(r));return r;}
const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value,write=(name,value)=>t('wasm',{op:'write',name,value});
async function pulse(button){await input({[button]:true});await step(2);await input({});await step(2);}
async function click(x,y){await t('input',{op:'pointer',x,y,left:true});await step(2);await t('input',{op:'pointer',x,y,left:false});await step(2);}
async function shot(name){await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,name+'.png')});}
await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),deterministicSeed:17107,deterministicStepMs:1000/60});await step(25);await shot('01-mission-grid');
await pulse('down');assert.equal(await read('mission.level'),4);await pulse('right');assert.equal(await read('mission.level'),5);await pulse('up');assert.equal(await read('mission.level'),1);await pulse('left');assert.equal(await read('mission.level'),0);
await pulse('west');assert.equal(await read('mission.state'),7);await shot('02-controls');await pulse('east');assert.equal(await read('mission.state'),0);
// Pointer coordinates are framebuffer pixels; the runtime maps them to the UI canvas.
await t('input',{op:'pointer',x:640,y:400,left:false});await step(2);const pointer=(await t('wasm',{op:'read',name:'menu.pointer'})).values;const scale=640/pointer[0];assert(scale===1||scale===2);
await click(Math.round(660*scale),Math.round(485*scale));assert.equal(await read('mission.state'),5);await shot('03-click-settings');const before=await t('wasm',{op:'save'});await click(Math.round(650*scale),Math.round(174*scale));const after=await t('wasm',{op:'save'});assert.notEqual(before.hex,after.hex,'clicking pitch changes saved setting');await click(Math.round(290*scale),Math.round(465*scale));assert.equal(await read('mission.state'),0);
const launched=[];for(let i=0;i<8;i++){
 const x=46+(i%4)*220+104,y=120+Math.floor(i/4)*132+60;await click(Math.round(x*scale),Math.round(y*scale));assert.equal(await read('mission.state'),1);assert.equal(await read('mission.level'),i);assert.equal(await read('mission.missiles'),18);launched.push(i);
 await pulse('start');assert.equal(await read('mission.state'),2);if(i===0){await shot('04-pause-buttons');await pulse('east');assert.equal(await read('mission.state'),1,'B resumes');await pulse('start');}
 await pulse('down');await pulse('down');assert.equal(await read('menu.row'),2);await pulse('south');assert.equal(await read('mission.state'),0);
}
await shot('05-all-missions-selectable');const saved=await t('wasm',{op:'save'});await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc')});await step(25);assert.equal(await read('mission.level'),7,'chosen mission survives reload');assert.equal((await t('wasm',{op:'save'})).hex,saved.hex);assert.equal(await read('cw.gpu.errors'),0);
const report={allEightClickable:true,launched,gridNavigation:true,controlsPage:true,clickSettingsSave:true,pauseButtons:true,selectedMissionSaved:true,pointerScale:scale,gpuErrors:0};writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));console.log('PASS: visible mission grid, click-to-play, pad navigation, pause actions and saved selection',report);
