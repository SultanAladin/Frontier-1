import React, {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import {createRoot} from 'react-dom/client';
import {
  Aperture, Box, Check, ChevronDown, ChevronRight, Circle, Command, Copy, Crosshair,
  Cylinder, Eye, EyeOff, Focus, Grid3X3, Hexagon, Layers3, Lock, Magnet, Menu, Minus,
  MousePointer2, Move3D, Orbit, PanelLeft, PanelRight, Plus, Redo2, Rotate3D, Save,
  Search, Settings2, Sparkles, Square, Spline, Trash2, Triangle, Undo2, Unlock, X, Zap
} from 'lucide-react';
import './cad.css';

const TOOLS = [
  {id:'select', name:'Select', key:'Q', icon:MousePointer2},
  {id:'move', name:'Move', key:'G', icon:Move3D},
  {id:'rotate', name:'Rotate', key:'R', icon:Rotate3D},
  {id:'scale', name:'Scale', key:'S', icon:Focus},
  {id:'curve', name:'Bezier curve', key:'C', icon:Spline},
  {id:'fillet', name:'Fillet', key:'F', icon:Circle},
  {id:'chamfer', name:'Chamfer', key:'B', icon:Triangle},
  {id:'loft', name:'Loft', key:'L', icon:Layers3},
  {id:'surface', name:'Patch surface', key:'P', icon:Grid3X3},
];
const treeSeed = [
  {id:'refs',name:'REFERENCE',kind:'group',open:true,children:[
    {id:'blueprint',name:'911 Blueprint',kind:'Canvas',visible:true,locked:true},
    {id:'symmetry',name:'XZ Symmetry',kind:'Construction plane',visible:true},
  ]},
  {id:'curves',name:'CURVES',kind:'group',open:true,children:[
    {id:'roof',name:'Roof Character',kind:'Bezier · G2',visible:true},
    {id:'belt',name:'Belt Line',kind:'NURBS · G2',visible:true},
    {id:'fender',name:'Front Fender Rail',kind:'NURBS · G1',visible:true},
    {id:'rocker',name:'Rocker Rail',kind:'Interpolated',visible:true},
    {id:'sections',name:'Body Sections',kind:'Curve set · 7',visible:true},
  ]},
  {id:'surfaces',name:'SURFACES',kind:'group',open:true,children:[
    {id:'hood',name:'Hood',kind:'4-side patch',visible:true},
    {id:'roofSurface',name:'Roof Canopy',kind:'G2 Loft',visible:true},
    {id:'quarter',name:'Rear Quarter',kind:'Network surface',visible:true},
    {id:'body',name:'Body Side',kind:'Rail loft',visible:true},
  ]},
  {id:'solids',name:'SOLIDS',kind:'group',open:true,children:[
    {id:'carBody',name:'Body Shell',kind:'Solid · 18 faces',visible:true},
    {id:'wheelFL',name:'Wheel FL',kind:'Revolved solid',visible:true},
    {id:'wheelRL',name:'Wheel RL',kind:'Revolved solid',visible:true},
  ]},
];
const curvePointsSeed = [
  {x:19,y:58,w:1},{x:27,y:46,w:.92},{x:39,y:36,w:1.1},{x:54,y:34,w:1},
  {x:67,y:38,w:.9},{x:78,y:48,w:1.08},{x:88,y:56,w:1},
];
const commandGroups = [
  {name:'SOLID',items:[['Extrude','E'],['Revolve','V'],['Sweep','W'],['Boolean union','U'],['Shell','H']]},
  {name:'SURFACE',items:[['Loft · Normal','L'],['Loft · Guide rails','⇧L'],['Loft · Centerline','⌥L'],['Boundary surface','N'],['Fill / Fair patch','P'],['Offset surface','O']]},
  {name:'CURVE',items:[['Bezier curve','C'],['NURBS control curve','⇧C'],['Interpolated curve','I'],['Arc · 3 point','A'],['Blend curve · G2','2'],['Project / Pull curve','J'],['Iso curve','K']]},
  {name:'DETAIL',items:[['Fillet','F'],['Variable fillet','⇧F'],['Chamfer','B'],['Bridge / blend','D']]},
];

function useHistory(initial) {
  const [stack,setStack]=useState([initial]);
  const [index,setIndex]=useState(0);
  const value=stack[index];
  const commit=useCallback(next=>{setStack(s=>[...s.slice(0,index+1),typeof next==='function'?next(s[index]):next]);setIndex(i=>i+1)},[index]);
  return {value,commit,canUndo:index>0,canRedo:index<stack.length-1,undo:()=>setIndex(i=>Math.max(0,i-1)),redo:()=>setIndex(i=>Math.min(stack.length-1,i+1)),count:index};
}

function Logo(){return <div className="cad-brand"><span className="brand-mark"><Layers3 size={19}/></span><span>frontier<span className="brand-dot">.</span></span><b>FORM</b></div>}
function IconButton({title,children,onClick,active,disabled}){return <button disabled={disabled} onClick={onClick} className={`icon-btn ${active?'active':''}`} title={title} aria-label={title}>{children}</button>}

function App(){
  const history=useHistory({points:curvePointsSeed,radius:6,continuity:'G2',degree:3});
  const model=history.value;
  const [selected,setSelected]=useState('roof');
  const [tree,setTree]=useState(treeSeed);
  const [tool,setTool]=useState('select');
  const [mode,setMode]=useState('Curve');
  const [palette,setPalette]=useState(false);
  const [paletteQuery,setPaletteQuery]=useState('');
  const [snap,setSnap]=useState(true);
  const [grid,setGrid]=useState(true);
  const [xray,setXray]=useState(false);
  const [saved,setSaved]=useState(true);
  const [toast,setToast]=useState('Ready');
  const [camera,setCamera]=useState({yaw:-8,pitch:2,zoom:1});
  const [drag,setDrag]=useState(null);
  const [pointDrag,setPointDrag]=useState(null);
  const [selectionMode,setSelectionMode]=useState('Object');
  const viewport=useRef(null);

  const selectedNode=useMemo(()=>tree.flatMap(g=>g.children).find(n=>n.id===selected) || tree[1].children[0],[tree,selected]);
  const notify=useCallback(message=>{setToast(message);window.clearTimeout(notify.timer);notify.timer=window.setTimeout(()=>setToast('Ready'),2200)},[]);
  const chooseTool=useCallback(id=>{setTool(id);const t=TOOLS.find(x=>x.id===id);notify(`${t?.name||id} active`)},[notify]);
  const save=useCallback(()=>{localStorage.setItem('frontier-form',JSON.stringify({tree,model,camera}));setSaved(true);notify('Model saved locally')},[tree,model,camera,notify]);

  useEffect(()=>{
    const down=e=>{
      if(/INPUT|TEXTAREA|SELECT/.test(e.target.tagName)) return;
      if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='s'){e.preventDefault();save();return}
      if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z'){e.preventDefault();e.shiftKey?history.redo():history.undo();return}
      if(e.code==='Space'){e.preventDefault();setPalette(true);return}
      if(e.key==='Escape'){setPalette(false);chooseTool('select');return}
      const match=TOOLS.find(t=>t.key.toLowerCase()===e.key.toLowerCase());if(match)chooseTool(match.id);
      if(e.key==='1')setSelectionMode('Object');if(e.key==='2')setSelectionMode('Edge');if(e.key==='3')setSelectionMode('Face');
    };
    window.addEventListener('keydown',down);return()=>window.removeEventListener('keydown',down);
  },[chooseTool,history,save]);

  const toggleNode=(id,field='visible')=>{setTree(groups=>groups.map(g=>({...g,children:g.children.map(n=>n.id===id?{...n,[field]:!n[field]}:n)})));setSaved(false)};
  const toggleGroup=id=>setTree(groups=>groups.map(g=>g.id===id?{...g,open:!g.open}:g));
  const updateModel=(patch,label)=>{history.commit({...model,...patch});setSaved(false);notify(label||'Parameter updated')};
  const updatePoint=(i,p)=>{const points=model.points.map((pt,j)=>j===i?{...pt,...p}:pt);history.commit({...model,points});setSaved(false)};

  const pointerDown=e=>{if(e.button===1||e.button===2||e.altKey){e.currentTarget.setPointerCapture(e.pointerId);setDrag({x:e.clientX,y:e.clientY,camera});}};
  const pointerMove=e=>{
    if(drag)setCamera({...drag.camera,yaw:drag.camera.yaw+(e.clientX-drag.x)*.18,pitch:Math.max(-25,Math.min(30,drag.camera.pitch+(e.clientY-drag.y)*.12))});
    if(pointDrag){const r=viewport.current.getBoundingClientRect();const x=(e.clientX-r.left)/r.width*100,y=(e.clientY-r.top)/r.height*100;setPointDrag({...pointDrag,x,y});}
  };
  const pointerUp=()=>{if(pointDrag){updatePoint(pointDrag.index,{x:Math.max(5,Math.min(95,pointDrag.x)),y:Math.max(15,Math.min(82,pointDrag.y))});setPointDrag(null)}setDrag(null)};
  const wheel=e=>{e.preventDefault();setCamera(c=>({...c,zoom:Math.max(.65,Math.min(1.8,c.zoom-e.deltaY*.001))}))};

  return <main className="cad-shell">
    <header className="appbar">
      <div className="appbar-left"><Logo/><span className="crumb">Automotive / <strong>GT Concept</strong></span></div>
      <div className="history-controls">
        <IconButton title="Undo · Ctrl Z" disabled={!history.canUndo} onClick={history.undo}><Undo2 size={15}/></IconButton>
        <IconButton title="Redo · Ctrl Shift Z" disabled={!history.canRedo} onClick={history.redo}><Redo2 size={15}/></IconButton>
        <span className="divider"/>
        {['Object','Edge','Face'].map((x,i)=><button key={x} className={`mode-chip ${selectionMode===x?'active':''}`} onClick={()=>setSelectionMode(x)}>{i+1} {x}</button>)}
      </div>
      <div className="appbar-actions"><span className={`save-state ${saved?'':'dirty'}`}><i/>{saved?'Saved':'Modified'}</span><button className="save-button" onClick={save}><Save size={14}/> Save</button><IconButton title="Workspace settings"><Settings2 size={16}/></IconButton></div>
    </header>

    <aside className="outliner-panel">
      <div className="panel-heading"><div><span className="eyebrow">MODEL</span><h2>Outliner <b>13</b></h2></div><IconButton title="Add object"><Plus size={16}/></IconButton></div>
      <label className="cad-search"><Search size={14}/><input placeholder="Filter model…"/><kbd>⌘F</kbd></label>
      <div className="tree-scroll">{tree.map(group=><div className="tree-group" key={group.id}>
        <button className="group-title" onClick={()=>toggleGroup(group.id)}>{group.open?<ChevronDown size={13}/>:<ChevronRight size={13}/>}<span>{group.name}</span><b>{group.children.length}</b></button>
        {group.open&&group.children.map(node=><div className={`cad-tree-row ${selected===node.id?'selected':''}`} key={node.id} onClick={()=>setSelected(node.id)}>
          <span className={`kind-icon kind-${node.kind.split(' ')[0].toLowerCase()}`}>{node.kind.includes('Curve')||node.kind.includes('NURBS')||node.kind.includes('Bezier')||node.kind.includes('Interpolated')?<Spline size={15}/>:node.kind.includes('Surface')||node.kind.includes('surface')||node.kind.includes('patch')||node.kind.includes('Loft')?<Grid3X3 size={15}/>:node.kind.includes('plane')?<Square size={15}/>:<Box size={15}/>}</span>
          <span className="node-label"><strong>{node.name}</strong><small>{node.kind}</small></span>
          {node.locked&&<Lock size={11} className="row-lock"/>}
          <button className="row-eye" aria-label={`Toggle ${node.name}`} onClick={e=>{e.stopPropagation();toggleNode(node.id)}}>{node.visible?<Eye size={13}/>:<EyeOff size={13}/>}</button>
        </div>)}
      </div>)}</div>
      <div className="outliner-footer"><div><span className="sync-light"/><span><strong>Parasolid session</strong><small>Millimeters · tolerance 0.001</small></span></div><ChevronRight size={13}/></div>
    </aside>

    <section className="workspace">
      <div className="tool-rail">{TOOLS.map(t=><button key={t.id} className={tool===t.id?'active':''} title={`${t.name} · ${t.key}`} onClick={()=>chooseTool(t.id)}><t.icon size={18}/><kbd>{t.key}</kbd></button>)}</div>
      <div className="viewport-toolbar">
        <div className="tool-context"><span className="tool-glyph">{tool==='curve'?<Spline size={16}/>:<MousePointer2 size={16}/>}</span><div><small>ACTIVE TOOL</small><strong>{TOOLS.find(x=>x.id===tool)?.name}</strong></div><ChevronDown size={13}/></div>
        <div className="view-actions"><button className={snap?'active':''} onClick={()=>setSnap(!snap)}><Magnet size={14}/> Snap</button><button className={grid?'active':''} onClick={()=>setGrid(!grid)}><Grid3X3 size={14}/> Grid</button><button className={xray?'active':''} onClick={()=>setXray(!xray)}><Aperture size={14}/> X-ray</button><span className="divider"/><button onClick={()=>setCamera({yaw:0,pitch:0,zoom:1})}>RIGHT</button></div>
      </div>
      <div ref={viewport} className={`model-viewport ${grid?'show-grid':''} ${xray?'xray':''}`} onContextMenu={e=>e.preventDefault()} onPointerDown={pointerDown} onPointerMove={pointerMove} onPointerUp={pointerUp} onPointerCancel={pointerUp} onWheel={wheel}>
        <CarViewport model={model} camera={camera} selected={selected} hidden={new Set(tree.flatMap(g=>g.children.filter(n=>!n.visible).map(n=>n.id)))} onSelect={setSelected} onPointDown={(e,index)=>{e.stopPropagation();e.currentTarget.setPointerCapture(e.pointerId);setPointDrag({index,x:model.points[index].x,y:model.points[index].y})}}/>
        <div className="view-cube"><button>TOP</button><div><span>F</span><span>R</span></div><i className="axis-z"/><i className="axis-x"/></div>
        <div className="viewport-hud"><span><MousePointer2 size={12}/> LMB select</span><span>MMB orbit</span><span>Wheel zoom</span><span>Alt + LMB orbit</span></div>
        <div className="selection-readout"><Crosshair size={13}/><span>{selectedNode.name}</span><small>{selectedNode.kind}</small></div>
      </div>
      <div className="statusbar"><span className="status-message"><i/>{toast}</span><span>YAW {camera.yaw.toFixed(1)}°</span><span>ZOOM {(camera.zoom*100).toFixed(0)}%</span><span>BODY <b>18 F</b></span><span>CURVES <b>26</b></span><span className="kernel"><Zap size={11}/> KERNEL READY</span></div>
    </section>

    <aside className="inspector-panel">
      <div className="inspector-title"><div><span className="object-badge"><Spline size={20}/></span><div><span className="eyebrow">{mode.toUpperCase()} / {model.continuity}</span><h1>{selectedNode.name}</h1></div></div><IconButton title="More options"><Menu size={17}/></IconButton></div>
      <div className="tabs">{['Curve','Geometry','Display'].map(t=><button className={mode===t?'active':''} key={t} onClick={()=>setMode(t)}>{t}</button>)}</div>
      <div className="inspector-scroll">
        {mode==='Curve'&&<CurveInspector model={model} update={updateModel} pointCount={model.points.length}/>} 
        {mode==='Geometry'&&<GeometryInspector model={model} update={updateModel}/>} 
        {mode==='Display'&&<DisplayInspector xray={xray} setXray={setXray}/>} 
      </div>
      <div className="inspector-actions"><button onClick={()=>notify('Duplicate created')}><Copy size={14}/> Duplicate</button><button className="danger" onClick={()=>notify('Protected demo object')}><Trash2 size={14}/></button></div>
    </aside>

    <button className="command-trigger" onClick={()=>setPalette(true)}><Command size={13}/><span>Commands</span><kbd>Space</kbd></button>
    {palette&&<CommandPalette query={paletteQuery} setQuery={setPaletteQuery} close={()=>setPalette(false)} choose={name=>{const lower=name.toLowerCase();const found=TOOLS.find(t=>lower.includes(t.id));if(found)chooseTool(found.id);notify(`${name} started`);setPalette(false)}}/>}
  </main>
}

