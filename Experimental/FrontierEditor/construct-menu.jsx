import React,{useEffect,useRef,useState} from 'react';
import {createPortal} from 'react-dom';
import {Box,Shapes,Sun,Camera,Layers,Search,X,Plus,ArrowRight,ArrowLeft,Check,LoaderCircle,Orbit,Wind,Mountain,Palette} from 'lucide-react';
import './construct-menu.css';
import OutlinerIcon from './outliner-icons.jsx';
export const constructItems=[
 {kind:0,name:'Cube',category:'Geometry',detail:'Six faces · unit cube',triangles:12},
 {kind:1,name:'Sphere',category:'Geometry',detail:'Smooth latitude mesh',triangles:960},
 {kind:2,name:'Cylinder',category:'Geometry',detail:'32 sides · capped',triangles:128},
 {kind:3,name:'Cone',category:'Geometry',detail:'32 sides · capped',triangles:64},
 {kind:4,name:'Plane',category:'Geometry',detail:'Flat surface · XY',triangles:2},
 {kind:5,name:'Torus',category:'Geometry',detail:'32 × 12 ring mesh',triangles:768},
 {kind:6,name:'Area emitter',category:'Lighting',detail:'Emissive mesh · 1,000 nits',triangles:2},
 {kind:7,name:'Camera',category:'Scene',detail:'Perspective camera record'},
 {kind:8,name:'Empty entity',category:'Scene',detail:'Transform · no geometry'},
];
export async function nativeWorld(request){
 const response=await fetch('/api/construct',request?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(request)}:{});
 let data;try{data=await response.json()}catch{throw Error('Start the native authoring bridge to create engine entities.')}
 if(!response.ok||!data.ok)throw Error(data.error||'Native scene is unavailable.');return data;
}
export function ConstructGlyph({kind,id='glyph'}){
 const color=['#9ba9fd','#a5b9d0','#a9b9f2','#ecac8a','#90c9bd','#c3a1ec','#f2cf80','#aeaff4','#8fa8ba'][kind];
 return <svg viewBox="0 0 80 64" fill="none" aria-hidden="true" style={{color}}><defs><linearGradient id={id} x1="15" y1="8" x2="65" y2="62" gradientUnits="userSpaceOnUse"><stop stopColor={color}/><stop offset="1" stopColor="#303746"/></linearGradient><radialGradient id={id+'s'} cx=".34" cy=".27" r=".76"><stop stopColor="#e0e9ef"/><stop offset=".45" stopColor={color}/><stop offset="1" stopColor="#303544"/></radialGradient></defs>
 {kind===0&&<><path d="M40 6 65 20 65 47 40 61 15 47 15 20Z" fill={'url(#'+id+')'} stroke={color}/><path d="M15 20 40 34 65 20M40 34V61" stroke="#242b40" strokeWidth="1.5"/><path d="M40 6 65 20 40 34 15 20Z" fill="#bec8ff" fillOpacity=".3"/></>}
 {kind===1&&<><circle cx="40" cy="33" r="26" fill={'url(#'+id+'s)'}/><ellipse cx="40" cy="33" rx="26" ry="10" stroke="#dbe4f7" opacity=".2"/><ellipse cx="40" cy="33" rx="11" ry="26" stroke="#dbe4f7" opacity=".2"/></>}
 {kind===2&&<><path d="M17 17V48C17 63 63 63 63 48V17Z" fill={'url(#'+id+')'} stroke={color}/><ellipse cx="40" cy="17" rx="23" ry="10" fill="#bbc8ee" stroke={color}/></>}
 {kind===3&&<><path d="M40 5 13 50C16 65 64 65 67 50Z" fill={'url(#'+id+')'} stroke={color}/><path d="M40 5 25 53" stroke="#f8d6ba" opacity=".3"/></>}
 {kind===4&&<><path d="M10 38 40 17 70 34 40 55Z" fill={'url(#'+id+')'} stroke={color}/><path d="m20 31 30 18m-20-25 30 18M20 43l30-21M30 49l30-21" stroke={color} opacity=".25"/></>}
 {kind===5&&<><ellipse cx="40" cy="35" rx="22" ry="16" stroke="#544368" strokeWidth="15"/><ellipse cx="40" cy="30" rx="22" ry="16" stroke={'url(#'+id+')'} strokeWidth="13"/><path d="M17 28C19 6 60 6 63 29" stroke="#decbff" strokeWidth="2" opacity=".6"/></>}
 {kind===6&&<><path d="m14 21 28-13 24 12-28 14Z" fill="#ffe8ad"/><path d="m14 24 24 13 28-14M22 38l-4 9m20-4v12m16-18 4 10" stroke={color} strokeWidth="2" strokeLinecap="round"/></>}
 {kind===7&&<><rect x="13" y="17" width="40" height="33" rx="7" fill={'url(#'+id+')'} stroke={color}/><path d="m54 28 14-9v29l-14-8Z" fill="#7079a4" stroke={color}/><circle cx="33" cy="33" r="9" fill="#262d43" stroke="#cbd3ff"/><circle cx="33" cy="33" r="4" fill="#7084b5"/></>}
 {kind===8&&<><path d="M40 6v50M15 44l49-24M16 20l48 24" stroke={color} strokeWidth="2"/><circle cx="40" cy="32" r="6" fill="#333c49" stroke="#cfdae8"/><circle cx="40" cy="6" r="3" fill="#a9b9e9"/><circle cx="64" cy="44" r="3" fill="#cc9292"/><circle cx="15" cy="44" r="3" fill="#a1ccac"/></>}
 </svg>;
}
export default function ConstructMenu({onClose,onCreated,onOrganize,entities=[],onInspect,properties}){
 const [category,setCategory]=useState('Geometry'),[query,setQuery]=useState(''),[kind,setKind]=useState(0),[name,setName]=useState(''),[position,setPosition]=useState(['0','0','0']),[size,setSize]=useState('1'),[busy,setBusy]=useState(false),[error,setError]=useState(''),[stay,setStay]=useState(true),[success,setSuccess]=useState(''),[step,setStep]=useState(false),[existing,setExisting]=useState(null);
 const dialog=useRef(null),search=useRef(null),back=useRef(null);const item=constructItems[kind];
 const catalogue=[...constructItems,...entities.filter(e=>!['cube','sphere','cylinder','plane'].includes(e.id)&&e.group!=='Water bodies').map(e=>({...e,name:e.id==='camera'?'Camera controls':e.name,existing:true,category:e.group==='Environment'?(['cloud','precip','air','local-cloud','local-fog'].includes(e.id)?'Weather':'Environment'):e.group==='Materials'?'Materials':e.group==='Scene'?'Scene':'World'}))];
 const closeRef=useRef(onClose);closeRef.current=onClose;
 useEffect(()=>{const previous=document.activeElement;search.current?.focus({preventScroll:true});return()=>previous?.focus()},[]);
 useEffect(()=>{if(step)back.current?.focus({preventScroll:true});else search.current?.focus({preventScroll:true})},[step]);
 function goBack(){if(busy)return;setStep(false);onInspect(null);setError('');setSuccess('')}
 function choose(entry){setExisting(entry.existing?entry:null);if(!entry.existing){setKind(entry.kind);setName('');onInspect(null)}else onInspect(entry.id);setError('');setSuccess('');setStep(true)}
 function keys(e){if(e.key==='Escape'&&!busy){e.stopPropagation();step?goBack():closeRef.current()}if(e.key==='Tab'){const items=[...dialog.current.querySelectorAll('button:not(:disabled),input:not(:disabled),select:not(:disabled),a[href]')].filter(el=>!el.closest('[inert]')&&el.getClientRects().length);const first=items[0],last=items.at(-1);if(e.shiftKey&&document.activeElement===first){e.preventDefault();last?.focus()}else if(!e.shiftKey&&document.activeElement===last){e.preventDefault();first?.focus()}}}
 async function create(e){e.preventDefault();if(busy)return;setBusy(true);setError('');setSuccess('');try{const data=await nativeWorld({kind,name,position:position.map(Number),size:Number(size)});onCreated(data);setSuccess(data.entities.at(-1).name+' added to native world and outliner');if(!stay)onClose()}catch(e){setError(e.message)}finally{setBusy(false)}}
 const shown=catalogue.filter(i=>query?i.name.toLowerCase().includes(query.toLowerCase()):category==='All'||i.category===category);
 return createPortal(<div className="construct-backdrop" onMouseDown={e=>{if(e.target===e.currentTarget&&!busy)onClose()}}><section ref={dialog} onKeyDown={keys} role="dialog" aria-modal="true" aria-labelledby="construct-title" className="construct-panel construct-sliding">
 <header><div className="construct-emblem"><Box size={19}/></div><div><h2 id="construct-title">Construct</h2><p>{step?'Configure your entity':'Choose something for your world'}</p></div><span className="construct-engine">FRONTIER / EDITOR</span><button className="construct-close" onClick={onClose} disabled={busy} aria-label="Close Construct"><X size={17}/></button></header>
 <div className="construct-context"><span className={!step?'current':''}>01 &nbsp; Entities</span><ArrowRight size={12}/><span className={step?'current':''}>02 &nbsp; Properties</span><span className="construct-context-end">{step?(existing?'Existing editor entity':'Native scene creation'):'Geometry · environment · scene'}</span></div>
 <div className="construct-slide-window"><div className={'construct-slide-track '+(step?'show-properties':'')}>
 <div className="construct-step construct-catalogue" inert={step?'':undefined} aria-hidden={step}>
 <div className="construct-layout"><nav aria-label="Entity categories">{[['All',Layers],['Geometry',Shapes],['Lighting',Sun],['Environment',Orbit],['Weather',Wind],['World',Mountain],['Materials',Palette],['Scene',Camera]].map(([label,Icon])=><button key={label} onClick={()=>{setCategory(label);setQuery('')}} className={category===label&&!query?'active':''}><Icon size={16}/>{label}<small>{label==='All'?catalogue.length:catalogue.filter(i=>i.category===label).length}</small></button>)}<div className="construct-nav-caption">WORKSPACE</div><button onClick={()=>{setCategory('Organize');setQuery('')}} className={category==='Organize'?'active':''}><Layers size={16}/>Organize</button></nav>
 <div className="construct-content"><label className="construct-search"><Search size={15}/><input ref={search} aria-label="Search construct catalogue" placeholder="Search all entities…" value={query} onChange={e=>setQuery(e.target.value)}/><kbd>⌕</kbd></label>
 <div className="construct-section-title">{query?'Search results':category}<span>{shown.length?String(shown.length).padStart(2,'0'):''}</span></div>
 {category==='Organize'&&!query?<div className="construct-organize"><p>Workspace containers organize outliner references, not engine geometry.</p>{['folder','collection'].map(k=><button key={k} onClick={()=>onOrganize(k)}><Layers size={22}/>{k}<Plus size={15}/></button>)}</div>:<div className="construct-grid">{shown.map(i=><button key={i.existing?i.id:i.kind} className="construct-tile" onClick={()=>choose(i)} aria-label={'Configure '+i.name}>{i.existing?<OutlinerIcon object={i} size={54}/>:<ConstructGlyph kind={i.kind} id={'construct-'+i.kind}/>}<strong>{i.name}</strong><span>{i.existing?'Edit existing':i.triangles?`${i.triangles.toLocaleString()} triangles`:'Scene entity'}</span><ArrowRight className="construct-tile-arrow" size={12}/></button>)}{!shown.length&&<p className="construct-no-results">No matching entities.</p>}</div>}
 <p className="construct-caveat">Select an entity to open its properties. Environment and world entries edit the existing editor controls; native creation is labelled separately. Water / fluids remain deferred.</p>
 </div></div></div>
 <div className="construct-step construct-detail" inert={!step?'':undefined} aria-hidden={!step}>
 <div className="construct-backbar"><button ref={back} onClick={goBack} disabled={busy} aria-label="Back to entities"><ArrowLeft size={15}/> Entities</button><span>{existing?.name||item.name}</span><small>{existing?'EDITOR CONTROLS':'NEW ENTITY'}</small></div>
 <div className="construct-detail-body">{existing?<div className="construct-existing-properties">{properties}</div>:<><div className="construct-property-hero"><ConstructGlyph kind={kind} id="construct-detail-glyph"/><div><span className="construct-section-title">{item.category}</span><h3>{item.name}</h3><p>{item.detail}</p></div></div><form onSubmit={create}><div className="construct-properties"><label>Name<input aria-label="New entity name" maxLength={64} placeholder={item.name} value={name} onChange={e=>setName(e.target.value)}/></label><div><span>Position · m</span><div className="construct-xyz">{position.map((value,i)=><label key={i}><b>{'XYZ'[i]}</b><input aria-label={'Position '+'XYZ'[i]} type="number" min="-100000" max="100000" step="any" required value={value} onChange={e=>setPosition(p=>p.map((v,j)=>i===j?e.target.value:v))}/></label>)}</div></div><label>Size · m<input aria-label="Entity size" type="number" min="0.001" max="10000" step="any" required value={size} onChange={e=>setSize(e.target.value)}/></label></div>
 <div className="construct-feedback" aria-live="polite">{error?<span className="error">{error}</span>:success?<span className="success"><Check size={13}/>{success}</span>:<span>{item.detail} <span className="construct-feedback-muted">· CPU scene authoring</span></span>}</div>
 <footer><label><input type="checkbox" checked={stay} onChange={e=>setStay(e.target.checked)}/> Keep open</label><span><kbd>Esc</kbd> close</span><button className="construct-submit" type="submit" disabled={busy}>{busy?<LoaderCircle className="construct-spin" size={16}/>:<Plus size={16}/>} {busy?'Creating…':'Create '+item.name}</button></footer></form></>}</div>
 </div></div></div>
 </section></div>,document.body);
}
export function NativeEntityInspector({entity}){
 const item=constructItems[entity.kind];
 return <section className="card native-entity-card"><div className="card-heading"><span><Box size={16}/>Native scene entity</span></div><div className="native-entity-preview"><ConstructGlyph kind={entity.kind} id={'inspect-'+entity.placement}/></div><dl><dt>Scene placement</dt><dd>#{entity.placement}</dd><dt>Type</dt><dd>{item.name}</dd><dt>World position</dt><dd>{entity.position.join(' / ')} m</dd><dt>Size</dt><dd>{entity.size} m</dd><dt>Geometry</dt><dd>{entity.triangles.toLocaleString()} triangles</dd><dt>Storage</dt><dd>Native C++ SceneStructure</dd></dl><p>Persisted in the native authoring scene. Transform editing and the live Vulkan game-process connection are not connected in this browser inspector.</p></section>;
}
