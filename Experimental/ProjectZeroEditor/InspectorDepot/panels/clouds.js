/* Cloud layer instrument: a weather map, optical coverage analysis, vertical deck and advection. */
import { el, colorChip } from '../kit.js';
import { tape, pillToggle } from './controls.js';
import { ic } from '../icons.js';
import { flat } from '../world.js';

const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const rgb=c=>{const n=parseInt((c||'#eef3f8').slice(1),16);return[n>>16&255,n>>8&255,n&255]};
const css=(c,a=1)=>`rgba(${c.map(Math.round).join(',')},${a})`;
const hash=(x,y)=>{const n=Math.sin(x*127.1+y*311.7)*43758.5453;return n-Math.floor(n)};
const valueNoise=(x,y)=>{const ix=Math.floor(x),iy=Math.floor(y),fx=x-ix,fy=y-iy,u=fx*fx*(3-2*fx),v=fy*fy*(3-2*fy),a=hash(ix,iy),b=hash(ix+1,iy),c=hash(ix,iy+1),d=hash(ix+1,iy+1);return a+(b-a)*u+(c-a)*v+(a-b-c+d)*u*v};
const noise=(x,y,s,d)=>{
  const f=Math.max(.2,s);let n=valueNoise(x*.028*f,y*.028*f)*.55+valueNoise(x*.067*f+9,y*.067*f-4)*.3;
  n+=valueNoise(x*.16*f-3,y*.16*f+7)*(.07+d*.08);return clamp(n/(.92+d*.08),0,1);
};
const cloudName=c=>c<.12?'Few':c<.35?'Scattered':c<.65?'Broken':'Overcast';

