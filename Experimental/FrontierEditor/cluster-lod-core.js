// Original educational parametric cluster LOD. Not Nanite/Vulcanite code or a QEM/DAG implementation.
export const U=12,V=6;
export const add=(a,b)=>a.map((x,i)=>x+b[i]);
export const sub=(a,b)=>a.map((x,i)=>x-b[i]);
export const mul=(a,s)=>a.map(x=>x*s);
export const dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);
export const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
export const length=a=>Math.hypot(...a);
export const unit=a=>mul(a,1/(length(a)||1));
export function point(u,v,amplitude=0){
 u=((u%1)+1)%1; const du=Math.min(Math.abs(u-.2),1-Math.abs(u-.2));
 const p=u*Math.PI*2,t=v*Math.PI,s=Math.sin(t);
 const r=1+amplitude*s*s*(.5*Math.sin(p*8)*Math.sin(t*6)+.5*Math.exp(-((v-.38)**2+du**2)*100));
 return [r*s*Math.cos(p),r*Math.cos(t),r*s*Math.sin(p)];
}
export function normal(u,v,a){
 const e=.0001;return unit(cross(sub(point(u+e,v,a),point(u-e,v,a)),sub(point(u,v+e,a),point(u,v-e,a))));
}
export function camera(yaw,pitch,distance){return [Math.sin(yaw)*Math.cos(pitch)*distance,Math.sin(pitch)*distance,Math.cos(yaw)*Math.cos(pitch)*distance];}
export function buildMesh({eye=[0,0,4],height=650,error=1.5,amplitude=.06,normalAware=true,glass=false,glassSafe=true,fixed=false}={}){
 const start=performance.now(),focal=height/(2*Math.tan(Math.PI/7));
 const clusters=[];
 for(let y=0;y<V;y++)for(let x=0;x<U;x++){
  const u=(x+.5)/U,v=(y+.5)/V,p=point(u,v,amplitude),distance=Math.max(.1,length(sub(eye,p))-.28);
  // Parametric curvature/displacement heuristic, NOT a certified QEM bound.
  const base=.065*(.35+.65*Math.sin(v*Math.PI)) + amplitude*.8;
  const n0=normal(u-.025,v,amplitude),n1=normal(u+.025,v,amplitude);
  const attribute=normalAware?1+Math.min(2,Math.acos(Math.max(-1,Math.min(1,dot(n0,n1))))*2):1;
  let n=1;
  while(n<8 && base*attribute*focal/(distance*n*n)>error)n*=2;
  if(fixed||(glass&&glassSafe))n=8;
  clusters.push({x,y,n,u,v,p,error:base*attribute*focal/(distance*n*n)});
 }
 const tris=[];
 const at=(x,y)=>y<0||y>=V?null:clusters[y*U+(x+U)%U];
 for(const c of clusters){
  const {x,y,n}=c;
  for(let j=0;j<n;j++)for(let i=0;i<n;i++){
   const u0=(x+i/n)/U,u1=(x+(i+1)/n)/U,v0=(y+j/n)/V,v1=(y+(j+1)/n)/V;
   const polygon=[];
   function edge(a,b,neighbor,onBoundary){
    const pieces=onBoundary?Math.max(1,(neighbor?.n||n)/n):1;
    for(let k=0;k<pieces;k++)polygon.push([a[0]+(b[0]-a[0])*k/pieces,a[1]+(b[1]-a[1])*k/pieces]);
   }
   // Extra edge vertices match the finer neighboring patch: no skirts or open cracks.
   edge([u0,v0],[u1,v0],at(x,y-1),j===0);
   edge([u1,v0],[u1,v1],at(x+1,y),i===n-1);
   edge([u1,v1],[u0,v1],at(x,y+1),j===n-1);
   edge([u0,v1],[u0,v0],at(x-1,y),i===0);
   const uv=[(u0+u1)/2,(v0+v1)/2],center=point(...uv,amplitude);
   for(let k=0;k<polygon.length;k++){
    const aa=polygon[k],bb=polygon[(k+1)%polygon.length];
    const a=point(...aa,amplitude),b=point(...bb,amplitude);
    const norm=cross(sub(a,center),sub(b,center));if(length(norm)<1e-10)continue;
    tris.push({p:[center,a,b],normal:unit(norm),center:mul(add(add(center,a),b),1/3),cluster:y*U+x,n,uv});
   }
  }
 }
 return {tris,clusters,buildMs:performance.now()-start,reference:72*4*64-192,levels:[1,2,4,8].map(n=>clusters.filter(c=>c.n===n).length)};
}
export function watertight(mesh){
 const edges=new Map(),key=p=>p.map(v=>Math.round(v*1e8)).join(',');
 for(const t of mesh.tris)for(let i=0;i<3;i++){
  const a=key(t.p[i]),b=key(t.p[(i+1)%3]);const k=a<b?a+'|'+b:b+'|'+a;
  edges.set(k,(edges.get(k)||0)+1);
 }
 return [...edges.values()].every(n=>n===2);
}
