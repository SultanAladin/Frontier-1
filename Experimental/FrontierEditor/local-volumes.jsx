import React,{useEffect,useState} from 'react';
import {Box,Cloud,CloudFog} from 'lucide-react';
export const localVolumeObjects=[
 {id:'local-cloud',name:'Local Volumetric Clouds',type:'Bounded cloud volume',group:'Environment',icon:Cloud,color:'#9bbfda'},
 {id:'local-fog',name:'Local Fog',type:'Bounded fog volume',group:'Environment',icon:CloudFog,color:'#a1c7bf'}
];
const shapes=['Box','Sphere','Cylinder','Cone','Custom'];
const defaultOutline=[[-1,-1],[.4,-1],[1,-.3],[.6,1],[-1,.6]];
const formatOutline=points=>points.map(p=>p.join(', ')).join('\n');
export function parseVolumeOutline(text){
 const points=text.trim().split('\n').map(line=>line.split(',').map(n=>n.trim()));
 if(points.length<3||points.length>16||points.some(p=>p.length!==2||p.some(n=>n===''||!Number.isFinite(Number(n))||Math.abs(Number(n))>1)))throw new Error('Enter 3–16 X, Z pairs, each between −1 and 1.');
 const p=points.map(pair=>pair.map(Number)),cross=(a,b,c)=>(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
 const on=(a,b,c)=>Math.abs(cross(a,b,c))<1e-8&&c[0]>=Math.min(a[0],b[0])-1e-8&&c[0]<=Math.max(a[0],b[0])+1e-8&&c[1]>=Math.min(a[1],b[1])-1e-8&&c[1]<=Math.max(a[1],b[1])+1e-8;
 for(let i=0;i<p.length;i++){
  const a=p[i],b=p[(i+1)%p.length];
  if(Math.hypot(a[0]-b[0],a[1]-b[1])<1e-6)throw new Error('Outline vertices must be distinct. The closing edge is automatic.');
  for(let j=i+1;j<p.length;j++){
   if(j===i+1||(i===0&&j===p.length-1))continue;
   const c=p[j],d=p[(j+1)%p.length];
   if((cross(a,b,c)*cross(a,b,d)<0&&cross(c,d,a)*cross(c,d,b)<0)||on(a,b,c)||on(a,b,d)||on(c,d,a)||on(c,d,b))throw new Error('Outline edges cannot cross or touch.');
  }
 }
 const area=p.reduce((sum,a,i)=>{const b=p[(i+1)%p.length];return sum+a[0]*b[1]-b[0]*a[1]},0);
 if(Math.abs(area)<1e-5)throw new Error('The outline must enclose an area.');
 return p;
}
function BoundsDiagram({shape,points}){
 const project=(p,top)=>`${100+p[0]*48+p[1]*19},${(top?30:85)+p[1]*12}`;
 return <svg viewBox="0 0 200 120" role="img" aria-label={`${shape} volume bounds schematic`} fill="none" stroke="currentColor" strokeWidth="1.5">
 {shape==='Sphere'?<><circle cx="100" cy="60" r="46"/><ellipse cx="100" cy="60" rx="46" ry="15" strokeOpacity=".5"/><ellipse cx="100" cy="60" rx="18" ry="46" strokeOpacity=".35"/></>:shape==='Cylinder'||shape==='Cone'?<><ellipse cx="100" cy="91" rx="48" ry="15" strokeOpacity=".5"/>{shape==='Cylinder'?<><ellipse cx="100" cy="28" rx="48" ry="15"/><path d="M52 28V91 M148 28V91"/></>:<path d="M52 91 100 13 148 91"/>}</>:<><polygon points={points.map(p=>project(p,true)).join(' ')} fill="currentColor" fillOpacity=".09"/><polygon points={points.map(p=>project(p,false)).join(' ')} strokeOpacity=".4" strokeDasharray="3 3"/>{points.map((p,i)=><path key={i} d={`M${project(p,true)} L${project(p,false)}`} strokeOpacity=".6"/>)}</>}
 </svg>;
}
export default function LocalVolumeInspector({kind,card,v,set}){
 const cloud=kind==='local-cloud';
 const defaults={volumeX:0,volumeY:cloud?500:20,volumeZ:0,volumeWidth:cloud?1000:200,volumeHeight:cloud?400:40,volumeDepth:cloud?1000:200,volumeRadius:cloud?200:20,volumeShape:'Box'};
 const value=k=>v(k)??defaults[k],shape=shapes.includes(value('volumeShape'))?value('volumeShape'):'Box';
 const outline=v('volumeOutline')??defaultOutline,serialized=formatOutline(outline);
 const [draft,setDraft]=useState(serialized),[error,setError]=useState('');
 useEffect(()=>{setDraft(serialized);setError('')},[serialized]);
 if(!['local-cloud','local-fog'].includes(kind))return null;
 const number=(key,label,min)=> <label key={key}><span>{label}</span><div><input aria-label={label} type="number" step="any" min={min} value={value(key)} onChange={e=>{const n=e.target.valueAsNumber;if(Number.isFinite(n))set(key,min===undefined?n:Math.max(min,n))}}/><small>m</small></div></label>;
 const custom=shape==='Custom',box=shape==='Box';
 return card('Volume bounds & position',Box,<>
 <div className="volume-summary"><BoundsDiagram shape={shape} points={custom?outline:[[-1,-1],[1,-1],[1,1],[-1,1]]}/><div><span className="small-pill">LOCAL · {shape.toUpperCase()} VOLUME</span><p className="muted">{cloud?'Contained cloud bank.':'Local pocket of mist.'} The selected shape defines the boundary.</p></div></div>
 <label className="volume-shape-label"><span>Volume shape</span><select aria-label="Volume shape" value={shape} onChange={e=>{set('volumeShape',e.target.value);setError('')}}>{shapes.map(s=><option key={s} value={s}>{s==='Custom'?'Custom outline':s}</option>)}</select></label>
 <div className="volume-subheading">DIMENSIONS <span>Meters · centered on position</span></div>
 <div className="volume-fields">{box||custom?<>{number('volumeWidth','Width',.1)}{number('volumeHeight','Height',.1)}{number('volumeDepth','Depth',.1)}</>:<>{number('volumeRadius','Radius',.1)}{shape!=='Sphere'&&number('volumeHeight','Height',.1)}</>}</div>
 {custom&&<div className="volume-custom"><label htmlFor="volume-outline">Custom footprint · X, Z vertices</label><p className="muted">Closed outline extruded through Height. Coordinates −1 to 1 span the Width and Depth; one pair per line. The last point connects to the first.</p><textarea id="volume-outline" aria-label="Custom footprint vertices" value={draft} rows={5} spellCheck="false" onChange={e=>{setDraft(e.target.value);setError('')}}/><button type="button" onClick={()=>{try{const points=parseVolumeOutline(draft);set('volumeOutline',points);setDraft(formatOutline(points));setError('')}catch(e){setError(e.message)}}}>Apply outline</button>{error&&<p role="alert">{error}</p>}<p className="muted">{draft!==serialized?'Unapplied edits — preview and saved shape use the last applied outline.':'Outline applied · schematic above.'}</p></div>}
 <div className="volume-subheading">POSITION <span>World-space center · meters</span></div>
 <div className="volume-fields">{number('volumeX','Center X')}{number('volumeY','Center Y')}{number('volumeZ','Center Z')}</div>
 <p className="muted">Bounds configuration only · rendering requires engine integration.</p>
 </>,'wide-card');
}
