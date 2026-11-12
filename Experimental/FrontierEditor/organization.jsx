import React,{useState,useEffect} from 'react';
import OutlinerIcon from './outliner-icons.jsx';
import {Folder,Layers,ChevronRight,ChevronDown,Eye,EyeOff,Link2,Plus,X,Box} from 'lucide-react';
export const initialOrganization=[{id:'folder-world',kind:'folder',name:'World Assets',members:['forest','terrain']},{id:'collection-night',kind:'collection',name:'Night Sky',members:['moon','stars']}];
export function migrateOrganization(stored,objects){
 if(stored.organizationVersion===2&&stored.organization)return stored.organization;
 const categories=[['Environment','Environment'],['World','World'],['Water bodies','Water Bodies'],['Scene','Scene'],['Materials','Materials']];
 const previous=stored.organization||initialOrganization;
 const groups=previous.map(g=>({...g,members:[...g.members]}));
 for(const [category,name] of categories){
  const id='folder-'+category.toLowerCase().replaceAll(' ','-');
  let folder=groups.find(g=>g.id===id);
  if(!folder){folder={id,kind:'folder',name,members:[],category};groups.push(folder)}
  folder.category=category;
  if(folder.name==='World Assets')folder.name='World';
  const owned=new Set(groups.filter(g=>g.kind==='folder').flatMap(g=>g.members));
  folder.members.push(...objects.filter(o=>!o.parent&&o.group===category&&!owned.has(o.id)).map(o=>o.id));
 }
 return groups;
}
export function organizationNode(item){return {...item,base:item.kind,type:item.kind==='folder'?'Scene folder':'Object collection',group:item.kind==='folder'?'Folders':'Collections',color:item.kind==='folder'?'#c5af87':'#afa0d2',icon:item.kind==='folder'?Folder:Layers,isContainer:true};}

export function SceneTree({all,organization,selected,onSelect,collapsed,setCollapsed,isHidden,toggleHidden,query,values={}}){
 useEffect(()=>{document.querySelector('.tree-row.selected')?.scrollIntoView({block:'nearest'})},[selected,all.length]);
 const folderMembers=new Set(organization.filter(g=>g.kind==='folder').flatMap(g=>g.members));
 const children=o=>o.isContainer?o.members.map(id=>all.find(n=>n.id===id)).filter(Boolean):all.filter(n=>n.parent===o.id);
 const matches=o=>o.name.toLowerCase().includes(query.toLowerCase())||children(o).some(matches);
 const row=(o,depth=0,reference=false,path='')=>{
  const kids=children(o),open=!collapsed[o.id]||!!query;
  return <React.Fragment key={path+o.id}><div className={`tree-row organized-row ${selected===o.id?'selected':''} ${isHidden(o.id)?'hidden-object':''} ${depth?'nested-object':''}`} style={{'--entity-color':o.color,'--tree-depth':Math.min(depth,4)}}>
  {kids.length>0&&<button className="tree-expander" aria-label={`${open?'Collapse':'Expand'} ${o.name}`} aria-expanded={open} onClick={()=>setCollapsed(s=>({...s,[o.id]:open}))}>{open?<ChevronDown size={11}/>:<ChevronRight size={11}/>}</button>}
  <button className="object-button" onClick={()=>onSelect(o.id)}><OutlinerIcon object={o} values={values[o.id]||{}}/><span>{o.name}</span>{reference&&<Link2 className="reference-mark" size={10}/>}</button><button className="visibility" disabled={!!o.native} title={o.native?'Native visibility editing is not connected yet':undefined} aria-label={`${isHidden(o.id)?'Show':'Hide'} ${o.name}`} onClick={()=>toggleHidden(o.id)}>{isHidden(o.id)?<EyeOff size={13}/>:<Eye size={13}/>}</button></div>
  {open&&kids.filter(n=>!query||matches(n)||o.name.toLowerCase().includes(query.toLowerCase())).map(n=>row(n,depth+1,o.base==='collection',path+o.id+'/'))}</React.Fragment>;
 };
 const groups=['Folders','Collections','Environment','World','Water bodies','Scene','Materials'];
 const roots=all.filter(o=>!o.parent&&!folderMembers.has(o.id));
 const visible=roots.filter(matches);
 return <div className="tree" aria-label="Scene hierarchy">{groups.map(group=>{const list=visible.filter(o=>o.group===group);if(!list.length)return null;return <div className="group" key={group}><button className="group-label" aria-expanded={!collapsed[group]} onClick={()=>setCollapsed(s=>({...s,[group]:!s[group]}))}>{collapsed[group]?<ChevronRight size={13}/>:<ChevronDown size={13}/>}<span>{group}</span><span className="group-count">{list.length}</span></button>{(!collapsed[group]||query)&&list.map(o=>row(o))}</div>})}{!visible.length&&<p className="empty">No objects found.</p>}</div>;
}

