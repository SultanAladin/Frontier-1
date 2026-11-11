import React, {useEffect, useId, useState, useRef} from 'react';

function useAnimatedValue(value) {
 const [display,setDisplay]=useState(value);
 useEffect(()=>{
  if(window.matchMedia?.('(prefers-reduced-motion: reduce)').matches){setDisplay(value);return;}
  let frame,previous;const animate=time=>{const dt=previous?Math.min(time-previous,64):16;previous=time;
   setDisplay(current=>{const next=current+(value-current)*(1-Math.exp(-dt/65));return Math.abs(next-value)<.01?value:next;});
   frame=requestAnimationFrame(animate);
  };frame=requestAnimationFrame(animate);const stop=setTimeout(()=>{cancelAnimationFrame(frame);setDisplay(value);},360);
  return ()=>{cancelAnimationFrame(frame);clearTimeout(stop);};
 },[value]);return display;
}

export function Iris({opening,label='Lens iris'}) {
 const size=useAnimatedValue(opening),id=useId().replace(/:/g,'');
 const point=(angle,r)=>[110+Math.cos(angle)*r,110+Math.sin(angle)*r];
 const radius=10+size*47;
 return <svg className="iris-graphic" viewBox="0 0 220 220" role="img" aria-label={label} data-opening={opening.toFixed(3)}>
 <defs><radialGradient id={id}><stop stopColor="#434047"/><stop offset="1" stopColor="#232225"/></radialGradient></defs>
 <circle cx="110" cy="110" r="97" fill="#171717" stroke="#525052"/>
 <circle cx="110" cy="110" r="88" fill={`url(#${id})`} stroke="#454247"/>
 {Array.from({length:48},(_,i)=><path key={i} d="M110 16 V20" transform={`rotate(${i*7.5} 110 110)`} stroke="#7a747e" strokeWidth=".6"/>)}
 {Array.from({length:8},(_,i)=>{let a=i*Math.PI/4;let p=point(a,radius),q=point(a+Math.PI/4,radius),o=point(a+1.07,86),n=point(a+.29,86);return <path key={i} d={`M${p} L${q} L${o} A86 86 0 0 0 ${n}Z`} fill={i%2?'#39363e':'#343238'} stroke="#79717e" strokeWidth=".65"/>})}
 <polygon points={Array.from({length:8},(_,i)=>point(i*Math.PI/4,radius).join(',')).join(' ')} fill="#101014" stroke="#b7a7c4" strokeWidth="1"/>
 <circle cx="110" cy="110" r={radius*.55} fill="#9690b607"/>
 <circle cx="110" cy="110" r="2" fill="#c1b6cd" opacity=".6"/>
 </svg>;
}

// Smooth, hand-shaped sky illustration; intentionally not a density/render preview.
export function CloudCoverage({coverage,thickness}) {
 const id=useId().replace(/:/g,''),amount=Math.max(0,Math.min(100,coverage))/100;
 const scale=.25+Math.sqrt(amount)*1.15,shade=Math.min(.65,.16+thickness*.065);
 const banks=[
  {x:110,y:70,s:.75,d:'M-92 22C-112 10-103-14-80-15C-82-43-43-55-24-34C-7-71 48-60 53-24C80-36 113-10 97 14C120 26 102 43 75 42H-63C-81 42-99 38-92 22Z'},
  {x:460,y:60,s:.6,d:'M-92 27C-112 8-93-18-70-14C-67-49-23-62-2-34C26-53 60-36 61-10C94-22 115 9 94 29C87 45 56 46 34 43H-61C-80 43-89 39-92 27Z'},
  {x:330,y:156,s:1.15,d:'M-102 24C-123 3-99-24-77-20C-77-52-38-66-16-42C-2-88 57-77 64-37C91-50 117-25 111-3C138 0 144 32 118 43C88 52-69 52-91 46C-111 43-116 32-102 24Z'},
  {x:616,y:174,s:.95,d:'M-92 27C-112 8-93-18-70-14C-67-49-23-62-2-34C26-53 60-36 61-10C94-22 115 9 94 29C87 45 56 46 34 43H-61C-80 43-89 39-92 27Z'}
 ];
 return <div className="coverage-field cloud-sky-view"><svg viewBox="0 0 640 240" role="img" aria-label={`Illustrative sky view: ${coverage}% cloud coverage, ${thickness} km layer thickness`}>
 <defs>
 <linearGradient id={id+'sky'} x1="0" y1="0" x2="0" y2="1"><stop stopColor="#243749"/><stop offset="1" stopColor="#627b8c"/></linearGradient>
 <linearGradient id={id+'cloud'} x1="0" y1="0" x2=".12" y2="1"><stop stopColor="#edf1f2"/><stop offset=".48" stopColor="#c9d5de"/><stop offset="1" stopColor="#8097ac"/></linearGradient>
 <linearGradient id={id+'shade'} x1="0" y1="0" x2="0" y2="1"><stop offset=".35" stopColor="#546b83" stopOpacity="0"/><stop offset="1" stopColor="#455f78" stopOpacity={shade}/></linearGradient>
 <clipPath id={id+'frame'}><rect width="640" height="240" rx="14"/></clipPath>
 </defs>
 <g clipPath={`url(#${id}frame)`}><rect width="640" height="240" fill={`url(#${id}sky)`}/>
 <rect width="640" height="240" fill={`url(#${id}cloud)`} opacity={Math.max(0,(amount-.65)/.35)}/>
 {amount>0&&banks.map((bank,i)=><g key={i} transform={`translate(${bank.x} ${bank.y}) scale(${scale*bank.s})`} opacity={Math.min(1,amount*8)}><path d={bank.d} fill={`url(#${id}cloud)`}/><path d={bank.d} fill={`url(#${id}shade)`}/></g>)}
 </g></svg><div className="graphic-caption"><span>SKY COVER · ILLUSTRATIVE</span><span>{Math.round(100-coverage)}% clear sky</span></div></div>;
}

