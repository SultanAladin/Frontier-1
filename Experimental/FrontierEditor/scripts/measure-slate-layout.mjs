/** Fit artwork independently of overlay badges; preserve source geometry. */
import fs from 'node:fs';
import {execFileSync} from 'node:child_process';
import {JSDOM} from 'jsdom';
import {createCanvas,loadImage} from '@napi-rs/canvas';
const selected=JSON.parse(fs.readFileSync('vendor/slate-new-icons/selection.json','utf8'));
const assets=JSON.parse(execFileSync('python3',['scripts/slate-reviewed-source.py'],{encoding:'utf8',maxBuffer:4*1024*1024})).filter(a=>selected.includes(a.id));
async function bounds(asset,badgeOnly){
 const dom=new JSDOM(asset.svg,{contentType:'image/svg+xml'}),svg=dom.window.document.documentElement;
 const scene=[...svg.children].find(e=>e.localName==='g'),badge=scene.lastElementChild;
 if(badge.localName!=='g'||!badge.hasAttribute('filter'))throw Error(asset.id+' unexpected badge');
 if(badgeOnly){for(const n of [...scene.children])if(n!==badge)n.remove();}else badge.remove();
 for(const e of [svg,...svg.querySelectorAll('*')]){e.removeAttribute('filter');e.removeAttribute('class');if(e.style)e.style.filter='';}
 const c=createCanvas(480,480),ctx=c.getContext('2d');ctx.drawImage(await loadImage(Buffer.from(svg.outerHTML)),0,0,480,480);
 const data=ctx.getImageData(0,0,480,480).data;let x0=480,y0=480,x1=0,y1=0;
 for(let y=0;y<480;y++)for(let x=0;x<480;x++)if(data[(y*480+x)*4+3]>20){x0=Math.min(x0,x);y0=Math.min(y0,y);x1=Math.max(x1,x+1);y1=Math.max(y1,y+1);}
 dom.window.close();return [x0/2,y0/2,(x1-x0)/2,(y1-y0)/2];
}
const layout={};
for(const asset of assets){
 const main=await bounds(asset,false),badge=await bounds(asset,true),scale=Math.min(192/main[2],190/main[3]);
 const x=(240-main[2]*scale)/2,y=20+(190-main[3]*scale)/2;
 const bs=46/Math.max(badge[2],badge[3]),side=badge[0]+badge[2]/2<120?'left':'right';
 const bx=side==='left'?22:218-badge[2]*bs,by=218-badge[3]*bs;
 const matrix=(b,s,x,y)=>[s,0,0,s,x-120-s*(b[0]-120),y-120-s*(b[1]-120)].map(n=>+n.toFixed(5));
 layout[asset.id]={side,mainBounds:main,badgeBounds:badge,mainTransform:matrix(main,scale,x,y),badgeTransform:matrix(badge,bs,bx,by)};
}
// Retain the previously approved light beam and lantern framing from the design session.
layout['slate-spotlight'].mainTransform=[1.80095,0,0,1.80095,0,-25.26066];
layout['slate-lantern'].mainTransform=[1.29252,0,0,1.29252,0,-1.12245];
fs.writeFileSync('vendor/slate-new-icons/layout.json',JSON.stringify(layout,null,2)+'\n');
console.log('Measured '+assets.length+' Slate layouts.');