export function OrganizationInspector({node,all,card,onRename,onMember,onSelect,isHidden}){
 const [target,setTarget]=useState('');
 const members=node.members.map(id=>all.find(o=>o.id===id)).filter(Boolean);
 const candidates=all.filter(o=>!o.parent&&!o.isContainer&&!node.members.includes(o.id));
 const folder=node.base==='folder';
 return <>
 {card(folder?'Folder overview':'Collection overview',folder?Folder:Layers,<><div className="organization-summary"><div className="organization-emblem"><OutlinerIcon object={node} size={64}/></div><div><div className="eyebrow">{folder?'HIERARCHY':'REFERENCE SET'}</div><div className="metric">{members.length}<small>{folder?'objects':'members'}</small></div><p className="muted">{folder?'Objects live here in the outliner.':'References leave objects in their original location.'}</p></div></div><label className="organization-name"><span>{folder?'Folder':'Collection'} name</span><input aria-label="Organization name" value={node.name} maxLength={60} onChange={e=>onRename(e.target.value)} onBlur={()=>{if(!node.name.trim())onRename(folder?'Untitled folder':'Untitled collection')}}/></label><div className="organization-stat-row"><span><i/>{members.filter(o=>!isHidden(o.id)).length} visible</span><span>{members.filter(o=>isHidden(o.id)).length} hidden</span><span>{folder?'Single folder ownership':'Reusable references'}</span></div></>,'wide-card')}
 {card(folder?'Folder contents':'Collection members',Box,<><div className="organization-members">{members.map(o=><div key={o.id}><button className="organization-member-link" onClick={()=>onSelect(o.id)}><OutlinerIcon object={o} size={28}/><span>{o.name}<small>{o.type}</small></span>{!folder&&<Link2 size={12}/>}</button><button className="remove-member" aria-label={`Remove ${o.name} from ${node.name}`} onClick={()=>onMember(o.id,false)}><X size={14}/></button></div>)}</div>{!members.length&&<div className="organization-empty"><Box size={27} strokeWidth={1}/><p>{folder?'No objects in this folder yet.':'No references in this collection yet.'}</p></div>}<div className="organization-add"><select aria-label="Choose organization member" value={candidates.some(o=>o.id===target)?target:''} onChange={e=>setTarget(e.target.value)}><option value="">Choose an object…</option>{candidates.map(o=><option key={o.id} value={o.id}>{o.name}</option>)}</select><button disabled={!candidates.some(o=>o.id===target)} onClick={()=>{onMember(target,true);setTarget('')}}><Plus size={14}/>{folder?'Move here':'Add reference'}</button></div><p className="organization-note">{folder?'Moving an object here also keeps its subcomponents together. Removing it returns it to its scene category.':'Members can belong to multiple collections. Removing a reference never deletes the object.'} Visibility applies to members and their subcomponents.</p></>,'wide-card')}
 </>;
}
