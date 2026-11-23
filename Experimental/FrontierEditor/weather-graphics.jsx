import React, {useId, useRef} from 'react';

export function WindFlow({speed,bearing,gusts,active=true,onChange}) {
 const id=useId().replace(/:/g,'');
 const angle=bearing-90,dragging=useRef(false);
 const radius=speed/30*106,rad=bearing*Math.PI/180;
 const handleX=320+Math.sin(rad)*radius,handleY=150-Math.cos(rad)*radius;
 const updatePointer=e=>{
  if(!active||!onChange)return;
  const box=e.currentTarget.getBoundingClientRect();
  const scale=Math.min(box.width/640,box.height/300);
  if(!scale)return;
  const x=(e.clientX-box.left-(box.width-640*scale)/2)/scale-320;
  const y=(e.clientY-box.top-(box.height-300*scale)/2)/scale-150;
  const length=Math.hypot(x,y);
  onChange(length<3?bearing:Math.round((Math.atan2(x,-y)*180/Math.PI+360)%360),Math.round(Math.min(30,length/106*30)*10)/10);
 };
 const keyDown=e=>{
  if(!active||!onChange)return;
  if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home'].includes(e.key))return;
  e.preventDefault();const step=e.shiftKey?5:1;
  onChange((bearing+(e.key==='ArrowLeft'?-step:e.key==='ArrowRight'?step:0)+360)%360,e.key==='Home'?0:Math.max(0,Math.min(30,+(speed+(e.key==='ArrowUp'?step:e.key==='ArrowDown'?-step:0)).toFixed(1))));
 };
 return <div className={`wind-flow ${speed===0||!active?'flow-paused':''}`} style={{'--flow-duration':`${Math.max(.4,9/(1+speed*.65))}s`}}>
 <svg className="wind-drag-control" viewBox="0 0 640 300" role="group" tabIndex={active?0:-1} aria-disabled={!active} aria-label={`Wind vector control: ${speed} meters per second toward ${bearing} degrees, ${gusts} percent variation. Drag to set direction and magnitude. Left and right arrows turn; up and down arrows change speed; Home calms wind.`}
 onPointerDown={e=>{if(!active)return;e.currentTarget.setPointerCapture(e.pointerId);dragging.current=true;updatePointer(e)}}
 onPointerMove={e=>{if(dragging.current)updatePointer(e)}} onPointerUp={()=>dragging.current=false} onPointerCancel={()=>dragging.current=false} onLostPointerCapture={()=>dragging.current=false} onKeyDown={keyDown}>
 <defs><clipPath id={id}><rect width="640" height="300" rx="14"/></clipPath><radialGradient id={id+'glow'}><stop stopColor="#a3c2c8" stopOpacity=".07"/><stop offset="1" stopColor="#a3c2c8" stopOpacity="0"/></radialGradient></defs>
 <g clipPath={`url(#${id})`}><rect width="640" height="300" fill="#191d1f"/><rect width="640" height="300" fill={`url(#${id}glow)`}/>
 {Array.from({length:24},(_,i)=><path key={i} d={`M${i*32} 0 V300 M0 ${i*32} H640`} stroke="#ffffff04"/>)}
 <g transform={`rotate(${angle} 320 150)`}>
 {Array.from({length:23},(_,i)=>{let y=-270+i*38;const points=Array.from({length:45},(_,j)=>`${j?'L':'M'}${-300+j*30} ${y+Math.sin(j*.35+i*.7)*gusts*.28+Math.cos(j*.66+i)*gusts*.09}`).join(' ');return <g key={i}><path d={points} fill="none" stroke="#b2cbd1" strokeOpacity=".065" strokeWidth=".7"/><path className="flow-streak" d={points} fill="none" stroke="#b2cbd1" strokeOpacity={speed===0?.08:.22+i%4*.11} strokeWidth={.7+i%3*.4} strokeDasharray={`${12+speed*1.5} ${145+i%5*23}`} style={{'--travel':-(12+speed*1.5+145+i%5*23),animationDelay:`-${i*.39}s`,animationDuration:`${Math.max(.35,9/(1+speed*.65))*(1+(i%4)*gusts/250)}s`}}/></g>})}
 </g><circle cx="320" cy="150" r="106" stroke="#c5dde51c" strokeDasharray="2 6" fill="none"/>
 <circle cx="320" cy="150" r="53" stroke="#c5dde511" strokeDasharray="2 6" fill="none"/>
 <line x1="320" y1="150" x2={handleX} y2={handleY} stroke="#c4dfe6" strokeWidth="1.4"/>
 <circle cx="320" cy="150" r="3" fill="#b6c9cd"/>
 <circle className="wind-handle-hit" cx={handleX} cy={handleY} r="18" fill="#bcd1d713"/>
 <circle className="wind-vector-handle" cx={handleX} cy={handleY} r="8" fill="#202d31" stroke="#d7edf2" strokeWidth="1.5"/>
 <g transform={`translate(${handleX} ${handleY}) rotate(${bearing})`}><path d="M0 -4 L3 3 L0 1 L-3 3Z" fill="#d7edf2"/></g>
 <g fill="#8b989b" fontSize="10" textAnchor="middle"><text x="320" y="20">N</text><text x="320" y="287">S</text><text x="18" y="155">W</text><text x="622" y="155">E</text></g></g></svg>
 <div className="weather-graphic-footer"><span><i/>{!active?'Wind disabled':speed===0?'Still air':'Live flow field'}</span><span>Drag the handle · outward = stronger</span></div>
 </div>;
}