export function CloudAltitude({base,thickness,onChange}) {
 const id=useId().replace(/:/g,''),drag=useRef(false),top=base+thickness,toY=h=>215-h*11;
 const yBase=toY(base),yTop=toY(top),depth=thickness*11;
 const update=e=>{if(!onChange)return;const b=e.currentTarget.getBoundingClientRect(),scale=Math.min(b.width/400,b.height/260);if(!scale)return;const y=(e.clientY-b.top-(b.height-260*scale)/2)/scale;onChange(Math.max(.2,Math.min(10,Math.round((215-y)/11*10)/10)));};
 return <div className="cloud-altitude-section detailed-cloud-altitude"><svg viewBox="0 0 400 260" role="img" aria-label={`Cloud layer from ${base.toFixed(1)} to ${top.toFixed(1)} kilometers above ground, ${thickness.toFixed(1)} kilometers thick. Drag cloud base to change altitude.`}
 onPointerDown={e=>{e.currentTarget.setPointerCapture(e.pointerId);drag.current=true;update(e)}} onPointerMove={e=>drag.current&&update(e)} onPointerUp={()=>drag.current=false} onPointerCancel={()=>drag.current=false}>
 <defs><linearGradient id={id} x2="0" y2="1"><stop stopColor="#9baeca" stopOpacity=".1"/><stop offset=".35" stopColor="#c2d0e3" stopOpacity=".38"/><stop offset="1" stopColor="#7790b4" stopOpacity=".12"/></linearGradient><clipPath id={id+'layer'}><rect x="75" y={yTop} width="214" height={depth} rx="3"/></clipPath><pattern id={id+'earth'} width="6" height="6" patternUnits="userSpaceOnUse"><path d="M0 6 L6 0" stroke="#7b766c" strokeWidth=".6"/></pattern></defs>
 <rect x="57" y="25" width="247" height="191" rx="9" fill="#191d23" stroke="#ffffff09"/>
 {[0,2,4,6,8,10,12,14,16].map(h=><g key={h}><text x="42" y={toY(h)+3} textAnchor="end" fill={h%4===0?'#9cabc0':'#5c6a7c'} fontSize="9">{h}</text><path d={`M57 ${toY(h)} H304`} stroke={h%4===0?'#b4c6dd16':'#b4c6dd08'} strokeDasharray="2 5"/></g>)}
 <text x="21" y="14" fill="#718097" fontSize="8">km AGL</text>
 <rect x="75" y={yTop} width="214" height={depth} fill={`url(#${id})`}/>
 <g clipPath={`url(#${id}layer)`}>{Array.from({length:7},(_,i)=><path key={i} d={`M68 ${yTop+depth*(i+.5)/7} C118 ${yTop+depth*(i-.5)/7} 155 ${yTop+depth*(i+1.1)/7} 190 ${yTop+depth*i/7} S265 ${yTop+depth*(i+1)/7} 300 ${yTop+depth*i/7}`} fill="none" stroke="#dce6f3" strokeOpacity={.07+i%3*.04} strokeWidth="3"/>)}</g>
 <path d={`M65 ${yTop} H311`} stroke="#91a7c3" strokeDasharray="3 4" strokeWidth=".8"/>
 <path d={`M65 ${yBase} H310`} stroke="#c5d9f3" strokeWidth="1.4"/>
 <path d={`M308 ${yTop} H314 V${yBase} H308`} fill="none" stroke="#7d92b0" strokeWidth=".7"/>
 <circle cx="287" cy={yBase} r="6" fill="#243246" stroke="#d6e7ff"/>
 <path d={`M285 ${yBase} H289`} stroke="#d6e7ff"/>
 <text x="322" y={yTop-6} fill="#879bb6" fontSize="8" letterSpacing=".8">TOP</text><text x="322" y={yTop+7} fill="#b2c1d5" fontSize="11">{top.toFixed(1)} km</text>
 <text x="322" y={Math.max(yBase+17,yTop+27)} fill="#9bb5d7" fontSize="8" letterSpacing=".8">BASE</text><text x="322" y={Math.max(yBase+30,yTop+40)} fill="#d5e3f5" fontSize="11">{base.toFixed(1)} km</text>
 <rect x="57" y="216" width="247" height="8" fill={`url(#${id}earth)`}/><path d="M57 215 H304" stroke="#9b9589" strokeWidth=".8"/>
 <text x="57" y="244" fill="#778393" fontSize="8" letterSpacing=".7">GROUND · 0 m</text><text x="185" y="244" fill="#8fabc9" fontSize="8">Drag base to adjust altitude</text>
 </svg></div>;
}

