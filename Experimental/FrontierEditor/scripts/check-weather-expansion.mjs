import assert from 'node:assert/strict';
import fs from 'node:fs';
import {JSDOM} from 'jsdom';
import {createCanvas,loadImage} from '@napi-rs/canvas';
const ids=['weather-rain','weather-snow','weather-hail','sky-scattering','environment-exposure'];
const dom=new JSDOM(fs.readFileSync('icons.html','utf8'),{runScripts:'dangerously',beforeParse(w){w.HTMLDialogElement.prototype.showModal=function(){this.open=true}}});
const d=dom.window.document;
assert.equal(d.querySelector('[data-id="weather-frost-ice"]'),null);
for(const dir of ['custom-icons','dist/custom-icons'])assert.ok(!fs.existsSync(dir+'/weather-frost-ice.svg'));
d.querySelector('[data-filter="Environment"]').click();
for(const id of ids){
 const raw=fs.readFileSync('custom-icons/'+id+'.svg','utf8');
 const svg=new dom.window.DOMParser().parseFromString(raw,'image/svg+xml');
 assert.ok(!svg.querySelector('parsererror,image,script,foreignObject'));
 if(id==='sky-scattering'){assert.ok(svg.getElementById('atmosphere-altitude'));assert.ok(!svg.querySelector('clipPath,[data-layer]')); assert.ok(!raw.includes('sky-scattering-sun'));}
 if(['weather-rain','weather-hail'].includes(id))assert.ok(!raw.includes('cloud'));
 for(const m of raw.matchAll(/url\(#([^\)]+)\)/g))assert.ok(svg.getElementById(m[1]),id+': '+m[1]);
 for(const size of [32,64,256]){
  const c=createCanvas(size,size),x=c.getContext('2d');x.drawImage(await loadImage(Buffer.from(raw)),0,0,size,size);
  const pixels=x.getImageData(0,0,size,size).data;assert.ok(pixels.some((v,i)=>i%4===3&&v>0));assert.equal(pixels[3],0);
  if(size===256&&process.env.PREVIEW_WEATHER){const p=createCanvas(320,320),ctx=p.getContext('2d');ctx.fillStyle='#202020';ctx.fillRect(0,0,320,320);ctx.drawImage(c,32,32);fs.writeFileSync('/tmp/'+id+'.png',p.toBuffer('image/png'));}
 }
 const card=d.querySelector(`[data-id="${id}"]`);assert.ok(card&&!card.hidden);card.click();
 assert.equal(d.querySelector('.detail-meta span').textContent,'256 × 256 viewBox');
 const all=[...d.querySelectorAll('[id]')].map(e=>e.id);assert.equal(all.length,new Set(all).size);
 assert.equal(raw,fs.readFileSync('dist/custom-icons/'+id+'.svg','utf8'));
}
dom.window.close();console.log('PASS: five weather/environment icons, native references, 32/64/256 renders, transparency, category/inspection, unique IDs and production exports.');
