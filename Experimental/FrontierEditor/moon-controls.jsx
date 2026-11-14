import React,{useId,useRef} from 'react';

// Surface coordinates rotate in 3D; pitch never flattens the lunar silhouette.
export function MoonDisc({phase,size=.52,rotation=0,pitch=0,comparison=false,onRotate}){
 const id=useId().replace(/:/g,''),drag=useRef(null),fraction=phase/29.53,lit=(1-Math.cos(fraction*Math.PI*2))/2;
 const r=comparison?20+54*Math.sqrt(size/(size+12)):60,waxing=fraction<.5,rx=Math.max(.01,Math.abs(Math.cos(fraction*Math.PI*2))*r);
 const path=`M128 ${104-r} A${r} ${r} 0 0 ${waxing?1:0} 128 ${104+r} A${rx} ${r} 0 0 ${(waxing?fraction<.25:fraction>=.75)?(waxing?0:1):(waxing?1:0)} 128 ${104-r}Z`;
 const p=pitch*Math.PI/180,project=(x,y,z)=>[x,y*Math.cos(p)-z*Math.sin(p),y*Math.sin(p)+z*Math.cos(p)];
 const craters=Array.from({length:64},(_,i)=>{const y=1-2*(i+.5)/64,a=i*2.39996,t=Math.sqrt(1-y*y);const [x,v,z]=project(Math.cos(a)*t,y,Math.sin(a)*t);return {x:128+x*r,y:104-v*r,z,angle:Math.atan2(-v,x)*180/Math.PI,r:r*(.027+(i%5)*.008)};}).filter(c=>c.z>.025);
 const wrap=a=>((Math.round(a)%360)+360)%360,clamp=a=>Math.max(-180,Math.min(180,Math.round(a)));
 const key=e=>{if(!onRotate||!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home'].includes(e.key))return;e.preventDefault();const n=e.shiftKey?10:1;onRotate(e.key==='Home'?0:wrap(rotation+(e.key==='ArrowLeft'?-n:e.key==='ArrowRight'?n:0)),e.key==='Home'?0:clamp(pitch+(e.key==='ArrowUp'?n:e.key==='ArrowDown'?-n:0)));};
 return <svg className={`moon-disc-diagram${onRotate?' moon-orientation-dial':''}`} viewBox="0 0 256 215" role={onRotate?'group':'img'} tabIndex={onRotate?0:undefined} aria-label={`Moon disc ${size.toFixed(2)} degrees, ${Math.round(lit*100)} percent illuminated, roll ${rotation} degrees, pitch ${pitch} degrees${onRotate?'. Drag or use arrow keys to rotate.':''}`} onKeyDown={key}
 onPointerDown={onRotate?e=>{if(e.button!==0)return;e.currentTarget.focus();e.currentTarget.setPointerCapture(e.pointerId);drag.current={x:e.clientX,y:e.clientY,roll:rotation,pitch};}:undefined}
 onPointerMove={onRotate?e=>{if(drag.current){const d=drag.current;onRotate(wrap(d.roll+(e.clientX-d.x)*.8),clamp(d.pitch-(e.clientY-d.y)*.8));}}:undefined}
 onPointerUp={()=>drag.current=null} onPointerCancel={()=>drag.current=null} onLostPointerCapture={()=>drag.current=null}>
 <defs><radialGradient id={id+'body'} cx="30%" cy="25%"><stop stopColor="#dce2e4"/><stop offset=".65" stopColor="#a5b0bc"/><stop offset="1" stopColor="#596b81"/></radialGradient><clipPath id={id+'lit'}><path d={path}/></clipPath><clipPath id={id+'disc'}><circle cx="128" cy="104" r={r}/></clipPath></defs>
 {comparison&&<circle cx="128" cy="104" r="64" fill="none" stroke="#aab9cc22" strokeDasharray="2 5"/>}
 <g transform={`rotate(${rotation} 128 104)`}><circle cx="128" cy="104" r={r} fill="#202834" stroke="#8397b34d"/>
 <g clipPath={`url(#${id}lit)`}><circle cx="128" cy="104" r={r} fill={`url(#${id+'body'})`}/><g className="moon-surface-detail" clipPath={`url(#${id}disc)`}>{craters.map((c,i)=><ellipse key={i} cx={c.x} cy={c.y} rx={c.r*c.z} ry={c.r} transform={`rotate(${c.angle} ${c.x} ${c.y})`} fill="#4d5c70" fillOpacity=".35" stroke="#e4edf1" strokeOpacity=".2" strokeWidth=".55"/>)}</g></g>
 {onRotate&&<g clipPath={`url(#${id}disc)`}><path d={Array.from({length:121},(_,i)=>{const t=i*Math.PI/60,[x,y,z]=project(Math.cos(t),0,Math.sin(t));return `${z<0||i===0?'M':'L'}${128+x*r},${104-y*r}`;}).join(' ')} fill="none" stroke="#d3e2f0" strokeOpacity=".35" strokeWidth=".7" strokeDasharray="2 3"/></g>}
 <path d={`M128 ${100-r} V${94-r}`} stroke="#c5d9f2"/>
 </g>
 {comparison&&<path d={`M${128-r} 178v7m0-3h${r*2}m0-4v7`} stroke="#7d95b5" strokeWidth=".7"/>}
 <text x="128" y="202" textAnchor="middle" fill="#869bb6" fontSize="9">{comparison?`${size.toFixed(2)}° angular diameter`:`Roll ${rotation}° · Pitch ${pitch}°`}</text>
 </svg>;
}

export function LunarSphere({azimuth,elevation,rotation,size,onChange}){
 const id=useId().replace(/:/g,''),drag=useRef(null),R=85,cx=180,cy=117,tilt=.34;
 const project=(lon,lat)=>{let a=lon*Math.PI/180,b=lat*Math.PI/180,x=Math.sin(a)*Math.cos(b),z=Math.cos(a)*Math.cos(b),y=Math.sin(b);return [cx+R*x,cy-R*(y*Math.cos(tilt)-z*Math.sin(tilt)),z*Math.cos(tilt)+y*Math.sin(tilt)]};
 const [x,y,z]=project(azimuth,elevation);
 const trace=pts=>pts.map((p,i)=>`${i?'L':'M'}${p[0].toFixed(2)},${p[1].toFixed(2)}`).join(' ');
 const key=e=>{if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home'].includes(e.key))return;e.preventDefault();const d=e.shiftKey?10:1;onChange((azimuth+(e.key==='ArrowLeft'?-d:e.key==='ArrowRight'?d:0)+360)%360,e.key==='Home'?0:Math.max(-90,Math.min(90,elevation+(e.key==='ArrowUp'?d:e.key==='ArrowDown'?-d:0))));};
 return <svg className="lunar-spherical-dial" viewBox="0 0 360 257" tabIndex={0} role="group" aria-label={`Spherical lunar dial: azimuth ${azimuth} degrees, elevation ${elevation} degrees. Drag or use arrow keys.`} onPointerDown={e=>{e.currentTarget.setPointerCapture(e.pointerId);drag.current={x:e.clientX,y:e.clientY,a:azimuth,e:elevation}}} onPointerMove={e=>{if(drag.current){const d=drag.current;onChange((Math.round(((d.a+(e.clientX-d.x)*.7)%360+360)%360)%360),Math.round(Math.max(-90,Math.min(90,d.e-(e.clientY-d.y)*.65))))}}} onPointerUp={()=>drag.current=null} onPointerCancel={()=>drag.current=null} onLostPointerCapture={()=>drag.current=null} onKeyDown={key}>
 <defs><radialGradient id={id+'globe'} cx="28%" cy="24%" r="82%"><stop stopColor="#455263"/><stop offset=".6" stopColor="#252f3c"/><stop offset="1" stopColor="#151b25"/></radialGradient><radialGradient id={id+'moon'} cx="28%" cy="25%"><stop stopColor="#e3edf5"/><stop offset="1" stopColor="#8eaccd"/></radialGradient></defs>
 <circle cx={cx} cy={cy} r={R+17} fill="none" stroke="#91a9c022"/>
 {Array.from({length:36},(_,i)=><path key={i} d={`M180 15v${i%3===0?5:2}`} transform={`rotate(${i*10} 180 117)`} stroke="#8098b0" strokeOpacity=".45" strokeWidth=".7"/>)}
 <circle cx={cx} cy={cy} r={R} fill={`url(#${id+'globe'})`} stroke="#7897ba55"/>
 {[-60,-30,0,30,60].map(lat=><path key={lat} d={trace(Array.from({length:73},(_,i)=>project(i*5,lat)))} fill="none" stroke={lat===0?'#b5d5f0':'#7a99b5'} strokeOpacity={lat===0?.55:.2} strokeWidth={lat===0?1.1:.6}/>)}
 {[0,30,60,90,120,150].map(lon=><path key={lon} d={trace(Array.from({length:73},(_,i)=>project(lon,i*5)))} fill="none" stroke="#90afc7" strokeOpacity=".19" strokeWidth=".6"/>)}
 <path d={trace(Array.from({length:37},(_,i)=>project(azimuth,i*elevation/36)))} fill="none" stroke="#aacfeb" strokeWidth="1.4" strokeDasharray={z<0?'3 3':undefined}/>
 <line x1={cx} y1={cy} x2={x} y2={y} stroke="#c8def2" strokeOpacity=".35" strokeDasharray="2 4"/>
 <circle cx={x} cy={y} r="17" fill="#b6d2ed0c"/>
 <g transform={`rotate(${rotation} ${x} ${y})`}><circle cx={x} cy={y} r={6+Math.sqrt(Math.min(size,12))*3} fill={`url(#${id+'moon'})`} stroke="#d4e6f5" strokeWidth=".6"/><circle cx={x-2} cy={y+1} r="2" fill="#405d823d"/><path d={`M${x} ${y-11}v-4`} stroke="#c6dced" strokeWidth="1"/></g>
 <circle cx="180" cy="117" r="2" fill="#c5d8e5"/>
 <g fontSize="9" fill="#8ca2bd" textAnchor="middle"><text x="180" y="9">N</text><text x="68" y="121">W</text><text x="292" y="121">E</text><text x="180" y="231">S</text><text x="180" y="252">{z<0?'Far side · dashed guide':'Near side'} · drag to orbit</text></g>
 </svg>;
}
