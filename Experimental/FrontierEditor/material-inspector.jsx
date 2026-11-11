import React,{useEffect,useRef,useState} from 'react';
import {Circle,Palette,SlidersHorizontal,Check,Minus,RotateCcw,Sparkles} from 'lucide-react';
import {materialChannels,readMaterial} from './material-model.js';
const rgb=hex=>[1,3,5].map(i=>parseInt(hex.slice(i,i+2),16)/255);
export function MaterialSphere({material}){
 const canvas=useRef(null);
 useEffect(()=>{
  const c=canvas.current,ctx=c.getContext('2d');if(!ctx)return;
  const n=c.width,img=ctx.createImageData(n,n),data=img.data;
  const p=(key,fallback)=>material[key]?.enabled?material[key].value:fallback;
  const base=rgb(p('baseColor','#b6b6b6')),metal=p('metallic',0),rough=p('roughness',.5),normal=p('normal',0),height=p('height',0),ao=p('ao',0),opacity=p('opacity',1),trans=p('transmission',0),ior=p('ior',1.5),spec=p('specular',.5),coat=p('coat',0),coatR=p('coatRoughness',.1),sheen=p('sheen',0),anis=p('anisotropy',0),angle=p('anisotropyRotation',0)*Math.PI/180,sss=p('subsurface',0),thick=p('thickness',.5),iri=p('iridescence',0),film=p('iridescenceThickness',400);
  const tint=rgb(p('specularTint','#ffffff')),sheenColor=rgb(p('sheenTint','#ffffff')),scatter=rgb(p('subsurfaceColor','#e9ac90')),emission=rgb(p('emission','#000000')),gain=p('emissionStrength',1);
  for(let y=0;y<n;y++)for(let x=0;x<n;x++){
   const i=(y*n+x)*4,nx=(x/n-.5)*2.6,ny=(.46-y/n)*2.6,r2=nx*nx+ny*ny;
   const bg=[.085,.09,.1];let color=bg;
   if(r2<=1){
    let z=Math.sqrt(1-r2),xx=nx,yy=ny;
    const texture=(Math.sin(nx*48)*Math.sin(ny*43))*normal*.09+Math.sin(nx*17+ny*22)*height*.06;
    xx+=texture;yy+=texture*.6;const length=Math.hypot(xx,yy,z);xx/=length;yy/=length;z/=length;
    const ndl=Math.max(0,-xx*.48+yy*.62+z*.62),ndh=Math.max(0,-xx*.27+yy*.35+z*.9),rim=Math.pow(1-z,3);
    const f0=Math.pow((ior-1)/(ior+1),2)*2*spec;
    const directional=1+anis*Math.cos(Math.atan2(yy,xx)-angle)*.6;
    const highlight=Math.pow(ndh,Math.max(2,2/(rough*rough)))*directional;
    const clear=Math.pow(ndh,Math.max(10,2/(coatR*coatR)))*coat*.65;
    const reflection=Math.pow(Math.max(0,-xx*.7+yy*.15+z*.55),6)*.65+Math.pow(Math.max(0,xx*.5+yy*.1+z*.4),10)*.35;
    const transmission=trans*(.3+(1-z)*.35)/(1+thick*.8);
    color=base.map((b,k)=>{
     const diffuse=b*(.19+ndl*.7)*(1-metal*.85)*(1-ao*Math.pow(Math.max(0,-yy),2)*.65);
     const reflected=(metal?b:1)*(highlight*(f0*(1-metal)+metal*.75)*3+reflection*metal);
     const rainbow=(.5+.5*Math.cos((1-z)*11+film*.015+k*2.1))*iri*rim*.5;
     const v=diffuse+reflected*tint[k]+clear+sheen*rim*sheenColor[k]*.5+sss*scatter[k]*Math.pow(Math.max(0,-ndl+.55),2)*(1-thick*.5)+rainbow+emission[k]*gain;
     const lit=v*(1-transmission)+(.17+reflection*.5)*transmission;
     return bg[k]+(lit-bg[k])*opacity;
    });
   } else {const shadow=Math.exp(-((nx/.88)**2+( (ny+1.09)/.13)**2))* .05;color=bg.map(v=>v-shadow)}
   color.forEach((v,k)=>data[i+k]=Math.round(Math.min(1,Math.max(0,v))*255));data[i+3]=255;
  }
  ctx.putImageData(img,0,0);
 },[material]);
 return <canvas className="material-sphere" ref={canvas} width="260" height="260" role="img" aria-label="Quick material sphere preview under fixed studio lighting"/>;
}
export default function MaterialInspector({card,v,set,values}){
 const [selected,setSelected]=useState('baseColor');
 const dependencies={emissionStrength:'emission',coatRoughness:'coat',sheenTint:'sheen',anisotropyRotation:'anisotropy',subsurfaceColor:'subsurface',iridescenceThickness:'iridescence'};const c=materialChannels.find(c=>c.id===selected),mat=readMaterial(values),enabled=mat[selected].enabled;
 return <>
 {card('Quick PBR preview',Circle,<><MaterialSphere material={mat}/><div className="material-preview-caption"><span>Studio sphere</span><span>{materialChannels.filter(c=>mat[c.id].enabled).length} / 24 channels</span></div><p className="material-disclaimer">Approximate shading preview · procedural detail, no texture maps.</p></>,'material-preview-card')}
 {card(c.name,SlidersHorizontal,<><div className="channel-editor-top"><span className="eyebrow">SELECTED CHANNEL</span><button className={`enabled-pill ${enabled?'':'disabled'}`} aria-label={`Toggle selected ${c.name}`} aria-pressed={enabled} onClick={()=>set('channel:'+selected,!enabled)}><span/>{enabled?'Enabled':'Disabled'}</button></div><div className="channel-editor-value">{c.type==='color'?<label className="material-color"><input type="color" aria-label={`${c.name} color`} value={v('mat:'+selected)} disabled={!enabled} onChange={e=>set('mat:'+selected,e.target.value)}/><span>{v('mat:'+selected).toUpperCase()}</span></label>:<><div className="metric">{Number(v('mat:'+selected)).toFixed(c.max>3?0:2)}<small>{selected==='iridescenceThickness'?'nm':selected==='anisotropyRotation'?'°':''}</small></div><input type="range" aria-label={`${c.name} value`} min={c.min} max={c.max} step={c.max>3?1:.01} disabled={!enabled} value={v('mat:'+selected)} onChange={e=>set('mat:'+selected,+e.target.value)} style={{'--progress':`${(v('mat:'+selected)-c.min)/(c.max-c.min)*100}%`}}/><div className="range-labels"><span>{c.min}</span><span>{c.max}</span></div></>}</div><p className="material-channel-note">{enabled?'Edits update the sphere immediately.':'Disabled channels keep their settings but do not contribute to the preview.'}</p>{dependencies[selected]&&!mat[dependencies[selected]].enabled&&<div className="channel-dependency">Enable {materialChannels.find(c=>c.id===dependencies[selected]).name} to see this channel’s effect.</div>}<button className="channel-reset" onClick={()=>set('mat:'+selected,c.value)}><RotateCcw size={12}/>Reset channel value</button></>,'material-editor-card')}
 {card('Material channels',Palette,<><p className="muted">Select a channel to edit it. Use its switch to include or exclude it.</p><div className="material-channel-grid">{materialChannels.map(channel=><div className={`material-channel ${selected===channel.id?'channel-selected':''} ${mat[channel.id].enabled?'channel-on':''}`} key={channel.id}><button className="channel-pick" aria-pressed={selected===channel.id} onClick={()=>setSelected(channel.id)}><span className="channel-dot" style={{background:channel.type==='color'?mat[channel.id].value:undefined}}/>{channel.name}</button><button className="channel-enable" aria-label={`Toggle ${channel.name} channel`} aria-pressed={mat[channel.id].enabled} onClick={()=>set('channel:'+channel.id,!mat[channel.id].enabled)}>{mat[channel.id].enabled?<Check size={12}/>:<Minus size={12}/>}</button></div>)}</div></>,'wide-card')}
 </>;
}