export function HazeTransmission({haze}) {
 const id=useId().replace(/:/g,'');
 return <div className="haze-transmission"><svg viewBox="0 0 360 150" role="img" aria-label={`Aerosol light attenuation at ${haze} percent haze`}>
 <defs><linearGradient id={id}><stop stopColor="#d7c097" stopOpacity=".42"/><stop offset="1" stopColor="#d7c097" stopOpacity={.38*Math.exp(-haze/22)}/></linearGradient><linearGradient id={id+'fog'}><stop stopColor="#cab89c" stopOpacity="0"/><stop offset="1" stopColor="#cab89c" stopOpacity={haze/450}/></linearGradient></defs>
 <rect x="25" y="16" width="310" height="100" rx="10" fill="#1b1b1b" stroke="#ffffff0b"/><rect x="25" y="16" width="310" height="100" rx="10" fill={`url(#${id}fog)`}/>
 <path d="M34 67 L327 35 V100Z" fill={`url(#${id})`}/>
 {Array.from({length:32},(_,i)=><circle key={i} cx={50+i*53%275} cy={28+i*31%75} r={.6+i%3*.45} fill="#d6c19d" opacity={haze/200}/>)}
 <path d={Array.from({length:45},(_,i)=>`${i?'L':'M'}${35+i*6.5} ${108-78*Math.exp(-haze/25*i/44)}`).join(' ')} fill="none" stroke="#d4bc94" strokeWidth="1.4"/>
 <circle cx="35" cy="67" r="4" fill="#eddbb8"/>
 <g fill="#8e867b" fontSize="9"><text x="25" y="138">LIGHT SOURCE</text><text x="268" y="138">DISTANCE →</text></g>
 </svg><div className="transmission-readout"><span>Relative transmission</span><strong>{Math.round(Math.exp(-haze/25)*100)}<small>%</small></strong></div></div>;
}

export function PrecipitationField({rate,type,size,active,fallSpeed=6,moving=true}) {
 const snow=type==='Snow',hail=type==='Hail';
 return <div className={`precipitation-field ${!active||rate===0||!moving?'precipitation-paused':''}`} aria-label={`${type} intensity ${rate} millimeters per hour`} role="img">
 <div className="rain-cloud"><span/><span/><span/></div>
 {Array.from({length:60},(_,i)=>{const on=i<rate/30*60;const diameter=hail?3+size*.27:snow?2+size*.65:1+size*.15;return <i key={i} className={snow?'snow-particle':hail?'hail-particle':'rain-particle'} style={{left:`${4+i*37%92}%`,opacity:on&&active?.2+i%4*.12:0,width:diameter,height:snow||hail?diameter:9+size*3,animationDuration:`${Math.max(.2,6/fallSpeed)*(1+i%3*.12)}s`,animationDelay:`-${i*.27}s`}}/>})}
 <div className="precipitation-ground"/>
 </div>;
}

