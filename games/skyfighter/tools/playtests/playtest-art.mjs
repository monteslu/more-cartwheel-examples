// Matched deterministic art review, renderer safety and host CPU counters.
// SKY_ART_BACKEND=gl SKY_ART_LEVELS=0,3,4 node scripts/playtest-art.mjs
import {resolve} from 'node:path';
import {mkdirSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const root=resolve(import.meta.dirname,'../..'),out=resolve(root,'../../out/skyfighter-review/art');mkdirSync(out,{recursive:true});
const backend=process.env.SKY_ART_BACKEND||'webgpu',levels=(process.env.SKY_ART_LEVELS||'0,1,2,3,4,5,6,7').split(',').map(Number),report=[];
const headers={'content-type':'application/json','x-romdev-session':'sky-fighter-art-final-'+backend};
async function t(n,a){const r=await fetch('http://localhost:7331/tool/'+n,{method:'POST',headers,body:JSON.stringify(a)}),j=await r.json();if(!r.ok||j.error)throw Error(JSON.stringify(j));return j;}
const step=frames=>t('frame',{op:'step',frames}),write=(name,value)=>t('wasm',{op:'write',name,value}),read=async name=>(await t('wasm',{op:'read',name})).value,input=p=>t('input',{op:'set',ports:[p]}),command=text=>t('wasm',{op:'command',text});
for(const level of levels){
 await t('loadMedia',{platform:'wasmcart',path:resolve(root,'../../build/cart-wgpu/carts/skyfighter.wasc'),wgpu:backend==='webgpu',deterministicStepMs:16,deterministicSeed:17107});await step(25);
 assert.equal((await t('wasm',{op:'conformance'})).conforms,true);
 await write('mission.level',level);await input({south:true});await step(2);await input({});await step(3);assert.equal(await read('mission.state'),1);
 await step(60);await command('perf reset');await step(20);
 await t('frame',{op:'screenshot',scale:.65,outputPath:resolve(out,`final-${backend}-${level}.png`)});
 for(const key of ['cw.gpu.errors','render.skipped','cw.three.missing'])assert.equal(await read(key),0,`${backend} mission ${level} ${key}`);
 report.push({level,environment:(await command('environment')).reply,perf:(await command('perf 20')).reply,state:await t('wasm',{op:'debugState'})});
 if(backend==='webgpu'){await write('art.hide_hud',1);await step(1);await t('frame',{op:'screenshot',scale:.65,outputPath:resolve(out,`clean-${level}.png`)});}
 await t('host',{op:'unload'});console.log('PASS',backend,level);
}
writeFileSync(resolve(out,`final-${backend}.json`),JSON.stringify(report,null,2));
