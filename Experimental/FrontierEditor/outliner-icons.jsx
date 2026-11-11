import React,{useState} from 'react';
import sun from './custom-icons/sun.svg?url';
import moon from './custom-icons/moon.svg?url';
import stars from './ui-icons/outliner-stars.svg?url';
import sky from './custom-icons/sky-scattering.svg?url';
import clouds from './custom-icons/clouds.svg?url';
import localCloud from './ui-icons/local-cloud.svg?url';
import localFog from './ui-icons/local-fog.svg?url';
import wind from './custom-icons/wind.svg?url';
import precipitation from './ui-icons/outliner-precipitation.svg?url';
import forest from './custom-icons/forest.svg?url';
import terrain from './custom-icons/terrain.svg?url';
import liquid from './custom-icons/fluid-glass.svg?url';
import mesh from './custom-icons/editor-mesh.svg?url';
import material from './custom-icons/editor-material.svg?url';
import camera from './custom-icons/camera.svg?url';
import flare from './custom-icons/lens-flare.svg?url';
import rainbow from './custom-icons/rainbow.svg?url';
import collection from './custom-icons/collection-bracketed-objects.svg?url';
import earth from './custom-icons/earth.svg?url';
import environmentFolder from './ui-icons/folder-environment.svg?url';
import waterFolder from './ui-icons/folder-water.svg?url';
import genericFolder from './ui-icons/folder-generic.svg?url';
import sceneFolder from './custom-icons/asset-folder-scenes.svg?raw';
import modelFolder from './custom-icons/asset-folder-models.svg?raw';
import materialFolder from './custom-icons/asset-folder-materials.svg?raw';

// Compact UI variants keep the folder artwork, but never show illustrative names,
// sizes or item counts from the standalone gallery as if they were live data.
const compactFolder=svg=>'data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg.replace(/<text\b[^>]*>[\s\S]*?<\/text>/g,'').replace(/<title\b[^>]*>[\s\S]*?<\/title>/g,'').replace(/\saria-labelledby="[^"]*"/g,''));
const folders={Environment:environmentFolder,'Water bodies':waterFolder,Scene:compactFolder(sceneFolder),World:compactFolder(modelFolder),Materials:compactFolder(materialFolder)};
const assets={'local-cloud':localCloud,'local-fog':localFog,sun,moon,stars,sky,cloud:clouds,air:wind,forest,terrain,liquid,cube:mesh,material,camera,flare,rainbow,collection,project:earth};
export function outlinerIconSource(object,values={}){
 const kind=object.base||object.id;
 if(kind==='folder'){
  const category=object.category||Object.keys(folders).find(key=>key.toLowerCase()===(object.name||'').toLowerCase());
  return folders[category]||genericFolder;
 }
 if(kind==='precip')return precipitation;
 return assets[kind]||null;
}
export default function OutlinerIcon({object,values={},size=30}){
 const src=outlinerIconSource(object,values),[failed,setFailed]=useState(null),Fallback=object.icon;
 const style={width:size,height:size,flexShrink:0};
 // Retain specific primitive/species/water symbols where no matching gallery
 // artwork exists instead of labelling every shape with the same cube or tree.
 if(!src||failed===src)return Fallback?<Fallback className="outliner-icon-fallback" size={size} style={{...style,color:object.color}} aria-hidden="true"/>:null;
 return <img className="outliner-asset-icon" src={src} alt="" aria-hidden="true" draggable={false} width={size} height={size} style={style} onError={()=>setFailed(src)} data-icon-kind={object.base||object.id}/>;
}