export function cloudsPanel(node,ctx){
  const{compact=false,setProp,register}=ctx,P=node.props,host=el('div','mpanel cloudpanel');
  const C=()=>P.coverage??.46,D=()=>P.density??.62,A=()=>P.altitude??130,F=()=>P.scale??1,Q=()=>P.detail??.55,V=()=>P.speed??1;
  const canvas=(cv,h)=>{const w=cv.clientWidth||290,d=Math.min(devicePixelRatio||1,2);if(cv.width!==w*d||cv.height!==h*d){cv.width=w*d;cv.height=h*d}cv.style.height=h+'px';const g=cv.getContext('2d');g.setTransform(d,0,0,d,0,0);return[g,w,h]};
  const threshold=()=>1-C()*.78;

  /* satellite hero */
  const hero=el('div','pcard mp-hero cl-hero'),cv=el('canvas');cv.title='Drag across for cloud cover; vertically for optical density';const cap=el('div','mp-cap',`<div class="l"><b>—</b><span class="mp-illum cl-sub">—</span></div><div class="r">—</div>`);hero.append(cv,cap);host.append(hero);let mark=null;
  function paintMap(){const[g,w,h]=canvas(cv,compact?142:170),tint=rgb(P.tint),shade=rgb(P.shade);g.clearRect(0,0,w,h);const sea=g.createLinearGradient(0,0,w,h);sea.addColorStop(0,'#111820');sea.addColorStop(1,'#050708');g.fillStyle=sea;g.fillRect(0,0,w,h);
    const step=3,th=threshold();for(let y=0;y<h;y+=step)for(let x=0;x<w;x+=step){const n=noise(x,y,F(),Q());if(n>th){const body=clamp((n-th)/(1-th),0,1),light=shade.map((v,i)=>v+(tint[i]-v)*(body*.55+.2));g.fillStyle=css(light,(.16+body*.72)*D());g.fillRect(x,y,step+.3,step+.3)}}
    g.strokeStyle='rgba(255,255,255,.10)';for(let x=0;x<w;x+=w/8){g.beginPath();g.moveTo(x,0);g.lineTo(x,h);g.stroke()}for(let y=0;y<h;y+=h/5){g.beginPath();g.moveTo(0,y);g.lineTo(w,y);g.stroke()}
    g.font='8px ui-sans-serif,system-ui';g.fillStyle='rgba(255,255,255,.34)';g.textAlign='left';g.fillText('N',8,13);g.textAlign='right';g.fillText(`${Math.round(12/F())} km SWATH`,w-8,13);
    const wind=flat.find(n=>n.type==='wind')?.props||{direction:214,speed:4.2},ang=(wind.direction+90)*Math.PI/180,x=w-30,y=h-22;g.strokeStyle='rgba(255,255,255,.7)';g.beginPath();g.moveTo(x,y);g.lineTo(x+Math.cos(ang)*18,y+Math.sin(ang)*18);g.stroke();g.fillStyle='#fff';g.beginPath();g.arc(x,y,2,0,Math.PI*2);g.fill();
    if(mark){g.strokeStyle='rgba(255,255,255,.45)';g.setLineDash([2,3]);g.beginPath();g.moveTo(mark.x,0);g.lineTo(mark.x,h);g.moveTo(0,mark.y);g.lineTo(w,mark.y);g.stroke();g.setLineDash([])}
    cap.querySelector('b').textContent=cloudName(C());cap.querySelector('.cl-sub').textContent=`${Math.round(C()*8)} / 8 oktas · optical depth ${(D()*12).toFixed(1)}`;cap.querySelector('.r').textContent=`base ${A().toFixed(0)} m`;
  }
  const mapFrom=e=>{const r=cv.getBoundingClientRect(),x=clamp((e.clientX-r.left)/r.width,0,1),y=clamp((e.clientY-r.top)/r.height,0,1);mark={x:x*r.width,y:y*r.height};setProp(node,'coverage',+x.toFixed(2));setProp(node,'density',+(1-y).toFixed(2));paintAll()};cv.onpointerdown=e=>{cv.setPointerCapture(e.pointerId);cv.classList.add('grabbing');mapFrom(e)};cv.onpointermove=e=>{if(cv.hasPointerCapture?.(e.pointerId))mapFrom(e)};cv.onpointerup=()=>{mark=null;cv.classList.remove('grabbing');paintMap()};

  const rail=el('div','mp-rail');const metric=k=>{const e=el('div','mp-pill',`<b class="v">—</b><span class="k">${k}</span>`);rail.append(e);return e.querySelector('.v')};const rCov=metric('Coverage'),rDen=metric('Optical'),rAlt=metric('Base'),rDrift=metric('Drift');host.append(rail);
  const duo=el('div','mp-duo');const stat=(icon,label)=>{const e=el('div','pcard mp-stat',`<span class="i">${ic(icon,{size:12})}</span><span class="l">${label}</span><b class="n">—</b>`);duo.append(e);return e};const sunlight=stat('sun','Sun reaching datum'),oktas=stat('cloud','Sky cover');host.append(duo);

  /* coverage distribution */
  const cc=el('div','pcard mp-metric cl-cover');cc.innerHTML=`<div class="mp-chead"><div class="l"><span class="t">Coverage</span><span class="s">Condensate threshold · cell population</span></div></div><div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">%</span></div><div class="mp-k mp-target">Sky fraction <span class="v">—</span></div>`;const wrap=el('div','mp-chartwrap'),hist=el('canvas');wrap.append(hist);cc.append(wrap);host.append(cc);
  function paintHistogram(){const[g,w,h]=canvas(hist,110);g.clearRect(0,0,w,h);const bins=new Array(20).fill(0);for(let i=0;i<1600;i++)bins[Math.min(19,Math.floor(noise((i%40)*7,Math.floor(i/40)*5,F(),Q())*20))]++;const max=Math.max(...bins),L=8,R=8,T=8,B=17;bins.forEach((n,i)=>{const x=L+i/20*(w-L-R),bh=n/max*(h-T-B);g.fillStyle=i/20>threshold()?'rgba(238,243,248,.72)':'rgba(255,255,255,.11)';g.fillRect(x,h-B-bh,(w-L-R)/20-2,bh)});const x=L+threshold()*(w-L-R);g.strokeStyle='#fff';g.setLineDash([3,3]);g.beginPath();g.moveTo(x,T);g.lineTo(x,h-B);g.stroke();g.setLineDash([]);g.font='8px ui-sans-serif,system-ui';g.fillStyle='rgba(255,255,255,.3)';g.textAlign='left';g.fillText('CLEAR AIR',L,h-3);g.textAlign='right';g.fillText('CONDENSED',w-R,h-3)}
  const coverFrom=e=>{const r=hist.getBoundingClientRect();setProp(node,'coverage',+clamp((e.clientX-r.left)/r.width,0,1).toFixed(2));paintAll()};hist.onpointerdown=e=>{hist.setPointerCapture(e.pointerId);hist.classList.add('drag');coverFrom(e)};hist.onpointermove=e=>{if(hist.hasPointerCapture?.(e.pointerId))coverFrom(e)};hist.onpointerup=e=>hist.releasePointerCapture?.(e.pointerId);

  /* vertical deck */
  const lc=el('div','pcard mp-light cl-layer');lc.innerHTML=`<div class="mp-chead"><div class="l"><span class="t">Cloud deck</span><span class="s">Base altitude · optical body</span></div></div>`;const lb=el('div','pbody'),section=el('div','mp-meter cl-section','<div class="hd"><span class="k">vertical section · 0–400 m</span></div>'),sec=el('canvas');section.append(sec);lb.append(section);lc.append(lb);
  const alt=tape({label:'Cloud base',min:20,max:400,value:A(),dec:0,step:2,unit:'m',marks:[{t:0,l:'20 m'},{t:.289,l:'LOW 130'},{t:1,l:'400 m'}],onInput:v=>{setProp(node,'altitude',Math.round(v));paintAll()}}),den=tape({label:'Optical density',min:0,max:1,value:D(),dec:2,step:.01,marks:[{t:0,l:'VEIL'},{t:.62,l:'BODY .62'},{t:1,l:'OPAQUE'}],onInput:v=>{setProp(node,'density',v);paintAll()}});lb.append(alt,den);host.append(lc);lc.querySelector('.mp-chead .l').onclick=()=>lc.classList.toggle('shut');
  function paintSection(){const[g,w,h]=canvas(sec,104);g.clearRect(0,0,w,h);g.fillStyle='#060708';g.fillRect(0,0,w,h);const py=m=>h-12-m/400*(h-20),base=py(A()),thick=10+34*D();for(let x=0;x<w;x+=4){const top=base-thick*(.65+.3*Math.sin(x*.08*F())+.12*Math.sin(x*.31));const gr=g.createLinearGradient(0,top,0,base+4);gr.addColorStop(0,css(rgb(P.tint),.05));gr.addColorStop(.35,css(rgb(P.tint),.75*D()));gr.addColorStop(1,css(rgb(P.shade),.72*D()));g.fillStyle=gr;g.fillRect(x,top,4.3,base-top+4)}[0,100,200,300,400].forEach(m=>{const y=py(m);g.strokeStyle='rgba(255,255,255,.07)';g.beginPath();g.moveTo(0,y);g.lineTo(w,y);g.stroke();g.fillStyle='rgba(255,255,255,.27)';g.font='8px ui-sans-serif';g.textAlign='left';g.fillText(m===0?'DATUM':m+' m',5,y-2)});g.strokeStyle='rgba(255,255,255,.65)';g.setLineDash([3,3]);g.beginPath();g.moveTo(0,base);g.lineTo(w,base);g.stroke();g.setLineDash([])}
  const secFrom=e=>{const r=sec.getBoundingClientRect();setProp(node,'altitude',Math.round(clamp((1-(e.clientY-r.top)/r.height)*400,20,400)));paintAll()};sec.onpointerdown=e=>{sec.setPointerCapture(e.pointerId);sec.classList.add('drag');secFrom(e)};sec.onpointermove=e=>{if(sec.hasPointerCapture?.(e.pointerId))secFrom(e)};sec.onpointerup=e=>sec.releasePointerCapture?.(e.pointerId);

  /* morphology + advection */
  const mc=el('div','pcard mp-light cl-form');mc.innerHTML=`<div class="mp-chead"><div class="l"><span class="t">Morphology</span><span class="s">Cell scale · edge detail · advection</span></div></div>`;const mb=el('div','pbody');mc.append(mb);
  const scale=tape({label:'Feature size',min:.2,max:4,value:F(),dec:2,step:.02,unit:'×',marks:[{t:0,l:'SHEETS'},{t:.21,l:'1×'},{t:1,l:'CELLS'}],onInput:v=>{setProp(node,'scale',v);paintAll()}}),detail=tape({label:'Edge detail',min:0,max:1,value:Q(),dec:2,step:.01,marks:[{t:0,l:'SOFT'},{t:.55,l:'NATURAL'},{t:1,l:'FRACTAL'}],onInput:v=>{setProp(node,'detail',v);paintAll()}}),speed=tape({label:'Drift speed',min:0,max:6,value:V(),dec:2,step:.05,unit:'×',marks:[{t:0,l:'STILL'},{t:.167,l:'REAL 1×'},{t:1,l:'6×'}],onInput:v=>{setProp(node,'speed',v);paintAll()}});mb.append(scale,detail,speed);
  const colours=el('div','cl-colours'),th=el('div','mp-subhead','<span class="k">sunlit</span>'),tc=colorChip(P.tint,v=>{setProp(node,'tint',v);paintAll()}),sh=el('div','mp-subhead','<span class="k">shadowed</span>'),sc=colorChip(P.shade,v=>{setProp(node,'shade',v);paintAll()});th.append(tc);sh.append(sc);colours.append(th,sh);mb.append(colours);const tags=el('div','mp-tags'),wind=pillToggle('FOLLOW WIND',P.windLinked!==false,v=>{setProp(node,'windLinked',v);paintAll()});tags.append(wind);mb.append(tags);const note=el('div','mp-note');mb.append(note);host.append(mc);mc.querySelector('.mp-chead .l').onclick=()=>mc.classList.toggle('shut');

  function paintAll(){paintMap();paintHistogram();paintSection();const pass=Math.exp(-D()*C()*2.2),pct=C()*100;rCov.innerHTML=`${pct.toFixed(0)}<em>%</em>`;rDen.textContent=(D()*12).toFixed(1);rAlt.innerHTML=`${A().toFixed(0)}<em>m</em>`;rDrift.innerHTML=`${V().toFixed(1)}<em>×</em>`;sunlight.querySelector('.n').innerHTML=`${(pass*100).toFixed(0)}<em>%</em>`;oktas.querySelector('.n').innerHTML=`${Math.round(C()*8)}<em>/8</em>`;cc.querySelector('.mp-num .i').textContent=Math.floor(pct);cc.querySelector('.mp-num .d').textContent='.'+Math.round(pct*10)%10;cc.querySelector('.mp-target .v').textContent=cloudName(C()).toLowerCase()+' cloud';alt._set(A());den._set(D());scale._set(F());detail._set(Q());speed._set(V());tc._set?.(P.tint);sc._set?.(P.shade);wind._set(P.windLinked!==false);note.textContent=`${P.windLinked!==false?'Advection follows the Wind Field.':'Layer has independent drift.'} Feature scale is ${F().toFixed(2)}× with ${Math.round(Q()*100)}% edge detail.`}
  register?.(paintAll);const ro=new ResizeObserver(()=>{paintMap();paintHistogram();paintSection();[alt,den,scale,detail,speed].forEach(t=>t._paint?.())});ro.observe(host);host._dispose=()=>ro.disconnect();requestAnimationFrame(paintAll);paintAll();return host;
}