export function OzoneAbsorption({amount}) {
 const id=useId().replace(/:/g,'');
 const curve=Array.from({length:81},(_,i)=>{const t=i/80;return `${i?'L':'M'}${22+t*316} ${36+amount*.78*Math.exp(-Math.pow((t-.59)/.23,2))}`}).join(' ');
 return <div className="ozone-spectrum-card"><svg viewBox="0 0 360 178" role="img" aria-label={`Illustrative ozone absorption spectrum at ${amount} percent strength`}>
 <defs><linearGradient id={id}><stop stopColor="#8a74b9"/><stop offset=".25" stopColor="#7597c9"/><stop offset=".5" stopColor="#9bb7a0"/><stop offset=".72" stopColor="#c9b47d"/><stop offset="1" stopColor="#c08681"/></linearGradient><linearGradient id={id+'fill'} x2="0" y2="1"><stop stopColor="#b8a0d1" stopOpacity=".2"/><stop offset="1" stopColor="#b8a0d1" stopOpacity=".02"/></linearGradient></defs>
 {[36,77,118].map(y=><path key={y} d={`M22 ${y} H338`} stroke="#ffffff0b" strokeDasharray="2 5"/>)}
 <path d="M22 36 H338" stroke="#bca582" strokeOpacity=".5" strokeDasharray="4 5"/>
 <path d={`${curve} L338 36 H22Z`} fill={`url(#${id}fill)`}/>
 <path d={curve} fill="none" stroke="#bea3dd" strokeWidth="1.6"/>
 <circle cx={22+.59*316} cy={36+amount*.78} r="3.5" fill="#d8c3ed"/>
 <path d={`M${22+.59*316} ${42+amount*.78} V138`} stroke="#b79acd" strokeOpacity=".25" strokeDasharray="2 4"/>
 <rect x="22" y="142" width="316" height="3" rx="1.5" fill={`url(#${id})`} opacity=".7"/>
 <g fill="#8e8597" fontSize="9"><text x="22" y="166">380 nm</text><text x="139" y="166">VISIBLE LIGHT</text><text x="301" y="166">780 nm</text></g>
 </svg><div className="ozone-legend"><span><i/>Incoming</span><span><i/>Transmitted</span><span>Illustrative response</span></div></div>;
}

export function GroundFogProfile({density}) {
 const id=useId().replace(/:/g,'');
 return <div className="fog-visibility"><svg viewBox="0 0 360 178" role="img" aria-label={`Fog visibility falloff at ${density} percent density`}>
 <defs><radialGradient id={id} cx="0%" cy="50%" r="100%"><stop stopColor="#b5cdd3" stopOpacity=".14"/><stop offset="1" stopColor="#b5cdd3" stopOpacity="0"/></radialGradient><linearGradient id={id+'mist'}><stop stopColor="#b4c5ca" stopOpacity="0"/><stop offset="1" stopColor="#b4c5ca" stopOpacity={density/600}/></linearGradient></defs>
 <rect x="18" y="18" width="324" height="126" rx="12" fill="#191d1f" stroke="#ffffff08"/>
 <path d="M40 81 L326 23 V140Z" fill={`url(#${id})`}/>
 <path d="M40 81 H326" stroke="#a5bdc533" strokeDasharray="2 6"/>
 {[0,1,2,3].map(i=>{const x=88+i*72,fade=Math.exp(-density/28*(i+1)/4);return <g key={i}><g opacity={fade}><rect x={x-10} y={59-i*3} width="20" height={44+i*6} rx="3" fill="#adc7cf0a" stroke="#bdd2d9" strokeWidth="1"/><path d={`M${x-5} 81 H${x+5} M${x} 76 V86`} stroke="#d0e0e4" strokeWidth=".8"/></g><text x={x} y="166" fill="#7e8e94" fontSize="9" textAnchor="middle">{[100,250,500,1000][i]} m</text></g>})}
 <rect x="18" y="18" width="324" height="126" rx="12" fill={`url(#${id}mist)`}/>
 <circle cx="40" cy="81" r="10" fill="#243237" stroke="#a9c5cf" strokeWidth=".8"/><circle cx="40" cy="81" r="3" fill="#c2dce4"/>
 </svg><div className="fog-readout"><span>Visibility at distance</span><span>{density<20?'Clear':density<55?'Softened':'Obscured'}</span></div></div>;
}