function CarViewport({model,camera,selected,hidden,onSelect,onPointDown}){
 const transform=`translate(50 50) scale(${camera.zoom}) translate(-50 -50) skewY(${camera.pitch*.04}) translate(${camera.yaw*.025} 0)`;
 const curve=model.points.map(p=>`${p.x},${p.y}`).join(' ');
 const bodyPath='M 11 63 C 13 53, 19 48, 27 46 C 36 34, 47 29, 61 32 C 70 34, 76 43, 82 48 C 89 50, 93 54, 94 62 L 91 69 C 82 73, 23 73, 12 68 Z';
 const roofPath='M 28 47 C 36 35, 47 31, 59 33 C 68 35, 74 42, 79 48 Z';
 return <svg className="car-svg" viewBox="0 0 100 100" role="img" aria-label="Interactive automotive surface model">
   <defs><linearGradient id="bodyFill" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stopColor="#bfc5ca" stopOpacity=".9"/><stop offset=".55" stopColor="#5f676e" stopOpacity=".78"/><stop offset="1" stopColor="#272d31" stopOpacity=".94"/></linearGradient><radialGradient id="wheel"><stop offset="0" stopColor="#191b1c"/><stop offset=".45" stopColor="#454a4d"/><stop offset=".52" stopColor="#151718"/><stop offset=".9" stopColor="#292c2e"/><stop offset="1" stopColor="#0d0e0f"/></radialGradient><filter id="glow"><feGaussianBlur stdDeviation=".35"/></filter></defs>
   <g transform={transform}>
    {!hidden.has('blueprint')&&<g className="blueprint-lines"><path d="M10 72 H95 M10 30 H95 M18 20 V78 M50 20 V78 M84 20 V78"/><rect x="10" y="24" width="84" height="51" rx="2"/></g>}
    {!hidden.has('carBody')&&<g className={selected==='carBody'?'selected-shape':''} onClick={()=>onSelect('carBody')}>
      <path d={bodyPath} className="body-shell"/>
      <path d="M14 61 C30 58, 55 57, 92 60" className="surface-isoline"/>
      <path d="M18 52 C34 51, 64 50, 86 51" className="surface-isoline faint"/>
      <path d="M25 69 C30 52, 31 47, 39 36 M75 69 C72 53, 69 42, 60 33" className="surface-isoline faint"/>
      <path d="M46 70 L46 52 M64 70 L64 51" className="panel-seam"/>
      <path d="M13 64 C34 65, 72 65, 93 63" className="highlight-line"/>
    </g>}
    {!hidden.has('roofSurface')&&<path d={roofPath} className={`glass ${selected==='roofSurface'?'selected-edge':''}`} onClick={()=>onSelect('roofSurface')}/>} 
    {!hidden.has('hood')&&<path d="M12 60 C19 53, 24 50, 30 48 C24 53, 23 57, 22 62 Z" className={`patch ${selected==='hood'?'selected-edge':''}`} onClick={()=>onSelect('hood')}/>} 
    {!hidden.has('quarter')&&<path d="M73 49 C82 48, 90 51, 94 61 L91 67 C85 61,79 57,72 56 Z" className={`patch ${selected==='quarter'?'selected-edge':''}`} onClick={()=>onSelect('quarter')}/>} 
    {!hidden.has('wheelFL')&&<g onClick={()=>onSelect('wheelFL')} className={selected==='wheelFL'?'selected-shape':''}><circle cx="28" cy="68" r="10" fill="url(#wheel)"/><circle cx="28" cy="68" r="4.2" className="rim"/><path d="M28 64 V72 M24 68 H32 M25.2 65.2 L30.8 70.8 M30.8 65.2 L25.2 70.8" className="spokes"/></g>}
    {!hidden.has('wheelRL')&&<g onClick={()=>onSelect('wheelRL')} className={selected==='wheelRL'?'selected-shape':''}><circle cx="78" cy="68" r="10" fill="url(#wheel)"/><circle cx="78" cy="68" r="4.2" className="rim"/><path d="M78 64 V72 M74 68 H82 M75.2 65.2 L80.8 70.8 M80.8 65.2 L75.2 70.8" className="spokes"/></g>}
    {!hidden.has('roof')&&<g className={selected==='roof'?'curve-selected':''} onClick={()=>onSelect('roof')}><polyline points={curve} className="design-curve"/>{selected==='roof'&&<>{model.points.map((p,i)=><g key={i}><line x1={p.x} y1={p.y} x2={p.x} y2={Math.min(78,p.y+7)} className="cv-handle"/><circle cx={p.x} cy={p.y} r="1.2" className="cv" onPointerDown={e=>onPointDown(e,i)}/></g>)}</>}</g>}
    {!hidden.has('belt')&&<path d="M16 53 C32 51, 55 49, 88 51" className={`design-curve secondary ${selected==='belt'?'selected-edge':''}`} onClick={()=>onSelect('belt')}/>} 
    {!hidden.has('fender')&&<path d="M17 65 C19 55, 36 54, 39 66 M68 66 C70 54, 86 54, 90 65" className={`design-curve secondary ${selected==='fender'?'selected-edge':''}`} onClick={()=>onSelect('fender')}/>} 
    <g className="ground"><path d="M5 79 H96"/><path d="M12 82 H89"/></g>
   </g>
 </svg>
}

