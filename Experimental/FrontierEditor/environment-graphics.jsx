import React, {useRef, useId} from 'react';

// Solar orbit in a vertical plane above a gridded horizon. Azimuth turns the
// orbital plane; elevation moves the sun along it, including below the horizon.
export function SunGizmo({azimuth,elevation,onChange}) {
 const drag=useRef(null),id=useId().replace(/:/g,''),r=103,cx=180,cy=143;
 const angle=(azimuth-135)*Math.PI/180;
 const point=t=>[cx-r*Math.cos(t)*Math.cos(angle),cy-r*Math.sin(t)+r*.45*Math.cos(t)*Math.sin(angle)];
 const arc=(start)=>Array.from({length:81},(_,i)=>{const p=point(start+i*Math.PI/80);return `${i?'L':'M'}${p[0].toFixed(2)},${p[1].toFixed(2)}`}).join(' ');
 const [x,y]=point(elevation*Math.PI/180),[hx,hy]=point(0),night=elevation<0;
 const move=e=>{if(!drag.current)return;const d=drag.current;onChange(Math.round(((d.a+(e.clientX-d.x)*.65)%360+360)%360),Math.round(Math.max(-90,Math.min(90,d.e-(e.clientY-d.y)*.6))))};
 return <div className="gizmo solar-orbit-gizmo" title="Drag horizontally for azimuth, vertically for elevation. Sliders offer precise control."
 onPointerDown={e=>{e.currentTarget.setPointerCapture(e.pointerId);drag.current={x:e.clientX,y:e.clientY,a:azimuth,e:elevation}}}
 onPointerMove={move} onPointerUp={()=>drag.current=null} onPointerCancel={()=>drag.current=null}>
 <svg viewBox="0 0 360 280" role="img" aria-label={`Solar orbit: azimuth ${azimuth} degrees, elevation ${elevation} degrees`}>
 <defs><clipPath id={id+'horizon'}><ellipse cx={cx} cy={cy} rx={r} ry="49"/></clipPath><radialGradient id={id+'glow'}><stop stopColor="#ffda87" stopOpacity=".3"/><stop offset="1" stopColor="#ffda87" stopOpacity="0"/></radialGradient></defs>
 <ellipse cx={cx} cy={cy} rx={r} ry="49" fill="#151515" fillOpacity=".6"/>
 <g clipPath={`url(#${id}horizon)`}>{Array.from({length:17},(_,i)=><path key={i} d={`M${77+i*13} 94 V192 M77 ${94+i*13} H283`} stroke="#ffffff0c" strokeWidth=".65"/>)}</g>
 <ellipse cx={cx} cy={cy} rx={r} ry="49" fill="none" stroke="#707070" strokeOpacity=".65" strokeWidth=".9"/>
 <path d={arc(Math.PI)} fill="none" stroke="#77736b" strokeOpacity=".5" strokeWidth="1" strokeDasharray="2 3"/>
 <path d={arc(0)} fill="none" stroke="#e2ac5b" strokeWidth="1.8" strokeLinecap="round"/>
 <line x1={x} y1={y} x2={hx} y2={hy} stroke="#b8aa8e" strokeDasharray="2 4" strokeWidth=".8" opacity=".55"/>
 <ellipse cx={x} cy={hy} rx="6" ry="2.3" fill="#c5c0b6" opacity=".32"/>
 <circle cx={hx} cy={hy} r="5.5" fill="#181818" stroke="#f1efe7" strokeWidth="1.7"/>
 {!night&&<circle cx={x} cy={y} r="23" fill={`url(#${id}glow)`}/>}
 <circle className="orbit-sun" cx={x} cy={y} r="8" fill={night?'#9b805c':'#f4cd75'} stroke={night?'#9b805c':'#b89045'} strokeWidth="1"/>
 {!night&&<circle cx={x} cy={y} r="4.4" fill="#fff4c7"/>}
 <g textAnchor="middle"><text className="orbit-time" x="180" y="24">12h</text><text className="orbit-time" x="180" y="269">24h</text><text x="180" y="81">N</text><text x="180" y="210">S</text><text x="47" y="147">W</text><text x="313" y="147">E</text></g>
 </svg></div>;
}

