import React, {useId, useRef} from 'react';

export function ParticleGauge({type,size}) {
 const r=9+Math.sqrt(size)*7,cy=78,id=useId().replace(/:/g,'');
 return <svg className="particle-gauge" viewBox="0 0 360 172" role="img" aria-label={`${type} particle diameter ${size} millimeters; magnified schematic`}>
 <defs><radialGradient id={id} cx="30%" cy="25%"><stop stopColor="#d5e4f3" stopOpacity=".55"/><stop offset="1" stopColor="#6d93b8" stopOpacity=".1"/></radialGradient></defs>
 <circle cx="180" cy={cy} r="59" fill="#1a1e23" stroke="#ffffff0b"/><path d="M105 78 H255 M180 8 V146" stroke="#b8cde31b" strokeDasharray="2 5"/>
 {type==='Snow'?<g stroke="#b9d3ec" fill="none" strokeWidth="1.4">{Array.from({length:6},(_,i)=><g key={i} transform={`rotate(${i*60} 180 78)`}><path d={`M180 78 V${78-r} M${180-r*.25} ${78-r*.75} L180 ${78-r*.5} L${180+r*.25} ${78-r*.75}`}/></g>)}</g>:type==='Hail'?<g><path d={`M180 ${cy-r} L${180+r*.8} ${cy-r*.5} L${180+r} ${cy+r*.4} L${180+r*.3} ${cy+r} L${180-r*.65} ${cy+r*.72} L${180-r} ${cy-r*.22}Z`} fill={`url(#${id})`} stroke="#b9d3ec"/><path d={`M180 ${cy-r*.55} L${180+r*.6} ${cy+r*.2} L${180-r*.35} ${cy+r*.55}Z`} stroke="#d9e9f344" fill="none"/></g>:<path d={`M180 ${cy-r} C${180+r*.28} ${cy-r*.48} ${180+r} ${cy-r*.04} ${180+r} ${cy+r*.25} C${180+r} ${cy+r*.7} ${180+r*.55} ${cy+r} 180 ${cy+r} C${180-r*.55} ${cy+r} ${180-r} ${cy+r*.7} ${180-r} ${cy+r*.25} C${180-r} ${cy-r*.04} ${180-r*.28} ${cy-r*.48} 180 ${cy-r}Z`} fill={`url(#${id})`} stroke="#b9d3ec"/>}
 <path d={`M${180-r} 141 V149 M${180-r} 145 H${180+r} M${180+r} 141 V149`} stroke="#91a8c0" strokeWidth=".8"/>
 <text x="180" y="166" fill="#94aac2" fontSize="9" textAnchor="middle">Ø {size.toFixed(1)} mm · magnified</text>
 </svg>;
}

export function FallSpeed({speed,type}) {
 return <div className="fall-speed-diagram" style={{'--fall-time':`${Math.max(.3,4/speed)}s`}} role="img" aria-label={`${type} fall speed ${speed} meters per second`}>
 <div className="fall-speed-ruler"><span>0</span><span>5</span><span>10 m</span></div>
 {Array.from({length:9},(_,i)=><i key={i} style={{left:`${22+i*8}%`,animationDelay:`-${i*.21}s`,borderRadius:type==='Rain'?'3px':'50%',height:type==='Rain'?15:5}}/>)}
 <span className="fall-direction">↓</span>
 </div>;
}

export function WaterSection({level,onChange}) {
 const id=useId().replace(/:/g,''),drag=useRef(false),y=190-level*6;
 const update=e=>{let box=e.currentTarget.getBoundingClientRect(),s=Math.min(box.width/620,box.height/225);if(!s)return;const py=(e.clientY-box.top-(box.height-225*s)/2)/s;onChange(Math.max(0,Math.min(25,Math.round((190-py)/6*2)/2)));};
 return <svg className="water-section" viewBox="0 0 620 225" role="img" aria-label={`Water surface elevation ${level} meters. Drag to change level.`} onPointerDown={e=>{e.currentTarget.setPointerCapture(e.pointerId);drag.current=true;update(e)}} onPointerMove={e=>drag.current&&update(e)} onPointerUp={()=>drag.current=false} onPointerCancel={()=>drag.current=false}>
 <defs><linearGradient id={id} x2="0" y2="1"><stop stopColor="#72afc2" stopOpacity=".22"/><stop offset="1" stopColor="#315b7133"/></linearGradient><clipPath id={id+'clip'}><path d="M63 35 L107 80 L155 156 Q165 187 220 190 H435 Q476 180 491 134 L534 63 L558 35Z"/></clipPath></defs>
 {[0,5,10,15,20,25].map(h=><g key={h}><text x="15" y={194-h*6} fill="#7a929e" fontSize="9">{h} m</text><path d={`M53 ${190-h*6} H573`} stroke="#ffffff0b" strokeDasharray="2 5"/></g>)}
 <g clipPath={`url(#${id}clip)`}><rect x="53" y={y} width="520" height={195-y} fill={`url(#${id})`}/>{[1,2,3].map(i=><path key={i} d={`M53 ${y+i*12} H574`} stroke="#83b7c3" strokeOpacity={.15/i}/>)}</g>
 <path d="M63 35 L107 80 L155 156 Q165 187 220 190 H435 Q476 180 491 134 L534 63 L558 35" stroke="#78817d" strokeWidth="1" fill="none"/>
 <path d={`M60 ${y} H566`} stroke="#a4d3df" strokeWidth="1.2"/>
 <circle cx="312" cy={y} r="7" fill="#24353c" stroke="#c5e6ee"/><path d={`M309 ${y} H315`} stroke="#d9f1f6"/>
 <text x="61" y="215" fill="#7c909a" fontSize="9" letterSpacing="1">BASIN CROSS-SECTION</text><text x="440" y="215" fill="#94b7c3" fontSize="9">Drag the surface to raise / lower</text>
 </svg>;
}

