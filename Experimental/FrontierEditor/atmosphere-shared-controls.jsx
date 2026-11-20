import React from 'react';
import {Cloud,CloudFog,Layers,MoveUpRight} from 'lucide-react';
import {CloudCoverage,CloudAltitude} from './property-graphics.jsx';
import {GroundFogProfile} from './weather-graphics.jsx';

// Shared by global and local effects: only bounds/position are local additions.
export function CloudControls({card,v,set,slider,num}){return <>
    {card('Cloud coverage',Cloud,<><div className="scattering-top"><div><div className="metric">{num('clouds','%')}</div><p className="muted">Volumetric layer coverage</p></div><span className="small-pill">{v('clouds')<20?'Mostly clear':v('clouds')<60?'Scattered':'Overcast'}</span></div><CloudCoverage coverage={v('clouds')} thickness={v('thickness')}/>{slider('clouds',0,100)}<div className="range-labels"><span>Clear sky</span><span>Full coverage</span></div></>,'wide-card')}
    {card('Cloud base',MoveUpRight,<><div className="scattering-top"><div className="metric">{num('cloudBase','km',1)}</div><span className="small-pill">{v('cloudBase')<2?'Low level':v('cloudBase')<6?'Mid level':'High level'}</span></div><p className="muted">Altitude above ground · AGL</p><CloudAltitude base={v('cloudBase')} thickness={v('thickness')} onChange={height=>set('cloudBase',height)}/>{slider('cloudBase',.2,10,.1)}<div className="range-labels"><span>0.2 km</span><span>10 km</span></div><div className="altitude-presets" role="group" aria-label="Cloud altitude presets">{[['Low',.8],['Mid',3],['High',7]].map(([label,height])=><button key={label} aria-pressed={v('cloudBase')===height} onClick={()=>set('cloudBase',height)}>{label}<span>{height.toFixed(1)} km</span></button>)}</div><div className="cloud-altitude-details"><div><span>Layer top</span><strong>{(v('cloudBase')+v('thickness')).toFixed(1)}<small> km</small></strong></div><div><span>Thickness</span><strong>{v('thickness').toFixed(1)}<small> km</small></strong></div></div></>,'cloud-base-card')}
    {card('Layer thickness',Layers,<><div className="metric">{num('thickness','km',1)}</div><p className="muted">Vertical cloud development</p><div className="cloud-layers">{[0,1,2,3,4].map(i=><i key={i} style={{transform:`translateY(${(i-2)*v('thickness')*4}px)`,opacity:.7-i*.1}}/>)}</div>{slider('thickness',.2,6,.1)}<div className="range-labels"><span>Thin veil</span><span>Towering</span></div></>)}
</>}
export function FogControls({card,v,slider,num}){return <>
       {card('Ground fog',CloudFog,<><div className="metric">{num('fog','%')}</div><p className="muted">How quickly distant detail fades into mist</p><GroundFogProfile density={v('fog')}/>{slider('fog',0,100)}<div className="range-labels"><span>Clear horizon</span><span>Dense mist</span></div></>)}
</>}
