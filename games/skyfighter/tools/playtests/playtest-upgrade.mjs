import {resolve} from 'node:path';
import {mkdirSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/engine-upgrade');mkdirSync(out,{recursive:true});
const report={},impactsOnly=!!process.env.SKY_UPGRADE_IMPACTS_ONLY;
for(const wgpu of (process.env.SKY_UPGRADE_BACKEND?[process.env.SKY_UPGRADE_BACKEND==='webgpu']:[false,true])) {
 const backend=wgpu?'webgpu':'gl',headers={'content-type':'application/json','x-romdev-session':'sky-fighter-upgrade-'+backend};
 async function t(name,args){const response=await fetch('http://127.0.0.1:7331/tool/'+name,{method:'POST',headers,body:JSON.stringify(args)});const r=await response.json();if(!response.ok||r.error)throw Error(name+': '+JSON.stringify(r));return r;}
 const step=frames=>t('frame',{op:'step',frames}),input=p=>t('input',{op:'set',ports:[p]}),read=async name=>(await t('wasm',{op:'read',name})).value,write=(name,value)=>t('wasm',{op:'write',name,value});
 async function pulse(button){await input({[button]:true});await step(2);await input({});await step(3);}
 async function set(v){for(const [name,value] of Object.entries(v))await write(name,value);}
 async function launch(level){await write('mission.level',level);await pulse('south');await write('mission.spawn',10000);for(let i=0;i<7;i++)await write(`enemy.${i}.active`,0);await step(5);assert.equal(await read('mission.state'),1);}
 async function menu(){await pulse('start');await pulse('down');await pulse('down');await pulse('south');assert.equal(await read('mission.state'),0);}
 async function safe(){assert.equal(await read('cw.gpu.errors'),0);assert.equal(await read('render.skipped'),0);assert.equal(await read('cw.three.missing'),0);}
 await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu,deterministicStepMs:16,deterministicSeed:17107});await step(25);assert.equal((await t('wasm',{op:'conformance'})).conforms,true);
 await write('forest.grid',4);await launch(0);assert.equal(await read('forest.trees'),2200);const initialMeshes=await read('scenery.meshes'),views=[];
 if(!impactsOnly)for(const heading of [0,Math.PI/2,Math.PI,-Math.PI/2]) {
  await set({'jet.x':0,'jet.z':0,'jet.altitude':600,'jet.pitch':0,'jet.roll':0,'jet.heading':heading});await step(15);
  const visible=await read('scenery.visible'),culled=await read('scenery.culled'),meshes=await read('scenery.meshes');assert(meshes>30,'forest cells registered with VisibilityManager');assert(culled>0&&visible>0,'both visible and culled cells');views.push({heading,meshes,visible,culled});await safe();
 }
 if(!impactsOnly)await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,backend+'-forest.png')});
 await menu();await launch(2);
 const bx=await read('building.44.x'),by=await read('building.44.y'),bz=await read('building.44.z');
 await set({'jet.x':bx,'jet.z':bz+450,'jet.altitude':by,'jet.heading':0,'jet.pitch':0,'jet.roll':0});
 await input({south:true});await step(35);await input({});await step(3);
 assert(await read('building.44.hp')>0,'ordinary bullet test must leave the structure alive');
 assert(await read('impact.builds')>0,'cannon impact stamps a surviving named structure');
 const bulletTriangles=await read('impact.triangles');assert(bulletTriangles>0,'bullet mark clips real receiving geometry');
 await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,backend+'-bullet.png')});
 await input({south:true});await step(55);await input({});await step(3);assert.equal(await read('building.44.hp'),0);

 const decals=await read('impact.decals'),triangles=await read('impact.triangles'),scorchTriangles=await read('impact.scorch_triangles');assert(decals>0&&triangles>0&&scorchTriangles>0,'destroyed foundation receives lit mesh scorch');await safe();
 await t('frame',{op:'screenshot',scale:.5,outputPath:resolve(out,backend+'-scorch.png')});
 if(!impactsOnly){await menu();await launch(1);assert.equal(await read('forest.trees'),4200);await step(20);await safe();assert.equal(await read('impact.decals'),0,'sortie clears old decals');
 await menu();await launch(0);await step(20);assert.equal(await read('forest.trees'),2200);assert.equal(await read('scenery.meshes'),initialMeshes,'inactive cached missions do not inflate visibility counts');await safe();}
 report[backend]={views,decals,triangles,bulletTriangles,scorchTriangles,missionSwitches:3,events:await t('wasm',{op:'events'})};console.log(backend,'PASS',JSON.stringify({views,decals,triangles,bulletTriangles,scorchTriangles}));
 await t('host',{op:'unload'});
}
writeFileSync(resolve(out,impactsOnly?'impact-report.json':'report.json'),JSON.stringify(report,null,2));