export function WaveProfile({amount,active}) {
 const amp=amount*.31,points=Array.from({length:145},(_,i)=>`${i?'L':'M'}${i*5} ${76+Math.sin(i*5/120*Math.PI*2)*amp+Math.sin(i*5/60*Math.PI*2)*amp*.2}`).join(' ');
 return <svg className={`wave-profile ${!active||amount===0?'water-paused':''}`} viewBox="0 0 360 154" role="img" aria-label={`Wave amplitude ${amount} percent`}><path d="M0 76 H360" stroke="#91bfb033" strokeDasharray="3 5"/><g className="wave-travel"><path d={`${points} V149 H0Z`} fill="#78baae0c"/><path d={points} fill="none" stroke="#96cfc2" strokeWidth="1.5"/></g></svg>;
}

export function WaterTransmission({clarity}) {
 const id=useId().replace(/:/g,''),k=.02+(1-clarity/100)*.78;
 return <svg className="water-transmission" viewBox="0 0 360 172" role="img" aria-label={`Light transmission at 5 meters: ${Math.round(Math.exp(-k*5)*100)} percent`}>
 <defs><linearGradient id={id} x2="0" y2="1"><stop stopColor="#8cb9cd" stopOpacity=".35"/><stop offset="1" stopColor="#6289aa" stopOpacity={Math.exp(-k*10)*.3}/></linearGradient></defs>
 <rect x="48" y="19" width="292" height="125" rx="9" fill="#19232b"/>
 {[0,5,10].map(n=><g key={n}><text x="12" y={26+n*11.5} fill="#7e98aa" fontSize="9">{n} m</text><path d={`M48 ${24+n*11.5} H340`} stroke="#b8dced14" strokeDasharray="2 5"/></g>)}
 {[0,1,2,3,4].map(i=><path key={i} d={`M${71+i*50} 23 L${62+i*50} 140 H${85+i*50}Z`} fill={`url(#${id})`}/>)}
 <path d={Array.from({length:51},(_,i)=>`${i?'L':'M'}${60+Math.exp(-k*i/5)*258} ${24+i/50*115}`).join(' ')} stroke="#b5d9ed" fill="none" strokeWidth="1.5"/>
 <text x="48" y="167" fill="#7f99aa" fontSize="9">Light remaining through depth</text>
 </svg>;
}

export function WaterCurrent({speed,bearing}) {
 return <svg className={`water-current ${speed===0?'water-paused':''}`} viewBox="0 0 360 160" role="img" aria-label={`Water current ${speed} meters per second toward ${bearing} degrees`}><g transform={`rotate(${bearing-90} 180 80)`}>{[-2,-1,0,1,2].map(i=><path key={i} className="current-streak" d={`M45 ${80+i*22} Q150 ${80+i*22+12} 315 ${80+i*22}`} fill="none" stroke="#8ecac8" strokeWidth="1" strokeOpacity=".45" strokeDasharray="16 24" style={{animationDuration:`${2/(.2+speed)}s`}}/>)}</g><circle cx="180" cy="80" r="25" fill="#202a2b" stroke="#688e9277"/><g transform={`rotate(${bearing} 180 80)`}><path d="M180 62 L187 92 L180 87 L173 92Z" fill="#b4d3d7"/></g></svg>;
}

export function ShoreFoam({amount}) {
 return <svg className="shore-foam" viewBox="0 0 360 154" role="img" aria-label={`Shoreline foam coverage ${amount} percent`}><path d="M0 127 Q65 110 107 73 T236 57 T360 16 V154 H0Z" fill="#a8ac9911"/><path d="M0 117 Q65 100 107 63 T236 47 T360 6" fill="none" stroke="#78857f" strokeWidth="1"/>{Array.from({length:65},(_,i)=>{let x=i*5.6,y=118-x*.29+Math.sin(x/43)*12;return <circle key={i} cx={x} cy={y-(i%4)*3} r={.8+i%3*.6} fill="#d0e3da" opacity={i%10<amount/10?.65:.02}/>})}</svg>;
}
