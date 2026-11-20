import {buildMesh,watertight,camera,point,unit,sub,dot,cross,mul} from './cluster-lod-core.js';
const $=id=>document.getElementById(id),canvas=$('scene'),ctx=canvas.getContext('2d');
const colors={1:[155,138,216],2:[115,155,200],4:[95,202,177],8:[239,212,137]};
let mode='clusters',glass=false,fixed=false,yaw=.35,pitch=.13,mesh,lastKey='',drag=null,queued=false;
const css=(c,a=1)=>`rgba(${c.map(x=>Math.round(x)).join(',')},${a})`;
function request(){if(!queued){queued=true;requestAnimationFrame(draw);}}
function draw(){
 queued=false;const w=canvas.clientWidth,h=canvas.clientHeight,dpr=Math.min(devicePixelRatio||1,2);
 if(canvas.width!==Math.round(w*dpr)||canvas.height!==Math.round(h*dpr)){canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);}
 ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);
 const distance=+$('distance').value,error=+$('error').value,amplitude=+$('height').value,observer=$('observer').checked,safe=$('safe').checked;
 $('distanceValue').value=distance.toFixed(1)+' units';$('errorValue').value=error.toFixed(1)+' px';$('heightValue').value=amplitude.toFixed(2)+' units';
 const lodEye=camera(observer?0:yaw,observer?0:pitch,distance),eye=observer?camera(yaw+1.15,pitch+.12,5.5):lodEye;
 const opt={eye:lodEye,height:h,error,amplitude,normalAware:$('normals').checked,glass,glassSafe:safe,fixed};
 const key=JSON.stringify(opt);if(key!==lastKey){mesh=buildMesh(opt);lastKey=key;}
 const fwd=unit(mul(eye,-1)),right=unit(cross(fwd,[0,1,0])),up=cross(right,fwd),f=h/(2*Math.tan(Math.PI/7));
 function project(p){const r=sub(p,eye),z=dot(r,fwd);return z<.1?null:[w*.46+dot(r,right)*f/z,h*.54-dot(r,up)*f/z,z];}
 // A visual floor for perspective; it has no role in LOD selection.
 ctx.strokeStyle='#647d8b24';ctx.lineWidth=1;
 for(let i=-6;i<=6;i++){for(const pair of [[[i*.6,-1.22,-3.6],[i*.6,-1.22,3.6]],[[-3.6,-1.22,i*.6],[3.6,-1.22,i*.6]]]){let a=project(pair[0]),b=project(pair[1]);if(a&&b){ctx.beginPath();ctx.moveTo(a[0],a[1]);ctx.lineTo(b[0],b[1]);ctx.stroke();}}}
 const tris=[];let drawn=0;
 for(const t of mesh.tris){
  const face=dot(t.normal,sub(eye,t.center));if(!glass&&face<=0)continue;
  const p=t.p.map(project);if(p.some(q=>!q))continue;
  tris.push({t,p,z:(p[0][2]+p[1][2]+p[2][2])/3,front:face>0});
 }
 tris.sort((a,b)=>b.z-a.z);
 const light=unit([-1,2,3]);
 for(const {t,p,front} of tris){
  let n=t.normal;
  if($('normalmap').checked){const [u,v]=t.uv; n=unit([n[0]+.14*Math.sin(u*210)*Math.cos(v*150),n[1]+.12*Math.sin(v*150),n[2]]);}
  const shade=.28+.72*Math.max(0,dot(n,light)),base=mode==='clusters'?colors[t.n]:[144,177,185];
  const checker=(Math.floor(t.uv[0]*36)+Math.floor(t.uv[1]*18))%2;
  const tint=mode==='shaded'&&$('uv').checked?(checker?.85:1.06):1;
  let color=base.map(v=>v*shade*tint);
  if(glass)color=front?[114,214,209]:[68,121,155];
  ctx.beginPath();ctx.moveTo(p[0][0],p[0][1]);ctx.lineTo(p[1][0],p[1][1]);ctx.lineTo(p[2][0],p[2][1]);ctx.closePath();
  if(mode!=='wire'){ctx.fillStyle=css(color,glass?(front?.15:.07):1);ctx.fill();}
  if(mode!=='shaded'){ctx.strokeStyle=mode==='wire'?css(base,glass?.35:.65):'rgba(9,20,28,.32)';ctx.lineWidth=.55;ctx.stroke();}
  drawn++;
 }
 // Deliberately fixed UV island boundaries, independent of LOD. Draw only front arcs for opaque.
 if($('uv').checked){ctx.lineWidth=1.5;ctx.strokeStyle='rgba(234,242,251,.65)';
  for(const u of [0,.25,.5,.75])for(let i=0;i<80;i++){
   const a=point(u,i/80,amplitude),b=point(u,(i+1)/80,amplitude),mid=point(u,(i+.5)/80,amplitude);
   if(!glass&&dot(unit(mid),sub(eye,mid))<.02)continue;
   const p=project(a),q=project(b);if(p&&q){ctx.beginPath();ctx.moveTo(...p.slice(0,2));ctx.lineTo(...q.slice(0,2));ctx.stroke();}
  }
 }
 if(observer){
  const pos=project(lodEye),o=project([0,0,0]);if(pos&&o){
   const x=Math.min(w-220,Math.max(70,pos[0])),y=Math.min(h-150,Math.max(200,pos[1]));
   ctx.setLineDash([5,5]);ctx.strokeStyle='#82e3c370';ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(o[0],o[1]);ctx.stroke();ctx.setLineDash([]);
   ctx.fillStyle='#82e3c3';ctx.beginPath();ctx.arc(x,y,5,0,Math.PI*2);ctx.fill();ctx.font='11px system-ui';ctx.fillText(`LOD camera · ${distance.toFixed(1)} units`,x+12,y+4);
  }
 }
 $('triangles').textContent=mesh.tris.length.toLocaleString();$('reduction').textContent=Math.max(0,(1-mesh.tris.length/mesh.reference)*100).toFixed(0)+'%';$('visible').textContent=drawn.toLocaleString();
 $('build').textContent='Mesh rebuild '+mesh.buildMs.toFixed(1)+' ms CPU · not GPU timing';
 $('policy').textContent=glass&&safe?'GLASS SAFE · full closed shell locked':fixed?'REFERENCE · fixed maximum detail':observer?'OBSERVER · detail selected by marked camera':'72 patches · per-patch LOD · stitched edges';
 $('explain').textContent=glass?(safe?'Glass-safe mode keeps both front and back surfaces at full detail. This protects topology, not a certified optical error bound. Canvas transparency is not ray-traced glass.':'Unsafe comparison: camera-only LOD can simplify glass entry and exit surfaces differently. Refraction, thickness and reflected views need a stricter policy. No physical refraction is simulated here.'):'Opaque backfaces are culled from this view, not deleted from the mesh. Reflection and shadow rays still need off-camera geometry.';
 window.clusterLodDemo={triangles:mesh.tris.length,drawn,levels:mesh.levels,glassSafe:glass&&safe,watertight:()=>watertight(mesh),mesh};
}
for(const id of ['distance','error','height','observer','normals','normalmap','uv','safe'])$(id).addEventListener('input',request);
for(const b of document.querySelectorAll('[data-mode]'))b.onclick=()=>{mode=b.dataset.mode;for(const x of document.querySelectorAll('[data-mode]'))x.classList.toggle('active',x===b);request();};
function choose(a,b,on){$(a).classList.toggle('active',on);$(b).classList.toggle('active',!on);}
$('adaptive').onclick=()=>{fixed=false;choose('adaptive','fixed',true);request();};$('fixed').onclick=()=>{fixed=true;choose('adaptive','fixed',false);request();};
$('opaque').onclick=()=>{glass=false;choose('opaque','glass',true);request();};$('glass').onclick=()=>{glass=true;choose('opaque','glass',false);request();};
$('reset').onclick=()=>{yaw=.35;pitch=.13;fixed=false;glass=false;mode='clusters';$('distance').value=4.5;$('error').value=1.8;$('height').value=.03;for(const id of ['observer','normalmap'])$(id).checked=false;for(const id of ['normals','uv','safe'])$(id).checked=true;choose('opaque','glass',true);choose('adaptive','fixed',true);for(const b of document.querySelectorAll('[data-mode]'))b.classList.toggle('active',b.dataset.mode===mode);request();};
canvas.onpointerdown=e=>{drag=[e.clientX,e.clientY];canvas.setPointerCapture(e.pointerId);};canvas.onpointermove=e=>{if(!drag)return;yaw-=(e.clientX-drag[0])*.007;pitch=Math.max(-1.1,Math.min(1.1,pitch+(e.clientY-drag[1])*.006));drag=[e.clientX,e.clientY];request();};canvas.onpointerup=canvas.onpointercancel=()=>{drag=null;};
canvas.addEventListener('wheel',e=>{e.preventDefault();$('distance').value=Math.max(2.2,Math.min(16,+$('distance').value+e.deltaY*.007));request();},{passive:false});
new ResizeObserver(request).observe(canvas);request();
