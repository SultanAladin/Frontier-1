import React,{useEffect,useRef,useState} from 'react';
import {Orbit, MoveHorizontal, Sparkles, ArrowUpRight, Check, Circle, Hexagon, Octagon, Download, RotateCcw, Minus, Plus} from 'lucide-react';
import {Sun} from './celestial-icons.jsx';

export const flareDefaults={flareAnamorphic:true,flareStreaks:false,flareStarburst:true,flareRotation:0,flareRays:8,ghostShape:'hexagon',ghostGain:35,ghostSpacing:65,flareX:.28,flareY:.43};

// Render light elements into an RGBA image, just like a procedural sprite atlas.
// The canvas remains transparent so exports can be composited over a scene.
export function renderFlare(ctx,w,h,p){
 ctx.clearRect(0,0,w,h);ctx.save();ctx.globalCompositeOperation='lighter';
 const x=p.x*w,y=p.y*h,g=p.gain/100,s=.35+p.spread/100;
 const glow=(cx,cy,rx,ry,color,alpha,rotation=0)=>{
  if(alpha<=0||rx<=0||ry<=0)return;
  ctx.save();ctx.translate(cx,cy);ctx.rotate(rotation);ctx.scale(rx,ry);
  const grad=ctx.createRadialGradient(0,0,0,0,0,1);
  grad.addColorStop(0,`rgba(${color},${Math.min(1,alpha)})`);grad.addColorStop(.1,`rgba(${color},${Math.min(1,alpha*.62)})`);grad.addColorStop(.35,`rgba(${color},${alpha*.2})`);grad.addColorStop(.7,`rgba(${color},${alpha*.035})`);grad.addColorStop(1,`rgba(${color},0)`);
  ctx.fillStyle=grad;ctx.fillRect(-1,-1,2,2);ctx.restore();
 };
 if(p.flare&&g>0){
  glow(x,y,155*s,155*s,'225,154,93',g*.4);
  glow(x,y,62*s,62*s,'246,199,131',g*.9);
  if(p.anamorphic){
   glow(x,y,w*s*.9,11*s,'81,150,255',g*.95);
   glow(x,y,w*s*.7,2.1,'154,210,255',g*1.4);
   glow(x-w*.21*s,y,65*s,8*s,'40,115,245',g*.65);
   glow(x+w*.21*s,y,65*s,8*s,'40,115,245',g*.65);
   glow(x,y,14*s,75*s,'81,136,216',g*.12);
  }
  if(p.streaks){
   const a=p.rotation*Math.PI/180;
   glow(x,y,w*.66*s,2.8,'255,206,139',g,a);
   glow(x,y,w*.4*s,8,'237,170,101',g*.4,a);
   glow(x,y,w*.42*s,1.1,'215,236,255',g*.8,a+.028);
  }
  if(p.starburst){
   for(let i=0;i<p.rays;i++){
    const a=i*Math.PI/p.rays+p.rotation*Math.PI/180;
    glow(x,y,(75+(i%3)*23)*s,1.2+(i%2)*.6,'255,220,162',g*.8,a);
   }
  }
  glow(x,y,16*s,16*s,'255,243,222',Math.min(1,g*2));
  glow(x,y,4.5,4.5,'255,255,255',1.2*g);
 }
 if(p.ghosts&&p.count>0&&p.ghostGain>0){
  const colors=['115,207,159','136,164,240','241,180,120','175,133,221'];
  for(let i=0;i<p.count;i++){
   const f=.6+(i+1)/Math.max(1,p.count)*(1.7+p.spacing/50);
   const gx=x+(w/2-x)*f,gy=y+(h/2-y)*f;
   const r=(12+(i*19)%51)*(.5+s*.5),alpha=p.ghostGain/100*.38;
   const grad=ctx.createRadialGradient(gx,gy,0,gx,gy,r);
   grad.addColorStop(0,`rgba(${colors[i%4]},${alpha*.08})`);grad.addColorStop(.65,`rgba(${colors[i%4]},${alpha*.18})`);grad.addColorStop(.9,`rgba(${colors[i%4]},${alpha*.4})`);grad.addColorStop(1,`rgba(${colors[i%4]},0)`);
   ctx.beginPath();
   if(p.shape==='round')ctx.arc(gx,gy,r,0,Math.PI*2);
   else{const sides=p.shape==='hexagon'?6:8;for(let j=0;j<sides;j++){const angle=j/sides*Math.PI*2+Math.PI/6;const px=gx+Math.cos(angle)*r,py=gy+Math.sin(angle)*r;j?ctx.lineTo(px,py):ctx.moveTo(px,py)}ctx.closePath();}
   ctx.fillStyle=grad;ctx.fill();ctx.strokeStyle=`rgba(${colors[i%4]},${alpha*.65})`;ctx.lineWidth=.7;ctx.stroke();
   glow(gx,gy,r*1.15,r*1.15,colors[i%4],alpha*.12);
  }
 }
 ctx.restore();
}