function Section({title,children,open=true}){const [shown,setShown]=useState(open);return <section className="property-section"><button className="section-head" onClick={()=>setShown(!shown)}>{shown?<ChevronDown size={13}/>:<ChevronRight size={13}/>}<span>{title}</span></button>{shown&&<div className="section-body">{children}</div>}</section>}
function NumberField({label,value,unit='',onChange,step=.1}){return <label className="field-row"><span>{label}</span><span className="number-field"><input type="number" value={value} step={step} onChange={e=>onChange(Number(e.target.value))}/><small>{unit}</small></span></label>}
function Segmented({value,values,onChange}){return <div className="segmented">{values.map(v=><button key={v} className={value===v?'active':''} onClick={()=>onChange(v)}>{v}</button>)}</div>}
function CurveInspector({model,update,pointCount}){return <>
 <Section title="CURVE DEFINITION"><div className="property-summary"><div className="summary-art"><Spline size={25}/><i/><i/></div><div><strong>NURBS curve</strong><span>Open · non-rational</span></div><button><ChevronRight size={14}/></button></div><NumberField label="Degree" value={model.degree} step={1} onChange={degree=>update({degree},'Curve degree changed')}/><NumberField label="Control points" value={pointCount} step={1} onChange={()=>{}}/><NumberField label="Tolerance" value={0.001} unit="mm" onChange={()=>{}} step={.001}/></Section>
 <Section title="CONTINUITY"><span className="field-label">End constraint</span><Segmented value={model.continuity} values={['G0','G1','G2','G3']} onChange={continuity=>update({continuity},`${continuity} continuity applied`)}/><div className="continuity-plot"><svg viewBox="0 0 240 80"><path d="M8 64 C55 64 66 17 117 32"/><path d="M117 32 C169 48 179 9 232 9"/><line x1="117" y1="13" x2="117" y2="57"/><circle cx="117" cy="32" r="3"/></svg><span><b>{model.continuity}</b> curvature match</span></div><label className="toggle-row"><span><strong>Symmetric handles</strong><small>Preserve tangent alignment</small></span><button className="switch on"><i/></button></label></Section>
 <Section title="FAIRING"><NumberField label="Smoothing passes" value={3} step={1} onChange={()=>{}}/><NumberField label="Max deviation" value={.025} unit="mm" onChange={()=>{}} step={.005}/><button className="full-action"><Sparkles size={14}/> Fair curve</button></Section>
 </>}
