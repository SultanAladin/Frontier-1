import React, {useId, useRef} from 'react';
import {formatDistance} from './optics.js';

export function SensorField({focal,aperture,focus,optics}) {
 const id=useId().replace(/:/g,''),extent=34+optics.horizontalFov*.85,gap=8+Math.min(38,optics.pupil*.75);
 return <div className="sensor-field"><svg viewBox="0 0 640 290" role="img" aria-label={`Full-frame sensor, ${optics.horizontalFov.toFixed(1)} degree horizontal field of view, ${optics.pupil.toFixed(1)} millimeter entrance pupil, focused at ${focus} meters`}>
 <defs><linearGradient id={id}><stop stopColor="#b59acc" stopOpacity=".02"/><stop offset="1" stopColor="#b59acc" stopOpacity=".1"/></linearGradient></defs>
 <path d="M30 141 H610" stroke="#b3a5bf25" strokeDasharray="3 6"/>
 <path className="optical-envelope" d={`M235 141 L553 ${141-extent} V${141+extent} Z`} fill={`url(#${id})`} stroke="#ae98c7" strokeWidth="1"/>
 <path d={`M78 107 L235 141 L553 ${141+extent} M78 175 L235 141 L553 ${141-extent}`} fill="none" stroke="#b79dd0" strokeOpacity=".36" strokeWidth=".8"/>
 <rect x="68" y="100" width="12" height="82" rx="3" fill="#a4b6c51f" stroke="#b9c7d6" strokeWidth="1.2"/>
 {[0,1,2,3,4,5].map(i=><path key={i} d={`M71 ${108+i*13} H77`} stroke="#d0dae2" strokeWidth=".6"/>)}
 <path d="M64 94 H57 V187 H64" stroke="#6d7b8a" fill="none" strokeWidth=".6"/>
 <path className="pupil-gate" d={`M235 72 V${141-gap} M235 ${141+gap} V210`} stroke="#b9a5cd" strokeWidth="7" strokeLinecap="round"/>
 <path d={`M225 ${141-gap} H245 M225 ${141+gap} H245`} stroke="#e2d0f4" strokeWidth="1.2"/>
 <path d={`M553 ${141-extent} V${141+extent}`} stroke="#d6c6e8" strokeWidth="2"/>
 <path d={`M545 ${141-extent} H562 M545 ${141+extent} H562`} stroke="#d6c6e8" strokeWidth="1"/>
 <path d="M320 111 Q328 141 320 171" stroke="#a68abb" fill="none" strokeWidth=".7"/>
 <text x="342" y="134" fill="#ddcbea" fontSize="21" fontWeight="300">{optics.horizontalFov.toFixed(1)}°</text>
 <text x="343" y="152" fill="#8f829d" fontSize="8" letterSpacing="1">HORIZONTAL FOV</text>
 <g textAnchor="middle"><text x="75" y="236" fill="#afbbc8" fontSize="10">Sensor</text><text x="75" y="254" fill="#737b85" fontSize="9">36 × 24 mm</text><text x="235" y="236" fill="#c5afd9" fontSize="10">ƒ/{aperture.toFixed(1)}</text><text x="235" y="254" fill="#8a7c97" fontSize="9">Ø {optics.pupil.toFixed(1)} mm pupil</text><text x="553" y="270" fill="#b5a5c4" fontSize="9">Focus plane · {focus} m</text></g>
 <path d="M84 211 H225" stroke="#5d5267" strokeWidth=".6"/>
 <text x="154" y="206" fill="#8f819b" fontSize="9" textAnchor="middle">{focal} mm</text>
 </svg><div className="sensor-field-caption"><span>Optical schematic · not to scale</span><span>Full-frame / 3:2</span></div></div>;
}

export function DepthOfField({focus,optics,enabled,onChange}) {
 const dragging=useRef(false),max=100;
 const x=m=>22+Math.log1p(Math.min(max,Math.max(0,m)))/Math.log1p(max)*316;
 const near=enabled?x(optics.near):22,far=enabled?x(optics.far):338,plane=x(focus);
 const update=e=>{const box=e.currentTarget.getBoundingClientRect(),scale=Math.min(box.width/360,box.height/174);if(!scale)return;
 const px=(e.clientX-box.left-(box.width-360*scale)/2)/scale;
 onChange(Math.round(Math.max(1,Math.min(100,Math.expm1((px-22)/316*Math.log1p(100))))));};
 return <div className="depth-of-field"><svg viewBox="0 0 360 174" role="img" aria-label={`Focus ${focus} meters. Acceptable sharpness ${enabled?`${formatDistance(optics.near)} to ${formatDistance(optics.far)}`:'unlimited; depth of field disabled'}. Drag focal plane to focus.`}
 onPointerDown={e=>{e.currentTarget.setPointerCapture(e.pointerId);dragging.current=true;update(e)}} onPointerMove={e=>{if(dragging.current)update(e)}} onPointerUp={()=>dragging.current=false} onPointerCancel={()=>dragging.current=false} onLostPointerCapture={()=>dragging.current=false}>
 <rect x="22" y="31" width="316" height="90" rx="9" fill="#18191c" stroke="#ffffff08"/>
 <rect className="dof-sharp-region" x={near} y="32" width={Math.max(1,far-near)} height="88" fill="#8eacd323"/>
 <path d={`M${near} 32 V120 M${far} 32 V120`} stroke="#a3badb" strokeOpacity=".55" strokeDasharray="3 5"/>
 {[1,3,10,30,100].map(m=><g key={m}><path d={`M${x(m)} 120 V125`} stroke="#6e788a"/><text x={x(m)} y="144" textAnchor="middle" fontSize="9" fill="#7e899b">{m} m</text></g>)}
 {Array.from({length:17},(_,i)=>{const xx=30+i*18.5,sharp=xx>=near&&xx<=far;return <path key={i} d={`M${xx-3} 62 H${xx+3} M${xx} 55 V93 M${xx-3} 86 H${xx+3}`} stroke="#aabbd1" strokeWidth=".7" opacity={sharp?.75:.2} style={{filter:sharp?'none':'blur(1.7px)'}}/>})}
 <path d={`M${plane} 21 V121`} stroke="#d2dff1" strokeWidth="1.2"/>
 <path d={`M${plane-5} 16 L${plane+5} 16 L${plane} 23Z`} fill="#d2dff1"/>
 <circle cx={plane} cy="76" r="9" fill="#1f2937" stroke="#c4d7ee"/>
 <path d={`M${plane-3} 76 H${plane+3} M${plane} 73 V79`} stroke="#e3edf7"/>
 <text x="22" y="168" fill="#7e899b" fontSize="8" letterSpacing="1">DISTANCE · LOG SCALE</text>
 </svg></div>;
}
