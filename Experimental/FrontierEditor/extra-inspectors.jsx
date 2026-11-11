import React from 'react';
import {MoonDisc,LunarSphere} from './moon-controls.jsx';
import FlareInspector,{flareDefaults} from './flare-inspector.jsx';
import {CelestialRotation,TwinkleSignal} from './celestial-controls.jsx';
import {Sparkles, Rainbow, Orbit, Waves, Wind, MoveUpRight, Droplets, Box, SlidersHorizontal, Layers} from 'lucide-react';
import {Moon, Stars, Sun} from './celestial-icons.jsx';

export const extraDefaults={...flareDefaults,phase:10.5,moonLux:.2,moonSize:.52,moonRotation:0,moonPitch:0,moonAzimuth:210,moonElevation:32,starLimit:5.5,twinkle:30,twinkleRate:1.2,starRotation:20,flareGain:45,flareSpread:50,ghosts:4,rainbowStrength:55,rainbowRadius:42,secondaryBow:25,swell:1.8,wavePeriod:8,tide:.4,riverSpeed:1.2,riverWidth:18,riverDepth:2.5,meander:40,viscosity:1,liquidDensity:1000,fill:65,liquidKind:'Water'};
export const extraSwitches={moon:[['Moonlight','Moonlight',Moon]],stars:[['Star field','Stars',Stars],['Twinkle','Twinkle',Sparkles]],flare:[['Lens flare','Flare',Sun],['Lens ghosts','Ghosts',Orbit]],rainbow:[['Primary bow','Rainbow',Rainbow],['Secondary bow','Secondary',Layers]],ocean:[['Ocean swell','Swell',Waves]],river:[['River flow','Flow',MoveUpRight]],liquid:[['Liquid volume','Volume',Box]]};
export const extraDescriptions={moon:'Phase, size, orientation & spherical position',stars:'Visibility, twinkle & celestial orientation',flare:'Sun / Optical effects · flare & lens ghosts',rainbow:'Atmosphere / Optical effects · dispersion',ocean:'Swell, wave period & tide',river:'Flow, channel dimensions & meander',liquid:'Fill, viscosity & material density'};
const metric=(num,key,unit,digits=0)=><div className="metric">{num(key,unit,digits)}</div>;
export default function ExtraInspectors({kind,card,v,set,slider,num,featureOn,active}) {
 const range=(a,b)=><div className="range-labels"><span>{a}</span><span>{b}</span></div>;
 if(kind==='moon'){
  const phase=v('phase')/29.53,illumination=(1-Math.cos(phase*Math.PI*2))/2;
  return <>
   {card('Lunar phase',Moon,<><div className="scattering-top">{metric(num,'phase','days',1)}<span className="small-pill">{Math.round(illumination*100)}% illuminated</span></div><p className="muted">Position in the 29.53-day synodic cycle</p><MoonDisc phase={v('phase')} rotation={v('moonRotation')} pitch={v('moonPitch')} size={v('moonSize')}/>{slider('phase',0,29.53,.01)}{range('New moon','New moon')}</>,'wide-card')}
   {card('Moonlight',Moon,<>{metric(num,'moonLux','lux',2)}<p className="muted">Full-disc illumination strength</p><div className="moon-light-disc" style={{opacity:.2+v('moonLux')*.6}}/><div className="water-detail">Phase-adjusted light <strong>{(v('moonLux')*illumination).toFixed(3)} lux</strong></div>{slider('moonLux',0,1,.01)}</>)}
   {card('Moon size',Moon,<>
    <div className="scattering-top">{metric(num,'moonSize','°',2)}<span className="small-pill">Angular diameter</span></div>
    <p className="muted">Apparent size, independent of position and rotation</p>
    <MoonDisc phase={v('phase')} size={v('moonSize')} rotation={v('moonRotation')} pitch={v('moonPitch')} comparison/>
    <label className="moon-size-entry"><span>Angular diameter</span><input type="number" aria-label="Moon angular diameter" min="0.1" step="0.01" value={v('moonSize')} onChange={e=>{const n=e.target.valueAsNumber;if(Number.isFinite(n)&&n>=.1)set('moonSize',n)}}/><span>°</span></label>
    {slider('moonSize',.1,Math.max(180,v('moonSize')),.01)}{range('0.10°',`${Math.max(180,v('moonSize'))}°`)}
    <p className="muted moon-size-hint">Type any larger value. Preview scale is compressed for oversized moons.</p>
    <button className="channel-reset" onClick={()=>set('moonSize',.52)}>Natural size · 0.52°</button>
   </>,'moon-size-card')}
   {card('Lunar position',MoveUpRight,<>
    <p className="muted">Position in the sky · drag the spherical dial</p>
    <LunarSphere azimuth={v('moonAzimuth')} elevation={v('moonElevation')} rotation={v('moonRotation')} size={v('moonSize')} onChange={(a,e)=>{set('moonAzimuth',a);set('moonElevation',e)}}/>
    <div className="lunar-axis-controls">
     <label><span>Azimuth <strong>{v('moonAzimuth')}°</strong></span>{slider('moonAzimuth',0,360)}</label>
     <label><span>Elevation <strong>{v('moonElevation')>0?'+':''}{v('moonElevation')}°</strong></span>{slider('moonElevation',-90,90)}</label>
    </div>
   </>,'lunar-location-card')}
   {card('Moon rotation',Orbit,<>
    <p className="muted">Surface orientation · independent of sky position</p>
    <MoonDisc phase={v('phase')} size={v('moonSize')} rotation={v('moonRotation')} pitch={v('moonPitch')} onRotate={(roll,pitch)=>{set('moonRotation',roll);set('moonPitch',pitch)}}/>
    <div className="lunar-axis-controls">
     <label><span>Roll <strong>{v('moonRotation')}°</strong></span>{slider('moonRotation',0,360)}</label>
     <label><span>Pitch <strong>{v('moonPitch')>0?'+':''}{v('moonPitch')}°</strong></span>{slider('moonPitch',-180,180)}</label>
    </div>
    <p className="muted moon-rotation-note">Drag left/right to roll, up/down to pitch. Arrow keys adjust either axis; Shift for 10° steps.</p>
    <button className="channel-reset" onClick={()=>{set('moonRotation',0);set('moonPitch',0)}}>Reset orientation</button>
   </>,'moon-rotation-card')}


  </>;
 }
 if(kind==='stars')return <>
  {card('Star field',Stars,<><div className="scattering-top">{metric(num,'starLimit','mag',1)}<span className="small-pill">Limiting magnitude</span></div><p className="muted">Higher values reveal fainter stars</p><svg className="star-field" viewBox="0 0 620 220" role="img" aria-label={`Star field magnitude limit ${v('starLimit')}`}><path d="M20 110 H600 M310 15 V205" stroke="#aaa1cf13" strokeDasharray="2 6"/><g transform={`rotate(${v('starRotation')} 310 110)`}>{Array.from({length:90},(_,i)=><circle key={i} className={active&&featureOn('Star field')&&featureOn('Twinkle')&&v('twinkle')>0?'twinkle-star':''} cx={22+i*137%574} cy={20+i*73%175} r={i%9===0?1.9:.6+i%3*.25} fill="#cfc6eb" opacity={i%9<=v('starLimit')?.25+i%4*.16:.02} style={{'--twinkle-low':1-v('twinkle')/100,animationDelay:`-${i*.19}s`,animationDuration:`${1/v('twinkleRate')}s`}}/>)}</g></svg>{slider('starLimit',0,8,.1)}{range('Bright stars only','Faint stars visible')}</>,'wide-card')}
  {card('Twinkle',Sparkles,<><div className="scattering-top">{metric(num,'twinkle','%')}<span className="small-pill">Modulation depth</span></div><TwinkleSignal strength={v('twinkle')} rate={v('twinkleRate')} active={active&&featureOn('Twinkle')&&featureOn('Star field')}/><div className="twinkle-depth-label"><span>Brightness variation</span><span>{100-v('twinkle')}–100%</span></div>{slider('twinkle',0,100)}{range('Steady light','Deep shimmer')}<div className="twinkle-frequency"><div><span>Frequency</span><strong>{v('twinkleRate').toFixed(1)}<small> Hz</small></strong></div>{slider('twinkleRate',.2,3,.1)}<p>Strength and frequency also update the star field.</p></div></>,'twinkle-card')}
  {card('Celestial rotation',Orbit,<>{metric(num,'starRotation','°')}<p className="muted">Rotate the field around the celestial pole</p><CelestialRotation angle={v('starRotation')} onChange={a=>set('starRotation',a)}/>{slider('starRotation',0,360)}{range('0°','360°')}</>)}
 </>;
 if(kind==='flare')return <FlareInspector card={card} v={v} set={set} slider={slider} num={num} featureOn={featureOn} active={active}/>;
 if(kind==='rainbow')return <>
  {card('Primary bow',Rainbow,<><div className="scattering-top">{metric(num,'rainbowStrength','%')}<span className="small-pill">Atmospheric optical effect</span></div><p className="muted">Visibility of refracted sunlight in airborne droplets</p><RainbowArc strength={v('rainbowStrength')} radius={v('rainbowRadius')} secondary={featureOn('Secondary bow')?v('secondaryBow'):0}/>{slider('rainbowStrength',0,100)}{range('Invisible','Vivid')}<div className="control-line current-bearing"><span>Angular radius</span><span>{v('rainbowRadius')}°</span></div>{slider('rainbowRadius',30,50,.5)}</>,'wide-card')}
  {card('Secondary bow',Layers,<>{metric(num,'secondaryBow','%')}<p className="muted">Fainter outer bow · reversed color order</p><div className="secondary-spectrum" style={{opacity:v('secondaryBow')/100}}/>{slider('secondaryBow',0,100)}{range('Primary only','Double rainbow')}</>,'wide-card')}
 </>;
 if(kind==='ocean')return <>
  {card('Ocean swell',Waves,<><div className="scattering-top">{metric(num,'swell','m',1)}<span className="small-pill">Wave height</span></div><p className="muted">Long-period open-water waves</p><svg className={`ocean-swell ${active&&featureOn('Ocean swell')?'':'water-paused'}`} viewBox="0 0 600 190"><g className="ocean-travel" style={{animationDuration:`${v('wavePeriod')}s`}}>{[0,1,2].map(j=><path key={j} d={Array.from({length:181},(_,i)=>`${i?'L':'M'}${i*5} ${90+j*19+Math.sin(i*5/300*Math.PI*2+j*.55)*v('swell')*7}`).join(' ')} fill="none" stroke="#7fb9cc" strokeWidth={1.5-j*.3} opacity={.7-j*.2}/>)}</g></svg>{slider('swell',0,8,.1)}{range('Flat sea','Heavy swell')}</>,'wide-card')}
  {card('Wave period',Wind,<>{metric(num,'wavePeriod','s')}<p className="muted">Time between wave crests</p><div className="period-pulses">{[0,1,2,3,4].map(i=><i key={i} style={{width:6+v('wavePeriod')*2,opacity:.25+i*.12}}/>)}</div>{slider('wavePeriod',3,20)}{range('Wind chop','Long swell')}</>)}
  {card('Tide offset',MoveUpRight,<>{metric(num,'tide','m',1)}<p className="muted">Sea level relative to mean datum</p><div className="tide-gauge"><i style={{bottom:50+v('tide')*8+'%'}}/><span/></div>{slider('tide',-5,5,.1)}{range('Low tide','High tide')}</>)}
 </>;
 if(kind==='river')return <>
  {card('River flow',MoveUpRight,<><div className="scattering-top">{metric(num,'riverSpeed','m/s',1)}<span className="small-pill">Downstream</span></div><p className="muted">Flow along the channel centerline</p><svg className={`river-channel ${active&&featureOn('River flow')&&v('riverSpeed')>0?'':'water-paused'}`} viewBox="0 0 600 200">{[-1,1].map(n=><path key={n} d={`M0 ${95+n*v('riverWidth')*.8} C170 ${95-v('meander')+n*v('riverWidth')*.8} 330 ${95+v('meander')+n*v('riverWidth')*.8} 600 ${95+n*v('riverWidth')*.8}`} fill="none" stroke="#728b8766"/>)}<path className="current-streak" d={`M0 95 C170 ${95-v('meander')} 330 ${95+v('meander')} 600 95`} stroke="#8bc8cc" strokeWidth="2" strokeDasharray="20 20" fill="none" style={{animationDuration:`${2/(.2+v('riverSpeed'))}s`}}/></svg>{slider('riverSpeed',0,5,.1)}{range('Still','Fast current')}<div className="control-line current-bearing"><span>Meander</span><span>{v('meander')}%</span></div>{slider('meander',0,100)}</>,'wide-card')}
  {card('Channel width',Waves,<>{metric(num,'riverWidth','m')}<p className="muted">Bank-to-bank channel width</p><div className="channel-width"><i style={{width:10+v('riverWidth')*.8+'%'}}/></div>{slider('riverWidth',2,100)}{range('Stream','Broad river')}</>)}
  {card('Channel depth',Droplets,<>{metric(num,'riverDepth','m',1)}<p className="muted">Mean channel depth</p><div className="river-depth" style={{'--depth':20+v('riverDepth')*7+'%'}}><i/></div>{slider('riverDepth',.2,10,.1)}{range('Shallow','Deep')}<div className="water-detail">Estimated discharge <strong>{(v('riverSpeed')*v('riverWidth')*v('riverDepth')).toFixed(1)} m³/s</strong></div></>)}
 </>;
 if(kind==='liquid')return <>
  {card('Liquid volume',Box,<><div className="scattering-top">{metric(num,'fill','%')}<span className="small-pill">Contained fluid</span></div><p className="muted">Fill fraction inside the simulation volume</p><div className="liquid-vessel"><div style={{height:v('fill')+'%'}}><i/></div><span>CAPACITY</span></div>{slider('fill',0,100)}{range('Empty','Full')}<div className="liquid-presets" role="group" aria-label="Liquid presets">{[['Water',1,1000],['Oil',80,850],['Syrup',2000,1300]].map(([name,viscosity,density])=><button key={name} aria-pressed={v('viscosity')===viscosity&&v('liquidDensity')===density} onClick={()=>{set('liquidKind',name);set('viscosity',viscosity);set('liquidDensity',density)}}>{name}</button>)}</div></>,'wide-card')}
  {card('Dynamic viscosity',SlidersHorizontal,<>{metric(num,'viscosity','mPa·s')}<p className="muted">Resistance to flow</p><div className="viscosity-strokes">{[0,1,2,3,4].map(i=><i key={i} style={{width:12+Math.log10(1+v('viscosity'))*15,height:60+i*5}}/>)}</div>{slider('viscosity',1,3000)}{range('Free-flowing','Thick')}</>)}
  {card('Fluid density',Layers,<>{metric(num,'liquidDensity','kg/m³')}<p className="muted">Mass per unit volume</p><div className="density-points">{Array.from({length:48},(_,i)=><i key={i} style={{opacity:i<v('liquidDensity')/2000*48?.8:.08}}/>)}</div>{slider('liquidDensity',500,2000,10)}{range('Light fluid','Dense fluid')}</>)}
 </>;
 return null;
}
function RainbowArc({strength,radius,secondary}){const colors=['#d18a86','#cfad7a','#d0c68c','#8fba9f','#80aec9','#998bbd'];return <svg className="rainbow-diagram" viewBox="0 0 600 240" role="img" aria-label={`Rainbow radius ${radius} degrees, strength ${strength} percent`}><path d="M40 204 H560" stroke="#ffffff12"/>{colors.map((c,i)=>{const r=110+radius*1.8-i*3;return <path key={c} d={`M${300-r} 204 A${r} ${r} 0 0 1 ${300+r} 204`} fill="none" stroke={c} strokeWidth="3.4" opacity={strength/100*.8}/>})}{colors.slice().reverse().map((c,i)=>{const r=143+radius*1.8-i*3;return <path key={c} d={`M${300-r} 204 A${r} ${r} 0 0 1 ${300+r} 204`} fill="none" stroke={c} strokeWidth="2" opacity={secondary/100*.3}/>})}<path d="M300 194 V214 M290 204 H310" stroke="#b9b0a755"/><text x="300" y="232" textAnchor="middle" fill="#8c888a" fontSize="9">ANTISOLAR POINT · {radius}°</text></svg>}