function GeometryInspector({model,update}){return <><Section title="BLEND"><span className="field-label">Edge treatment</span><Segmented value="Fillet" values={['Fillet','Chamfer','Blend']} onChange={()=>{}}/><NumberField label="Radius" value={model.radius} unit="mm" onChange={radius=>update({radius},'Blend radius updated')}/><label className="toggle-row"><span><strong>Variable radius</strong><small>Use edge control stations</small></span><button className="switch"><i/></button></label></Section><Section title="LOFT"><span className="field-label">Loft construction</span><select className="select-field"><option>Guide rails</option><option>Normal sections</option><option>Centerline</option><option>Developable</option><option>Closed periodic</option></select><NumberField label="Sections" value={7} step={1} onChange={()=>{}}/><NumberField label="Guide rails" value={2} step={1} onChange={()=>{}}/><label className="toggle-row"><span><strong>Refit sections</strong><small>Maintain tolerance</small></span><button className="switch on"><i/></button></label></Section></>}
function DisplayInspector({xray,setXray}){return <><Section title="APPEARANCE"><div className="swatch-row"><span>Surface material</span><button><i/> Clay graphite <ChevronRight size={13}/></button></div><NumberField label="Opacity" value={100} unit="%" onChange={()=>{}}/><label className="toggle-row"><span><strong>X-ray surfaces</strong><small>See obscured topology</small></span><button className={`switch ${xray?'on':''}`} onClick={()=>setXray(!xray)}><i/></button></label><label className="toggle-row"><span><strong>Zebra analysis</strong><small>Inspect surface continuity</small></span><button className="switch"><i/></button></label></Section><Section title="CURVATURE"><select className="select-field"><option>Gaussian curvature</option><option>Mean curvature</option><option>Draft angle</option><option>Porcupine comb</option></select><div className="color-ramp"/><div className="ramp-labels"><span>-0.25</span><span>0</span><span>+0.25</span></div></Section></>}

function CommandPalette({query,setQuery,close,choose}){const groups=commandGroups.map(g=>({...g,items:g.items.filter(([name])=>name.toLowerCase().includes(query.toLowerCase()))})).filter(g=>g.items.length);return <div className="palette-backdrop" onMouseDown={e=>{if(e.target===e.currentTarget)close()}}><div className="command-palette"><div className="palette-input"><Search size={18}/><input autoFocus value={query} onChange={e=>setQuery(e.target.value)} placeholder="Search modelling commands…"/><kbd>ESC</kbd></div><div className="palette-results">{groups.map(group=><section key={group.name}><h3>{group.name}</h3>{group.items.map(([name,key])=><button key={name} onClick={()=>choose(name)}><span>{name}</span><kbd>{key}</kbd></button>)}</section>)}</div><footer><span><b>↑↓</b> Navigate</span><span><b>↵</b> Run command</span><span>Plasticity keymap</span></footer></div></div>}

createRoot(document.getElementById('root')).render(<App/>);