export function TerrainContours({altitude,roughness,erosion}) {
 const rings=Array.from({length:12},(_,i)=>{const points=Array.from({length:97},(_,j)=>{const a=j/96*Math.PI*2;const r=(14+i*9)*Math.sqrt(altitude/500);const detail=(roughness/100)*(1-erosion/180);const irregular=1+detail*(.2*Math.sin(a*3+i*.15)+.1*Math.cos(a*7));return `${310+Math.cos(a)*r*1.85*irregular},${118+Math.sin(a)*r*.76*irregular}`});return points.join(' ')});
 return <div className="contour-diagram"><svg viewBox="0 0 620 238" role="img" aria-label="Terrain elevation contour diagram">
 <path d="M20 118 H600 M310 10 V225" stroke="#ffffff0c" strokeDasharray="3 5"/>
 {rings.map((points,i)=><polygon key={i} points={points} fill={i===0?'#d0ba9322':'none'} stroke={i%3===0?'#c3af8a':'#7e7360'} strokeOpacity={.85-i*.035} strokeWidth={i%3===0?1:.6}/>) }
 <path d="M305 118 H315 M310 113 V123" stroke="#e3c79b"/>
 <text x="325" y="113" fontSize="10" fill="#d3bea0">{altitude} m</text>
 <text x="22" y="218" fontSize="9" fill="#858078">CONTOUR INTERVAL</text><text x="132" y="218" fontSize="9" fill="#c3b498">{Math.max(1,Math.round(altitude/12))} m</text>
 </svg></div>;
}

export function RoughnessProfile({roughness}) {
 const points=Array.from({length:100},(_,i)=>`${i*4},${65-(Math.sin(i*.17)*.4+Math.sin(i*.72)*.24+Math.sin(i*1.7)*.1)*roughness*.65}`).join(' ');
 return <svg className="roughness-profile" viewBox="0 0 400 125" role="img" aria-label="Terrain surface roughness profile"><path d="M0 65 H400" stroke="#ffffff13" strokeDasharray="3 4"/><polygon points={`0,120 ${points} 400,120`} fill="#bfac9109"/><polyline points={points} stroke="#bca887" strokeWidth="1.3" fill="none"/></svg>;
}

export function ErosionChannels({erosion}) {
 return <svg className="erosion-channels" viewBox="0 0 400 125" role="img" aria-label="Erosion drainage channels"><path d="M185 6 C165 38 240 65 202 120" fill="none" stroke="#aa9a7b" strokeWidth={1+erosion/20}/>{[0,1,2,3,4,5,6,7].map((n)=><path key={n} d={`M${n%2?300+n*9:65+n*7} ${n*11+3} Q${n%2?245:130} ${n*12+4} ${190+Math.sin(n)*12} ${n*12+22}`} fill="none" stroke="#a99a80" strokeWidth={.4+erosion/75} opacity={.2+erosion/140}/>)}</svg>;
}
