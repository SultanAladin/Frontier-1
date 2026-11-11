import React,{useEffect,useRef,useState} from 'react';
import {Upload,Download,X} from 'lucide-react';
function BakeIcon({size=18}){return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d="M12 5h7a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-6M2 9h11m-4-4 4 4-4 4M3 2v3M1.5 3.5h3"/><path d="M14 14h3v3h-3z"/></svg>}

const targets={sun:[['sun','Sun lighting'],['sun-disk','Sun disk']],sky:[['atmosphere','Atmosphere']],rainbow:[['rainbow','Rainbow']],flare:[['lens-flare','Lens flare']],stars:[['stars','Stars']]};
const maxImageBytes=256*1024;
const supported=['image/png','image/jpeg','image/webp'];
const cleanSettings=settings=>Object.fromEntries(Object.entries(settings).filter(([key])=>!key.startsWith('bake:')));

function BakeTile({object,target,label,value,settings,onChange,allValues}){
 const state=value||{},asset=state.asset,input=useRef(),alive=useRef(true),revision=useRef(0);
 const [busy,setBusy]=useState(false),[error,setError]=useState('');
 useEffect(()=>{alive.current=true;return()=>{alive.current=false;revision.current++}},[]);
 const useBaked=state.mode==='baked'&&!!asset;
 const settingsJSON=JSON.stringify(cleanSettings(settings));
 const changed=state.request&&JSON.stringify(state.request.settings)!==settingsJSON;
 const requestBake=()=>{
  setError('');
  onChange({...state,request:{version:1,status:'pending-renderer',objectId:object.id,target,requestedAt:new Date().toISOString(),settings:cleanSettings(settings)}});
 };
 const exportRequest=()=>{
  if(!state.request)return;
  const url=URL.createObjectURL(new Blob([JSON.stringify(state.request,null,2)],{type:'application/json'}));
  const a=document.createElement('a');a.href=url;a.download=`frontier-${target}-bake-request.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
 };
 const loadImage=async event=>{
  const file=event.target.files?.[0];event.target.value='';if(!file)return;
  setError('');
  if(!supported.includes(file.type)){setError('Choose a PNG, JPEG or WebP image.');return;}
  if(file.size>maxImageBytes){setError('Use an image up to 256 KB for this local editor preview.');return;}
  const token=++revision.current;setBusy(true);
  try{
   const src=await new Promise((resolve,reject)=>{const reader=new FileReader();reader.onload=()=>resolve(reader.result);reader.onerror=()=>reject(new Error('Could not read the image.'));reader.readAsDataURL(file)});
   const dimensions=await new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>resolve({width:image.naturalWidth,height:image.naturalHeight});image.onerror=()=>reject(new Error('This file could not be decoded as an image.'));image.src=src});
   if(!alive.current||token!==revision.current)return;
   if(!dimensions.width||!dimensions.height||dimensions.width>8192||dimensions.height>8192)throw new Error('Use an image no larger than 8192 × 8192.');
   const existing=Object.values(allValues).flatMap(v=>Object.entries(v||{}).filter(([k])=>k.startsWith('bake:')).map(([,v])=>v?.asset?.src?.length||0)).reduce((a,b)=>a+b,0);
   if(existing-(asset?.src?.length||0)+src.length>1500000)throw new Error('Baked-image storage is full. Remove another image or use a smaller file.');
   onChange({...state,asset:{name:file.name,src,type:file.type,...dimensions},mode:'baked'});
  }catch(e){if(alive.current&&token===revision.current)setError(e.message||'Unable to load this image.');}
  finally{if(alive.current&&token===revision.current)setBusy(false);}
 };
 return <section className="card bake-quick-tile" aria-label={`${label} bake controls`} data-bake-target={target}>
  <div className="card-heading"><span><BakeIcon size={16}/>{label} bake</span><span className="bake-source-caption">{useBaked?'Baked source':'Procedural source'}</span></div>
  <div className="bake-source-row"><label className={`property-switch bake-image-switch ${useBaked?'is-on':'is-off'}`}><input aria-label={`Use baked image for ${label}`} type="checkbox" checked={useBaked} disabled={!asset||busy} onChange={e=>onChange({...state,mode:e.target.checked?'baked':'procedural'})}/><span className="switch-icon"><BakeIcon/></span><span className="switch-name">Use baked image</span><span className="switch-state">{useBaked?'ON':'OFF'}</span></label><div className="bake-actions"><button className="bake-trigger" onClick={requestBake} disabled={busy} aria-label={`Bake ${label}`}><BakeIcon size={14}/>Bake</button><button className="bake-load" onClick={()=>input.current?.click()} disabled={busy} aria-label={`Load baked image for ${label}`}><Upload size={14}/>{busy?'Loading…':asset?'Replace image':'Load image'}</button><small>{!asset?'Load an image to enable.':'Switch off to use procedural.'}</small></div><input ref={input} className="bake-file-input" type="file" accept="image/png,image/jpeg,image/webp" aria-label={`Baked image file for ${label}`} onChange={loadImage}/></div>
  {asset&&<div className="bake-asset"><img src={asset.src} alt={`Baked ${label} preview`}/><span title={asset.name}>{asset.name}<small>{asset.width} × {asset.height}</small></span><button aria-label={`Remove baked image for ${label}`} disabled={busy} onClick={()=>onChange({...state,asset:null,mode:'procedural'})}><X size={13}/></button></div>}
  <div className="bake-state" role="status">{error?<span className="bake-error">{error}</span>:state.request?<><span>{changed?'Settings changed — request a new bake.':'Request recorded — renderer connection required.'}</span><button onClick={exportRequest} aria-label={`Export ${label} bake request`} title="Download renderer request"><Download size={13}/></button></>:<span>{asset?'Image stored locally; renderer not connected.':'Load an existing image, or request a renderer bake.'}</span>}</div>
 </section>;
}
export default function BakeQuickTiles({object,kind,values={},settings,onSet,allValues}){
 const list=targets[kind];if(!list)return null;
 return <div className="bake-quick-area"><div className="section-label bake-quick-label"><span>BAKING</span><small>Editor setup · no renderer connected</small></div><div className="bake-quick-grid">{list.map(([target,label])=><BakeTile key={object.id+':'+target} object={object} target={target} label={label} value={values['bake:'+target]} settings={settings} allValues={allValues} onChange={v=>onSet('bake:'+target,v)}/>)}</div></div>;
}