export default function FlareInspector({card,v,set,slider,num,featureOn,active}){
 const canvas=useRef(null),dragging=useRef(false),[message,setMessage]=useState('');
 const params={gain:v('flareGain'),spread:v('flareSpread'),anamorphic:v('flareAnamorphic'),streaks:v('flareStreaks'),starburst:v('flareStarburst'),rotation:v('flareRotation'),rays:v('flareRays'),shape:v('ghostShape'),count:v('ghosts'),ghostGain:v('ghostGain'),spacing:v('ghostSpacing'),x:v('flareX'),y:v('flareY'),flare:active&&featureOn('Lens flare'),ghosts:active&&featureOn('Lens ghosts')};
 useEffect(()=>{const c=canvas.current,ctx=c?.getContext('2d');if(ctx)renderFlare(ctx,c.width,c.height,params);},[JSON.stringify(params)]);
 const update=e=>{const b=e.currentTarget.getBoundingClientRect();if(!b.width||!b.height)return;set('flareX',Math.max(.06,Math.min(.94,(e.clientX-b.left)/b.width)));set('flareY',Math.max(.08,Math.min(.92,(e.clientY-b.top)/b.height)));};
 const exportPNG=()=>{const c=document.createElement('canvas');c.width=1600;c.height=700;const ctx=c.getContext('2d');if(!ctx){setMessage('Image export is unavailable.');return;}ctx.scale(2.5,2.5);renderFlare(ctx,640,280,params);c.toBlob(blob=>{if(!blob){setMessage('Could not export this image.');return;}const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='frontier-lens-flare.png';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);setMessage('Exported transparent PNG · 1600 × 700');},'image/png');};
 return <>
 {card('Flare composite',Sun,<><div className="scattering-top"><div><div className="metric">{num('flareGain','%')}</div><p className="muted">Layered light sprites · additive compositing</p></div><button className="flare-export" onClick={exportPNG}><Download size={13}/>PNG</button></div><div className="flare-image-frame"><canvas ref={canvas} width="640" height="280" role="img" tabIndex={0} aria-label="Full lens flare composite image. Drag or use arrow keys to reposition the light source." onKeyDown={e=>{if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(e.key))return;e.preventDefault();const step=e.shiftKey?.1:.02;set('flareX',Math.max(.06,Math.min(.94,v('flareX')+(e.key==='ArrowRight'?step:e.key==='ArrowLeft'?-step:0))));set('flareY',Math.max(.08,Math.min(.92,v('flareY')+(e.key==='ArrowDown'?step:e.key==='ArrowUp'?-step:0))));}} onPointerDown={e=>{e.currentTarget.setPointerCapture(e.pointerId);dragging.current=true;update(e)}} onPointerMove={e=>dragging.current&&update(e)} onPointerUp={()=>dragging.current=false} onPointerCancel={()=>dragging.current=false} onLostPointerCapture={()=>dragging.current=false}/></div><div className="flare-image-caption"><span>Drag the light · ghosts follow the optical axis</span><button aria-label="Reset flare source position" onClick={()=>{set('flareX',.28);set('flareY',.43)}}><RotateCcw size={11}/>Recenter</button></div><div className="flare-main-controls"><label><span>Flare intensity<strong>{v('flareGain')}%</strong></span>{slider('flareGain',0,100)}</label><label><span>Spread<strong>{v('flareSpread')}%</strong></span>{slider('flareSpread',0,100)}</label></div><p className="flare-export-note" role="status">{message||'Procedural RGBA image · exports with transparency, without the dark background.'}</p></>,'wide-card flare-composite-card')}
 {card('Flare layers',Sparkles,<><p className="muted">Combine layers instead of choosing just one.</p><div className="flare-type-list">{[['flareAnamorphic','Anamorphic','Cool horizontal optical streak',MoveHorizontal],['flareStreaks','Streaks','Directional left-to-right light rays',ArrowUpRight],['flareStarburst','Starburst','Radial diffraction spikes',Sparkles]].map(([key,name,desc,Icon])=><button key={key} className={v(key)?'flare-type-on':''} aria-pressed={v(key)} onClick={()=>set(key,!v(key))}><Icon size={19}/><span>{name}<small>{desc}</small></span><i>{v(key)&&<Check size={11}/>}</i></button>)}</div><div className="control-line"><span>Streak / starburst rotation</span><span>{v('flareRotation')}°</span></div>{slider('flareRotation',0,180)}<div className="control-line current-bearing"><span>Starburst ray pairs</span><span>{v('flareRays')}</span></div>{slider('flareRays',2,12)}<p className="muted flare-layer-note">Anamorphic stays horizontal. Rotation affects the other two layers.</p></>)}
 {card('Lens ghosts',Orbit,<><div className="ghost-count-control"><label className="ghost-count-label"><input type="number" min="0" max="24" step="1" aria-label="Ghost element count" value={v('ghosts')} onChange={e=>{const n=e.target.valueAsNumber;if(Number.isFinite(n))set('ghosts',Math.max(0,Math.min(24,Math.round(n))))}}/><span>elements</span></label><div className="ghost-count-buttons"><button aria-label="Remove ghost element" disabled={v('ghosts')<=0} onClick={()=>set('ghosts',Math.max(0,v('ghosts')-1))}><Minus size={15}/></button><button aria-label="Add ghost element" disabled={v('ghosts')>=24} onClick={()=>set('ghosts',Math.min(24,v('ghosts')+1))}><Plus size={15}/></button></div></div><p className="muted">Aperture-shaped internal reflections</p><div className="ghost-shape-options" role="group" aria-label="Ghost shape">{[['round','Round',Circle],['hexagon','Hexagon',Hexagon],['octagon','Octagon',Octagon]].map(([key,label,Icon])=><button key={key} aria-pressed={v('ghostShape')===key} onClick={()=>set('ghostShape',key)}><Icon size={25} strokeWidth={1}/><span>{label}</span></button>)}</div>{slider('ghosts',0,24,1)}<div className="range-labels"><span>No ghosts</span><span>24 reflections</span></div><div className="control-line current-bearing"><span>Ghost brightness</span><span>{v('ghostGain')}%</span></div>{slider('ghostGain',0,100)}<div className="control-line current-bearing"><span>Spacing along optical axis</span><span>{v('ghostSpacing')}%</span></div>{slider('ghostSpacing',0,100)}</>)}
 </>;
}
