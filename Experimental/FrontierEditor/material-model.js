export const materialChannels=[
 ['baseColor','Base color','color','#a7b6c8'],['metallic','Metallic','number',.35,0,1],['roughness','Roughness','number',.3,.02,1],['normal','Normal','number',.25,0,1],
 ['height','Height','number',.1,0,1],['ao','Occlusion','number',.4,0,1],['emission','Emission','color','#e6a66b'],['emissionStrength','Emission gain','number',.4,0,3],
 ['opacity','Opacity','number',1,0,1],['transmission','Transmission','number',.6,0,1],['ior','IOR','number',1.5,1,2.5],['specular','Specular','number',.5,0,1],
 ['specularTint','Specular tint','color','#e3c9fa'],['coat','Clearcoat','number',.65,0,1],['coatRoughness','Coat roughness','number',.1,.02,1],['sheen','Sheen','number',.5,0,1],
 ['sheenTint','Sheen tint','color','#b89dca'],['anisotropy','Anisotropy','number',.6,0,1],['anisotropyRotation','Anisotropy angle','number',45,0,180],['subsurface','Subsurface','number',.5,0,1],
 ['subsurfaceColor','Scatter color','color','#d97c63'],['thickness','Thickness','number',.5,0,1],['iridescence','Iridescence','number',.6,0,1],['iridescenceThickness','Film thickness','number',400,100,1000]
].map(([id,name,type,value,min,max])=>({id,name,type,value,min,max}));
export const defaultChannels=['baseColor','metallic','roughness','specular','ior','opacity'];
export const materialDefaults=Object.fromEntries(materialChannels.flatMap(c=>[['mat:'+c.id,c.value],['channel:'+c.id,defaultChannels.includes(c.id)]]));
export function readMaterial(values={}){return Object.fromEntries(materialChannels.map(c=>[c.id,{enabled:values['channel:'+c.id]??defaultChannels.includes(c.id),value:values['mat:'+c.id]??c.value}]))}