export function IlluminanceCurve({intensity}) {
 const id=useId().replace(/:/g,''),t=intensity/150;
 const y=x=>76-56*(1-Math.cos(x*Math.PI/2));
 const points=(end)=>Array.from({length:61},(_,i)=>{let x=end*i/60;return `${i?'L':'M'}${14+x*332},${y(x)}`}).join(' ');
 const x=14+t*332,cy=y(t);
 return <svg className="illuminance-curve" viewBox="0 0 360 100" role="img" aria-label={`Illuminance response: ${intensity} of 150 kilolux`}>
 <defs><linearGradient id={id} x2="0" y2="1"><stop stopColor="#d8bd82" stopOpacity=".2"/><stop offset="1" stopColor="#d8bd82" stopOpacity="0"/></linearGradient></defs>
 <path d="M14 24 H346 M14 50 H346 M14 77 H346" stroke="#ffffff0b" strokeDasharray="2 5"/>
 <path d={points(1)} fill="none" stroke="#595246" strokeWidth="1.2"/>
 <path d={`${points(t)} L${x} 88 H14Z`} fill={`url(#${id})`}/>
 <path d={points(t)} fill="none" stroke="#dac18b" strokeWidth="1.6" strokeLinecap="round"/>
 <path d={`M${x} ${cy+6} V88`} stroke="#c8af7a" strokeOpacity=".35" strokeDasharray="2 4"/>
 <circle cx={x} cy={cy} r="10" fill="#e8c76a" opacity=".07"/><circle cx={x} cy={cy} r="3.4" fill="#efe2c2"/>
 </svg>;
}

export function ScatteringGraph({rayleigh,haze}) {
 return <div className="scattering-graphic"><svg viewBox="0 0 620 225" role="img" aria-label="Relative scattering by wavelength">
 <defs><linearGradient id="spectrum"><stop stopColor="#927fb0"/><stop offset=".25" stopColor="#7f9dc9"/><stop offset=".5" stopColor="#94b5a9"/><stop offset=".75" stopColor="#c5b27e"/><stop offset="1" stopColor="#c28b7c"/></linearGradient><linearGradient id="scatterFill" x2="0" y2="1"><stop stopColor="#91afd2" stopOpacity=".19"/><stop offset="1" stopColor="#91afd2" stopOpacity="0"/></linearGradient></defs>
 {[35,80,125,170].map(y=><path key={y} d={`M24 ${y} H595`} stroke="#ffffff0c" strokeDasharray="2 6"/>)}
 {[24,167,310,452,595].map(x=><path key={x} d={`M${x} 20 V181`} stroke="#ffffff07"/>)}
 <path d={`M24 ${160-rayleigh*43} C135 ${160-rayleigh*35} 150 148 320 158 S510 171 595 172 V181 H24Z`} fill="url(#scatterFill)"/>
 <path d={`M24 ${160-rayleigh*43} C135 ${160-rayleigh*35} 150 148 320 158 S510 171 595 172`} stroke="#a1b9d8" strokeWidth="2" fill="none"/>
 <path d={`M24 ${170-haze*1.1} Q310 ${175-haze*.9} 595 ${177-haze*.7}`} stroke="#ccba95" strokeWidth="1.2" strokeDasharray="5 5" fill="none"/>
 <rect x="24" y="193" width="571" height="3" rx="1.5" fill="url(#spectrum)" opacity=".65"/>
 <g fill="#858585" fontSize="10"><text x="24" y="218">380 nm</text><text x="286" y="218">Visible spectrum</text><text x="555" y="218">780 nm</text></g>
 </svg></div>;
}

export function WindCompass({bearing}) {
 return <svg className="wind-compass" viewBox="0 0 230 210" role="img" aria-label={`Wind bearing ${bearing} degrees`}>
 <circle cx="115" cy="103" r="69" fill="#1c1c1c" stroke="#454545"/>
 <circle cx="115" cy="103" r="51" fill="none" stroke="#303030" strokeDasharray="2 5"/>
 {Array.from({length:36},(_,i)=><path key={i} d={`M115 37 V${i%3===0?45:41}`} transform={`rotate(${i*10} 115 103)`} stroke="#6b6b6b" strokeWidth=".7"/>)}
 <g transform={`rotate(${bearing} 115 103)`}><path d="M115 53 L126 115 L115 108 L104 115Z" fill="#b8c9cf"/><path d="M115 150 V112" stroke="#70868e" strokeDasharray="3 4"/></g>
 <circle cx="115" cy="103" r="3" fill="#ededed"/>
 <g fill="#8b8b8b" fontSize="10" textAnchor="middle"><text x="115" y="22">N</text><text x="115" y="195">S</text><text x="28" y="107">W</text><text x="202" y="107">E</text></g></svg>;
}
