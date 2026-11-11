/* ════════════════════════════════════════════════════════════════════════════════════════════
   FOLDER / COLLECTION INSTRUMENT
   A folder is not an empty object with a tint field. It is a manifest: hierarchy, composition,
   health and visibility. This panel draws the collection as an operational overview.
   ════════════════════════════════════════════════════════════════════════════════════════════ */
import { el, colorChip } from '../kit.js';
import { pillToggle } from './controls.js';
import { ic, folderIcon } from '../icons.js';
import { TYPES, isFolder, typeOf } from '../world.js';
import { bus } from '../bus.js';

const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;' }[c]));

export function folderPanel(node, ctx) {
  const { compact = false, setProp, register, onDirty } = ctx;
  const host = el('div', 'mpanel folderpanel');
  const all = () => { const out=[]; const walk=l=>l.forEach(n=>{out.push(n);walk(n.kids||[])}); walk(node.kids); return out; };
  const leaves = () => all().filter(n => !isFolder(n));
  const tint = () => node.props.tint || '#c9a24b';

  /* ── hierarchy hero ────────────────────────────────────────────────────────────────────── */
  const hero = el('div', 'pcard mp-hero fd-hero');
  const cv = el('canvas');
  const cap = el('div', 'mp-cap', `<div class="l"><b>${esc(node.name)} collection</b><span class="mp-illum fd-sub">—</span></div><div class="r">—</div>`);
  hero.append(cv, cap); host.append(hero);

  function canvas(cv, h) {
    const w=cv.clientWidth||290,d=Math.min(devicePixelRatio||1,2); if(cv.width!==w*d||cv.height!==h*d){cv.width=w*d;cv.height=h*d} cv.style.height=h+'px'; const g=cv.getContext('2d');g.setTransform(d,0,0,d,0,0);return[g,w,h];
  }
  function paintHero() {
    const [g,w,h]=canvas(cv,compact?132:154), list=all(); g.clearRect(0,0,w,h); g.fillStyle='#070707';g.fillRect(0,0,w,h);
    const x0=18, rootY=h/2; g.strokeStyle='rgba(255,255,255,.12)';g.lineWidth=1;
    g.beginPath();g.moveTo(x0+9,rootY);g.lineTo(w-15,rootY);g.stroke();
    g.fillStyle=tint();g.beginPath();g.arc(x0,rootY,6,0,Math.PI*2);g.fill();
    const direct=node.kids.length||1, step=(h-28)/direct;
    node.kids.forEach((n,i)=>{
      const y=14+step*(i+.5), branchX=70;
      g.strokeStyle='rgba(255,255,255,.15)';g.beginPath();g.moveTo(x0+6,rootY);g.bezierCurveTo(42,rootY,42,y,branchX,y);g.stroke();
      const col=n.props?.tint||typeOf(n).color;g.fillStyle=col;g.beginPath();g.arc(branchX,y,isFolder(n)?4.5:3,0,Math.PI*2);g.fill();
      g.fillStyle=n.vis===false?'rgba(255,255,255,.22)':'rgba(255,255,255,.72)';g.font='9px ui-sans-serif,system-ui';g.textAlign='left';g.fillText(n.name,branchX+9,y+3);
      if(isFolder(n)&&n.kids.length){
        const bx=Math.min(w-20,branchX+88);g.strokeStyle='rgba(255,255,255,.10)';g.beginPath();g.moveTo(branchX+5,y);g.lineTo(bx,y);g.stroke();
        n.kids.slice(0,7).forEach((k,j)=>{const xx=bx+j*12;g.fillStyle=k.vis===false?'rgba(255,255,255,.16)':typeOf(k).color;g.fillRect(xx-2,y-2,4,4)});
      }
    });
    g.fillStyle='rgba(255,255,255,.25)';g.font='8px ui-sans-serif,system-ui';g.textAlign='right';g.fillText('ROOT  /  DIRECT CHILDREN  /  DESCENDANTS',w-10,h-7);
    cap.querySelector('.fd-sub').textContent=`${node.kids.length} direct · ${list.length} total · depth ${Math.max(0,...list.map(n=>(n.depth??node.depth)-(node.depth??0)))}`;
    cap.querySelector('.r').textContent=`${leaves().filter(n=>n.vis!==false).length} / ${leaves().length} visible`;
  }

  /* ── headline metrics ──────────────────────────────────────────────────────────────────── */
  const rail=el('div','mp-rail');
  const metric=k=>{const e=el('div','mp-pill',`<b class="v">—</b><span class="k">${k}</span>`);rail.append(e);return e.querySelector('.v')};
  const mDirect=metric('Direct'),mTotal=metric('Total'),mLive=metric('Visible'),mDyn=metric('Dynamic'); host.append(rail);

  /* ── composition ───────────────────────────────────────────────────────────────────────── */
  const comp=el('div','pcard mp-metric fd-comp');
  comp.innerHTML=`<div class="mp-chead"><div class="l"><span class="t">Composition</span><span class="s">Entity classes in this collection</span></div></div><div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">entities</span></div>`;
  const cw=el('div','fd-donut'); const cc=el('canvas'); const legend=el('div','fd-legend'); cw.append(cc,legend); comp.append(cw);host.append(comp);
  function paintComposition(){
    const list=leaves(),groups=new Map();list.forEach(n=>{const k=typeOf(n).cat||'Scene';const v=groups.get(k)||{n:0,c:typeOf(n).color};v.n++;groups.set(k,v)});
    const [g,w,h]=canvas(cc,128);g.clearRect(0,0,w,h);const cx=Math.min(65,w*.25),cy=h/2,r=42,total=Math.max(1,list.length);let a=-Math.PI/2;
    [...groups].forEach(([k,v])=>{const da=v.n/total*Math.PI*2;g.beginPath();g.arc(cx,cy,r,a,a+da);g.arc(cx,cy,r-14,a+da,a,true);g.closePath();g.fillStyle=v.c;g.globalAlpha=.78;g.fill();g.globalAlpha=1;a+=da});
    g.fillStyle='#f0f0f0';g.font='300 28px ui-sans-serif,system-ui';g.textAlign='center';g.fillText(list.length,cx,cy+7);g.fillStyle='rgba(255,255,255,.35)';g.font='8px ui-sans-serif,system-ui';g.fillText('TOTAL',cx,cy+20);
    legend.innerHTML=[...groups].map(([k,v])=>`<div><i style="background:${v.c}"></i><span>${esc(k)}</span><b>${v.n}</b></div>`).join('')||'<div><span>EMPTY COLLECTION</span><b>0</b></div>';
  }

  /* ── manifest ──────────────────────────────────────────────────────────────────────────── */
  const manifest=el('div','pcard mp-light fd-manifest'); manifest.innerHTML=`<div class="mp-chead"><div class="l"><span class="t">Manifest</span><span class="s">Direct children · live state</span></div></div>`;
  const mb=el('div','pbody fd-list');manifest.append(mb);host.append(manifest);manifest.querySelector('.mp-chead .l').onclick=()=>manifest.classList.toggle('shut');
  function buildManifest(){
    mb.innerHTML='';
    if(!node.kids.length){mb.innerHTML='<div class="fd-empty">NO ENTITIES IN THIS COLLECTION</div>';return}
    node.kids.forEach(n=>{
      const row=el('div','fd-row');row.innerHTML=`<span class="fd-ic">${ic(isFolder(n)?folderIcon(n.name):typeOf(n).icon,{size:14,color:n.props?.tint||typeOf(n).color})}</span><span class="fd-who"><b>${esc(n.name)}</b><em>${esc(typeOf(n).label)}${isFolder(n)?` · ${n.kids.length} children`:''}</em></span><button class="fd-eye" title="Toggle visibility">${ic(n.vis===false?'eyeoff':'eye',{size:13})}</button>`;
      row.querySelector('.fd-eye').onclick=()=>{n.vis=n.vis===false;bus.emit('treechange');onDirty?.();paintAll()}; mb.append(row);
    });
  }

  /* ── collection state ──────────────────────────────────────────────────────────────────── */
  const state=el('div','pcard mp-light fd-state');state.innerHTML=`<div class="mp-chead"><div class="l"><span class="t">Collection state</span><span class="s">One control for the whole branch</span></div></div>`;
  const sb=el('div','pbody');state.append(sb);const tags=el('div','mp-tags');
  const visible=pillToggle('BRANCH VISIBLE',node.vis!==false,v=>{node.vis=v;bus.emit('treechange');onDirty?.();paintAll()});
  const open=pillToggle('EXPANDED',node.open!==false,v=>{node.open=v;bus.emit('treechange');paintAll()});tags.append(visible,open);sb.append(tags);
  const th=el('div','mp-subhead','<span class="k">collection colour</span>');const chip=colorChip(tint(),v=>{setProp(node,'tint',v);bus.emit('treechange');paintAll()});th.append(chip);sb.append(th);
  const note=el('div','mp-note');sb.append(note);host.append(state);

  function paintAll(){
    const list=all(),leaf=leaves(),vis=leaf.filter(n=>n.vis!==false).length,dyn=leaf.filter(n=>n.dynamic).length;
    paintHero();paintComposition();buildManifest();mDirect.textContent=node.kids.length;mTotal.textContent=list.length;mLive.textContent=vis;mDyn.textContent=dyn;
    comp.querySelector('.mp-num .i').textContent=leaf.length;visible._set(node.vis!==false);open._set(node.open!==false);chip._set?.(tint());
    note.textContent=`${node.name} contains ${leaf.length} renderable ${leaf.length===1?'entity':'entities'}. ${leaf.length-vis} hidden, ${leaf.filter(n=>n.locked).length} locked, ${dyn} dynamic.`;
  }
  register?.(paintAll);const ro=new ResizeObserver(()=>{paintHero();paintComposition()});ro.observe(host);host._dispose=()=>ro.disconnect();requestAnimationFrame(paintAll);paintAll();return host;
}
