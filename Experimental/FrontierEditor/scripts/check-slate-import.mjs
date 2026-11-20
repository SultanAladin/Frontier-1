/** Integration checks for the original-appearance Slate collection. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {execFileSync} from 'node:child_process';
import {JSDOM} from 'jsdom';
import {createCanvas,loadImage} from '@napi-rs/canvas';
const raw=JSON.parse(execFileSync('python3',['scripts/slate-reviewed-source.py'],{encoding:'utf8',maxBuffer:4*1024*1024}));
const layouts=JSON.parse(fs.readFileSync('vendor/slate-new-icons/layout.json','utf8'));
const selection=JSON.parse(fs.readFileSync('vendor/slate-new-icons/selection.json','utf8'));
const kept=raw.filter(a=>selection.includes(a.id));
assert.equal(kept.length,26);
const html=fs.readFileSync('icons.html','utf8');let copied='',downloaded=false;
const dom=new JSDOM(html,{url:'http://localhost:5174',runScripts:'dangerously',beforeParse(w){
 w.HTMLDialogElement.prototype.showModal=function(){this.open=true};w.HTMLDialogElement.prototype.close=function(){this.open=false};
 w.navigator.clipboard={writeText:async text=>{copied=text}};
 w.URL.createObjectURL=()=> 'blob:slate';w.URL.revokeObjectURL=()=>{};w.HTMLAnchorElement.prototype.click=function(){downloaded=this.download};
}});
const {document:d,DOMParser}=dom.window;const parser=new DOMParser();
assert.equal(d.querySelectorAll('.icon-card').length,141);
d.querySelector('[data-filter="Slate"]').click();assert.equal(d.querySelectorAll('.icon-card:not([hidden])').length,26);
for(const item of raw.filter(a=>!selection.includes(a.id))){
 assert.equal(d.querySelector(`[data-id="${item.id}"]`),null,item.name);
 for(const dir of ['custom-icons','dist/custom-icons'])assert.ok(!fs.existsSync(dir+'/'+item.id+'.svg'),item.name+' should be removed');
}
for(const item of kept){
 const svg=fs.readFileSync('custom-icons/'+item.id+'.svg','utf8');
 const source=parser.parseFromString(item.svg,'image/svg+xml'),asset=parser.parseFromString(svg,'image/svg+xml');
 assert.ok(!asset.querySelector('parsererror,script,image,foreignObject'),item.name);
 // Compare the exact reviewed source geometry, allowing layout-only wrappers.
 assert.equal(asset.querySelectorAll('[data-slate-layout]').length,2);
 const layout=layouts[item.id];assert.ok(layout.mainTransform[0]>1,item.name+' enlarged');
 for(const part of ['main','badge']){
  const matrix=layout[part+'Transform'];
  assert.equal(asset.querySelector(`[data-slate-layout="${part}"]`).getAttribute('transform'),'matrix('+matrix.join(' ')+')');
  const [x,y,w,h]=layout[part+'Bounds'],s=matrix[0],left=120+s*(x-120)+matrix[4],top=120+s*(y-120)+matrix[5];
  assert.ok(left>=16&&top>=16&&left+w*s<=221&&top+h*s<=219,item.name+' safe canvas bounds');
  if(part==='badge'){assert.ok(Math.abs(top+h*s-218)<.01);assert.ok(left<30||left+w*s>210);}
 }
 if(['slate-spotlight','slate-l-e-d-panel'].includes(item.id))assert.ok(![...asset.querySelectorAll('[data-slate-layout="main"] [transform]')].some(e=>e.getAttribute('transform').includes('rotate(')));
 if(item.id==='slate-spotlight'){
  const beam=[...asset.querySelectorAll('path')].find(e=>e.getAttribute('fill')==='url(#slate-spotlight--spotBeam)');
  assert.equal(beam.getAttribute('d'),'M -22 20 L 22 20 L 48 75 L -48 75 Z');
  assert.deepEqual(layout.mainTransform,[1.80095,0,0,1.80095,0,-25.26066]);
  const light=asset.querySelector('ellipse[rx="22"]');assert.equal(light.getAttribute('cy'),'20');assert.equal(light.getAttribute('ry'),'5');
 }

 const a=[...source.documentElement.querySelectorAll('*')];const b=[...asset.documentElement.querySelectorAll('*')].filter(e=>e.id!==item.id+'-title'&&!e.hasAttribute('data-slate-layout'));
 assert.equal(a.length,b.length,item.name);
 a.forEach((e,i)=>{
  assert.equal(e.localName,b[i].localName,item.name);
  assert.deepEqual([...e.attributes].map(a=>[a.name,a.value]).sort(),[...b[i].attributes].map(a=>[a.name,a.value.replaceAll(item.id+'--','')]).sort(),item.name+' '+e.localName);
 });
 for(const m of svg.matchAll(/url\(#([^\)]+)\)/g))assert.ok(asset.getElementById(m[1]),item.name+': '+m[1]);
 assert.equal(asset.documentElement.getAttribute('viewBox'),source.documentElement.getAttribute('viewBox'));
 for(const size of [32,96,240]){
  const c=createCanvas(size,size),ctx=c.getContext('2d');ctx.drawImage(await loadImage(Buffer.from(svg)),0,0,size,size);
  assert.ok(ctx.getImageData(0,0,size,size).data.some((v,i)=>i%4===3&&v>0),item.name);
 }
 const card=d.querySelector(`[data-id="${item.id}"]`);assert.equal(card.querySelector('b').textContent,item.name);
 card.click();assert.equal(d.querySelector('#detail-title').textContent,item.name);
 assert.equal(d.querySelector('.detail-meta span').textContent,'240 × 240 viewBox');
 assert.equal(d.querySelector('.detail-source').hidden,false);
 const ids=[...d.querySelectorAll('[id]')].map(e=>e.id);assert.equal(ids.length,new Set(ids).size,item.name+' global ID uniqueness');
}
d.querySelector('#copy').click();await new Promise(r=>setTimeout(r,0));assert.ok(copied.includes('http://www.w3.org/2000/svg'));assert.ok(copied.includes('slate-shader-node'));
d.querySelector('#download').click();assert.equal(downloaded,'frontier-slate-shader-node.svg');
d.querySelector('[data-id="editor-material"]').click();assert.equal(d.querySelector('.detail-source').hidden,true);assert.equal(d.querySelector('.detail-meta span').textContent,'256 × 256 viewBox');
assert.equal(fs.readFileSync('dist/icons.html','utf8'),html);
dom.window.close();console.log('PASS: 26 selected Slate SVGs match reviewed artwork, enlarge central art and place badges in bottom corners; render at 32/96/240px; 141 gallery entries; 76 rejected imports absent; category/inspect/copy/download, unique IDs, viewBox metadata and attribution verified.');
