import assert from 'node:assert/strict';
import fs from 'node:fs';
import {JSDOM} from 'jsdom';
import {createCanvas,loadImage} from '@napi-rs/canvas';
const dom=new JSDOM(fs.readFileSync('icons.html','utf8'),{runScripts:'dangerously',beforeParse(w){w.HTMLDialogElement.prototype.showModal=function(){this.open=true}}});
const d=dom.window.document,parser=new dom.window.DOMParser();
const files=fs.readdirSync('custom-icons').filter(n=>n.startsWith('asset-'));
assert.equal(files.length,32);
for(const ext of ['ttf','otf','woff','woff2'])assert.ok(files.includes('asset-file-'+ext+'.svg'));
assert.equal(files.filter(n=>n.startsWith('asset-folder-')).length,12);
d.querySelector('[data-filter="Files & folders"]').click();assert.equal(d.querySelectorAll('.icon-card:not([hidden])').length,32);
for(const filename of files){
 const raw=fs.readFileSync('custom-icons/'+filename,'utf8');const svg=parser.parseFromString(raw,'image/svg+xml');
 assert.ok(!svg.querySelector('parsererror,image,foreignObject,script,linearGradient'));
 if(filename.startsWith('asset-folder-')){const label=svg.querySelector('text');assert.ok(label?.textContent.trim(),filename+' printed name');assert.equal(label.getAttribute('y'),'178');const meta=svg.querySelectorAll('text')[1];assert.ok(/^[\d.]+ [KMG]B · \d+ items$/.test(meta.textContent));assert.equal(meta.getAttribute('y'),'194');assert.ok(+meta.getAttribute('font-size') < +label.getAttribute('font-size'));}
 for(const m of raw.matchAll(/url\(#([^\)]+)\)/g))assert.ok(svg.getElementById(m[1]),filename+': '+m[1]);
 for(const size of [32,64,256]){const c=createCanvas(size,size),ctx=c.getContext('2d');ctx.drawImage(await loadImage(Buffer.from(raw)),0,0,size,size);const px=ctx.getImageData(0,0,size,size).data;assert.ok(px.some((v,i)=>i%4===3&&v>0));assert.equal(px[3],0,'transparent exterior');}
 const id=filename.slice(0,-4);d.querySelector(`[data-id="${id}"]`).click();assert.equal(d.querySelector('.detail-meta span').textContent,'256 × 256 viewBox');
 const ids=[...d.querySelectorAll('[id]')].map(e=>e.id);assert.equal(ids.length,new Set(ids).size);
 assert.equal(raw,fs.readFileSync('dist/custom-icons/'+filename,'utf8'));
}
dom.window.close();console.log('PASS: 12 folders + 20 formats, native SVG, isolated IDs, transparency, 32/64/256 renders, filtering, inspection and production exports.');
