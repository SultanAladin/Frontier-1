import React,{useRef,useId,useEffect} from 'react';

export function LunarPosition({azimuth,elevation,onChange}){
 const dragging=useRef(false),id=useId().replace(/:/g,'');
 const left=40,top=26,width=280,height=180,horizon=116;
 const x=left+azimuth/360*width,y=top+(90-elevation)/180*height;
 const update=ev=>{
  const rect=ev.currentTarget.getBoundingClientRect(),scale=Math.min(rect.width/360,rect.height/246);if(!scale)return;
  const px=(ev.clientX-rect.left-(rect.width-360*scale)/2)/scale;
  const py=(ev.clientY-rect.top-(rect.height-246*scale)/2)/scale;
  onChange(Math.round(Math.max(0,Math.min(360,(px-left)/width*360))),Math.round(Math.max(-90,Math.min(90,90-(py-top)/height*180))));
 };
 const keyboard=ev=>{if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home'].includes(ev.key))return;ev.preventDefault();const step=ev.shiftKey?10:1;if(ev.key==='Home'){onChange(azimuth,0);return;}onChange(Math.max(0,Math.min(360,azimuth+(ev.key==='ArrowLeft'?-step:ev.key==='ArrowRight'?step:0))),Math.max(-90,Math.min(90,elevation+(ev.key==='ArrowUp'?step:ev.key==='ArrowDown'?-step:0))));};
 return <div className="lunar-coordinate-pad"><svg viewBox="0 0 360 246" role="group" tabIndex={0} aria-label={`Moon position pad: azimuth ${azimuth} degrees, elevation ${elevation} degrees. Drag to position. Arrow keys adjust by one degree; Shift by ten; Home sets the horizon.`}
 onPointerDown={ev=>{ev.currentTarget.setPointerCapture(ev.pointerId);dragging.current=true;update(ev)}} onPointerMove={ev=>{if(dragging.current)update(ev)}} onPointerUp={()=>dragging.current=false} onPointerCancel={()=>dragging.current=false} onLostPointerCapture={()=>dragging.current=false} onKeyDown={keyboard}>
 <defs><linearGradient id={id+'sky'} x2="0" y2="1"><stop stopColor="#60728f" stopOpacity=".12"/><stop offset="1" stopColor="#60728f" stopOpacity=".02"/></linearGradient><radialGradient id={id+'halo'}><stop stopColor="#afccec" stopOpacity=".2"/><stop offset="1" stopColor="#afccec" stopOpacity="0"/></radialGradient><radialGradient id={id+'moon'} cx="28%" cy="22%"><stop stopColor="#ecf2f8"/><stop offset=".7" stopColor="#bacce0"/><stop offset="1" stopColor="#7e94ae"/></radialGradient></defs>
 <rect x={left} y={top} width={width} height={height} rx="9" fill="#1a1b1d" stroke="#ffffff0c"/>
 <path d="M49 26 H311 Q320 26 320 35 V116 H40 V35 Q40 26 49 26Z" fill={`url(#${id}sky)`}/>
 {[75,110,145,180,215,250,285].map(xx=><path key={xx} d={`M${xx} 27 V205`} stroke="#b5c8dd" strokeOpacity={xx===180?.12:.05} strokeWidth=".7"/>)}
 {[56,86,146,176].map(yy=><path key={yy} d={`M41 ${yy} H319`} stroke="#b5c8dd" strokeOpacity=".055" strokeWidth=".7"/>)}
 <path d="M40 116 H320" stroke="#9db0c8" strokeOpacity=".4" strokeWidth=".8"/>
 <text x="311" y="109" textAnchor="end" fill="#6f7f94" fontSize="7" letterSpacing="1">HORIZON</text>
 <path d={`M${x} 27 V205 M41 ${y} H319`} stroke="#adc9e8" strokeOpacity=".28" strokeDasharray="2 4" strokeWidth=".7"/>
 <path d={`M${x} 208 V213 M32 ${y} H37`} stroke="#c2d7ef" strokeWidth="1.3"/>
 <circle cx={x} cy={y} r="24" fill={`url(#${id}halo)`}/>
 <circle cx={x} cy={y} r="12" fill="#1c2530" stroke="#99b7da" strokeOpacity=".45"/>
 <circle className="lunar-pad-marker" cx={x} cy={y} r="7" fill={`url(#${id}moon)`}/>
 <circle cx={x-2} cy={y+1} r="1.5" fill="#6b809b" opacity=".22"/>
 <g fontSize="8" fill="#7a899d" textAnchor="end"><text x="29" y="29">+90°</text><text x="29" y="119">0°</text><text x="29" y="209">−90°</text></g>
 <g fontSize="9" fill="#9daac0" textAnchor="middle">{['N','E','S','W','N'].map((label,i)=><text key={i} x={left+i*70} y="229">{label}</text>)}</g>
 <text x="180" y="244" textAnchor="middle" fill="#606b7b" fontSize="7" letterSpacing="1.2">AZIMUTH · 0° — 360°</text>
 </svg><div className="lunar-pad-caption"><span><i/>{elevation===0?'On the horizon':elevation>0?'Above the horizon':'Below the horizon'}</span><span>Drag to position</span></div></div>;
}
export function CelestialRotation({angle,onChange}){
 const dragging=useRef(false);
 const update=ev=>{const r=ev.currentTarget.getBoundingClientRect(),s=Math.min(r.width/340,r.height/240);if(!s)return;const x=(ev.clientX-r.left-(r.width-340*s)/2)/s-170,y=(ev.clientY-r.top-(r.height-240*s)/2)/s-111;onChange(Math.round((Math.atan2(x,-y)*180/Math.PI+360)%360));};
 return <svg className="celestial-dial" viewBox="0 0 340 240" role="img" aria-label={`Celestial rotation ${angle} degrees. Drag around the pole to rotate.`} onPointerDown={e=>{e.currentTarget.setPointerCapture(e.pointerId);dragging.current=true;update(e)}} onPointerMove={e=>dragging.current&&update(e)} onPointerUp={()=>dragging.current=false} onPointerCancel={()=>dragging.current=false}>
 {[34,62,88].map(r=><circle key={r} cx="170" cy="111" r={r} fill={r===34?'#b097da04':'none'} stroke="#b39bcc" strokeOpacity=".15" strokeDasharray={r===88?undefined:'2 5'}/>)}
 {Array.from({length:24},(_,i)=><path key={i} d="M170 23 V28" transform={`rotate(${i*15} 170 111)`} stroke="#9e87b5" opacity=".6"/>)}
 <g transform={`rotate(${angle} 170 111)`}><path d="M138 70 L180 55 L201 91 L212 132 L178 168 L130 150" fill="none" stroke="#b49bcc" strokeOpacity=".5"/>{[[138,70],[180,55],[201,91],[212,132],[178,168],[130,150]].map(([x,y],i)=><circle key={i} cx={x} cy={y} r={i%2?2:3} fill="#c8b8e5"/>)}<path d="M170 111 V23" stroke="#c4a8e2" strokeOpacity=".35"/><circle cx="170" cy="23" r="6" fill="#312638" stroke="#ddc8f1"/></g>
 <circle cx="170" cy="111" r="3" fill="#d4c1e7"/><g textAnchor="middle" fontSize="9" fill="#9f8cab"><text x="170" y="12">0°</text><text x="272" y="115">90°</text><text x="170" y="217">180°</text><text x="63" y="115">270°</text><text x="170" y="237">Drag the ring · celestial pole</text></g>
 </svg>;
}
// A single normalized brightness signal drives the star, live reading and trace.
export function twinkleFlux(time,strength,rate){
 const oscillation=.5+.3*Math.sin(time*Math.PI*2*rate)+.2*Math.sin(time*Math.PI*2*rate*.47+.8);
 return 1-strength/100*oscillation;
}
export function TwinkleSignal({strength,rate,active}){
 const id=useId().replace(/:/g,''),trace=useRef(null),dot=useRef(null),sample=useRef(null),reading=useRef(null),time=useRef(0),status=useRef(null);
 useEffect(()=>{
  let frame,last,drawn=0;
  const media=window.matchMedia?.('(prefers-reduced-motion: reduce)');
  const draw=t=>{
   const amount=active?strength:0,flux=twinkleFlux(t,amount,rate);
   const d=Array.from({length:121},(_,i)=>{const value=twinkleFlux(t-4+i/30,amount,rate);return `${i?'L':'M'}${22+i*316/120} ${112+(1-value)*55}`}).join(' ');
   trace.current?.setAttribute('d',d);dot.current?.setAttribute('cy',String(112+(1-flux)*55));sample.current?.setAttribute('opacity',String(.1+flux*.9));
   if(reading.current)reading.current.textContent=`${Math.round(flux*100)}%`;
   if(status.current)status.current.textContent=media?.matches?'STATIC SAMPLE':!active?'DISABLED':strength===0?'STEADY':'LIVE SIGNAL';
  };
  const animate=now=>{
   const delta=last?Math.min((now-last)/1000,.1):0;last=now;
   if(!document.hidden){time.current+=delta;if(now-drawn>=32){draw(time.current);drawn=now;}}
   frame=requestAnimationFrame(animate);
  };
  const start=()=>{cancelAnimationFrame(frame);draw(time.current);last=undefined;if(active&&strength>0&&!media?.matches)frame=requestAnimationFrame(animate);};
  start();media?.addEventListener('change',start);
  return ()=>{cancelAnimationFrame(frame);media?.removeEventListener('change',start);};
 },[strength,rate,active]);
 return <div className="twinkle-monitor"><div className="twinkle-monitor-heading"><span><i/>RELATIVE BRIGHTNESS</span><span ref={status}>LIVE SIGNAL</span></div><svg className="twinkle-monitor-svg" viewBox="0 0 360 201" role="img" aria-label={`Live stellar brightness signal, ${strength} percent modulation at ${rate} hertz. Same signal drives the star sample and four-second history.`}>
 <defs><radialGradient id={id}><stop stopColor="#d7bcef" stopOpacity=".32"/><stop offset=".35" stopColor="#bf9cde" stopOpacity=".1"/><stop offset="1" stopColor="#bf9cde" stopOpacity="0"/></radialGradient></defs>
 <path d="M109 45 H139" stroke="#cbb5d427" strokeDasharray="2 4"/>
 <g ref={sample}><circle cx="71" cy="47" r="38" fill={`url(#${id})`}/><path d="M71 31 V63 M55 47 H87" stroke="#d9c2ed" strokeWidth=".7" opacity=".7"/><circle cx="71" cy="47" r="3.3" fill="#e6d4f4"/><circle cx="71" cy="47" r="1.4" fill="#fff5ff"/></g>
 <text x="152" y="48" ref={reading} fill="#e0d1ea" fontSize="30" fontWeight="300">100%</text><text x="154" y="69" fill="#8f809b" fontSize="9">instantaneous brightness</text>
 <rect x="13" y="99" width="334" height="78" rx="8" fill="#19171d" stroke="#ffffff07"/>
 {[112,139,167].map(y=><path key={y} d={`M22 ${y} H338`} stroke="#b29ac017" strokeDasharray="2 5"/>)}
 <path ref={trace} d="M22 112 H338" stroke="#c8a4df" strokeWidth="1.2" fill="none"/>
 <path d="M338 106 V171" stroke="#c3a2dc33" strokeWidth=".7"/><circle ref={dot} cx="338" cy="112" r="3" fill="#ead8f5"/>
 <g fill="#81718d" fontSize="8"><text x="22" y="195">−4 s</text><text x="174" y="195">−2 s</text><text x="325" y="195">now</text></g>
 </svg></div>;
}
