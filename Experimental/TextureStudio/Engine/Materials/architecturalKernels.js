// Pure math shared by the live material, standalone shader export and baker.
export function architecturalGLSL() {
  return `
 uniform vec3 uTertiary;
 uniform int uRibProfile,uSheetFinish;
 uniform float uSheetOxidation,uSheetSpangle;
 float sheetOxide=0.,sheetRoughness=.5;
 uniform float uRibDepth,uGroutWidth,uTileVariation,uTileStagger,uVeinWidth,uPoreDensity,uWallMode;
 uniform float uLeafAspect,uBladeWidth,uBladeLean,uSurfaceSeed;
 uniform float uScratchAngle;
 uniform float uScratchDensity,uScratchLength,uScratchWidth,uScratchDepth,uScratchSpread,uScratchBend;
 uniform float uLensDome,uPackageDepth;
 float groutMask=0.,scratchMask=0.,surfaceCoverage=1.,ledContact=0.,ledLens=0.;
 float stoneFBM(vec3 p){return .5333*noise3(p)+.2667*noise3(p*2.03+7.1)+.1333*noise3(p*4.17+19.3)+.0667*noise3(p*8.31+41.);}
 float boxMask(vec2 p,vec2 halfSize,float aa){return 1.-smoothstep(-aa,aa,max(abs(p.x)-halfSize.x,abs(p.y)-halfSize.y));}
 // Search neighboring cells, not one repeated scratch pasted at each cell center.
 // A cut may cross its seed cell's boundary; the neighbor search keeps it intact.
 // Each candidate has independent occupancy, endpoints, width, depth and bow.
 vec3 scratchField(vec2 uv){
   if(uScratchDensity<=0.)return vec3(0.);
   uv*=uScratchScale;vec2 cell=floor(uv);float coverage=0.,height=0.,rimTotal=0.;
   float aa=max(length(fwidth(uv))*.4,.00015);
   for(int layer=0;layer<12;layer++){
    float batch=float(layer/4)*4.;if(batch>=uScratchDensity)break;
    for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++){
     vec2 id=cell+vec2(float(x),float(y));float layerSeed=float(layer)*113.7+uSurfaceSeed;
     float r=hash31(vec3(id,layerSeed));
     if(r<clamp((uScratchDensity-batch)*.25,0.,1.)){
       float a=hash31(vec3(id+17.3,layerSeed+3.)),b=hash31(vec3(id+39.7,layerSeed+11.));
       vec2 center=id+.08+.84*vec2(a,b);
       float angle=uScratchAngle+(hash31(vec3(id+81.2,layerSeed))-.5)*3.14159265*uScratchSpread;
       vec2 direction=vec2(cos(angle),sin(angle)),side=vec2(-direction.y,direction.x);
       vec2 delta=uv-center;
       float len=uScratchLength*mix(.18,1.,a*a);
       float t=dot(delta,direction)/max(len,.001)+.5;
       float along=clamp(t,0.,1.);
       float bend=uScratchBend*(b-.5)*2.*len/max(uScratchLength,.001);
       float bow=(sin(along*3.14159265)+.2*sin(along*6.2831853+a*2.)*sin(along*3.14159265))*bend;
       float distanceToCut=abs(dot(delta,side)-bow);
       float taper=smoothstep(0.,.12,t)*(1.-smoothstep(.84,1.,t));
       float width=uScratchWidth*mix(.35,1.4,hash31(vec3(id-31.1,layerSeed)))*(.2+.8*sqrt(taper));
       float depth=uScratchDepth*mix(.35,1.,b);
       float cut=(1.-smoothstep(width-aa,width+aa,distanceToCut))*taper*min(1.,width/aa);
       float bowl=pow(max(0.,1.-distanceToCut/max(width,.0001)),1.3)*taper;
       bowl=mix(bowl,cut*.5,smoothstep(width*.4,width*1.5,aa));
       float lip=exp(-pow((distanceToCut-width*1.65)/max(width*.45,aa),2.))*taper*min(1.,width/aa);
       height=min(height,-depth*bowl);rimTotal=max(rimTotal,lip*depth*.16);
       coverage=max(coverage,cut);
     }
   }
   }
   float resolved=1.-smoothstep(.3,1.2,max(fwidth(uv.x),fwidth(uv.y)));
   return vec3(mix(uScratchDensity*uScratchWidth*uScratchLength*.75,coverage,resolved),(height+rimTotal)*resolved,rimTotal);
 }
`;
}

export function architecturalColor() {
  return `
 if(uType==21){
   if(uSheetFinish==0){
     float phase=pp.y*uScale*6.2831853;
     float resolved=1.-smoothstep(.5,3.1,fwidth(phase));
     surfaceHeight=(cos(phase)*.5+.5)*uRibDepth*resolved;
     diffuseColor.rgb*=.94+.07*noise3(pp*vec3(100.,5.,100.));
   }else{
     vec2 uv=rotateUV(surfaceUV(pp,weights));
     float coord=uv.y*uScale,phase=coord*6.2831853;
     float resolved=1.-smoothstep(.5,3.1,fwidth(phase));
     float t=fract(coord),rib=cos(phase)*.5+.5;
     if(uRibProfile==1)rib=smoothstep(.16,.33,t)*(1.-smoothstep(.67,.84,t));
     if(uRibProfile==2)rib=smoothstep(.07,.2,t)*(1.-smoothstep(.8,.93,t));
     surfaceHeight=rib*uRibDepth*resolved;
     float detail=1.-smoothstep(.45,1.5,length(fwidth(uv*52.)));
     vec4 spangle=cellular3(vec3(uv*52.,uSurfaceSeed*.01),uSurfaceSeed);
     sheetRoughness=mix(.5,spangle.z,detail);
     sheetOxide=smoothstep(.43,.75,stoneFBM(vec3(uv*2.7,uSurfaceSeed*.013)))*uSheetOxidation;
     diffuseColor.rgb*=.94+(spangle.y-.5)*uSheetSpangle*.24*detail;
     diffuseColor.rgb=mix(diffuseColor.rgb,vec3(.48)*(.9+grain*.16),sheetOxide);
   }
 }
 if(uType==22 && uPotterySurface==0){
   vec2 uv=surfaceUV(pp,weights)*uScale;uv.x+=floor(uv.y)*uTileStagger;
   vec2 tile=floor(uv),q=fract(uv);float edge=min(min(q.x,1.-q.x),min(q.y,1.-q.y));
   float aa=max(max(fwidth(uv.x),fwidth(uv.y)),.001);
   groutMask=1.-smoothstep(uGroutWidth*.5-aa,uGroutWidth*.5+aa,edge);
   float bevel=smoothstep(uGroutWidth*.5,uGroutWidth*.5+.065,edge);
   float variation=hash31(vec3(tile,31.));
   vec3 glaze=diffuseColor.rgb*(1.+(variation-.5)*uTileVariation);
   diffuseColor.rgb=mix(glaze,uSecondary*(.9+grain*.16),groutMask);
   surfaceHeight=bevel*uGrain*.012+(1.-groutMask)*noise3(pp*uScale*5.)*uTileVariation*.003;
 }
 if(uType==23){
   vec3 domain=pp*uScale;domain+=vec3(noise3(domain*.3),noise3(domain*.3+7.),noise3(domain*.3+21.))*.6;
   vec4 mineral=cellular3(domain,19.);
   vec3 color=mineral.y<.23?uTertiary:(mineral.y<.58?uSecondary:diffuseColor.rgb);
   color*=.8+mineral.z*.35;
   float seam=1.-smoothstep(.005,.035,mineral.x);
   vec3 meanColor=uTertiary*.23+uSecondary*.35+diffuseColor.rgb*.42;
   float resolved=1.-smoothstep(.4,1.5,length(fwidth(domain)));
   diffuseColor.rgb=mix(meanColor,color*(1.-seam*.13),resolved);
   surfaceHeight=(mineral.w-.5)*uGrain*.001*(1.-seam)*resolved;
 }
 if(uType==24){
   vec3 domain=pp*uScale;
   vec3 warp=vec3(stoneFBM(domain*.6),stoneFBM(domain*.6+13.),stoneFBM(domain*.6+37.));
   float field=stoneFBM(domain+warp*5.2);
   float d=abs(field-.5),aa=max(fwidth(field),.001);
   float vein=1.-smoothstep(uVeinWidth-aa,uVeinWidth+aa,d);
   float fine=1.-smoothstep(.007,.018,abs(noise3(domain*3.7+warp*7.)-.5));
   vein=max(vein,fine*.26);
   diffuseColor.rgb=mix(diffuseColor.rgb*(.93+.12*field),uSecondary,vein*.86);
   surfaceHeight=(field-.5)*uGrain*.0009;
 }
 if(uType==25 || uType==26){
   float sand=stoneFBM(pp*uScale),cloud=stoneFBM(pp*2.8);
   float pores=uPoreDensity<=0.?0.:smoothstep(1.-uPoreDensity*.42,1.,noise3(pp*uScale*.45));
   if(uType==26)pores=0.;
   diffuseColor.rgb*=.8+cloud*.28+sand*.09;
   diffuseColor.rgb=mix(diffuseColor.rgb,uSecondary,pores*.65);
   surfaceHeight=(sand-.5)*uGrain*(uType==26?.022:.006)-pores*.006;
   if(uType==25 && uWallMode>.5){
     float seam=1.-smoothstep(.009,.025,abs(fract(pp.y*1.7)-.5));
     float timber=noise3(pp*vec3(5.,120.,9.));
     surfaceHeight-=seam*.003;surfaceHeight+=timber*.0007;
     diffuseColor.rgb*=1.-seam*.13+(timber-.5)*.06;
   }
 }
 if(uType==27){
   vec2 uv=uBakeMode>0?pp.xy:vProcUv;
   float y=clamp(uv.y,0.,1.),center=.5+.025*sin(y*4.);
   float halfWidth=uLeafAspect*pow(max(0.,sin(y*3.14159265)),.85)*(1.+.02*sin(y*87.));
   float x=uv.x-center,aa=max(length(fwidth(uv)),.001);
   float body=1.-smoothstep(halfWidth-aa,halfWidth+aa,abs(x));
   float stem=1.-smoothstep(.005,.009,abs(x));
   surfaceCoverage=max(body,stem*(1.-smoothstep(.02,.13,y)))*step(0.,uv.y)*step(uv.y,1.);
   float midrib=1.-smoothstep(.003,.009,abs(x));
   float branchCoord=y*uScale-abs(x)*uScale*.65+sin(abs(x)*10.)*.17+.14*noise3(vec3(y*4.,abs(x)*3.,uSurfaceSeed));
   float branch=1.-smoothstep(.018,.08,abs(fract(branchCoord)-.5));
   branch*=smoothstep(.008,.04,abs(x))*(1.-smoothstep(.75,1.,abs(x)/max(halfWidth,.001)));
   float fine=1.-smoothstep(.008,.04,abs(fract(branchCoord*3.1+abs(x)*7.)-.5));
   float veins=max(midrib,branch*.65);
   float pigment=stoneFBM(vec3(uv*9.,5.));
   diffuseColor.rgb=mix(diffuseColor.rgb*(.75+pigment*.45),uSecondary,veins*.75+fine*.045);
   surfaceHeight=(veins*.0025+fine*.00015)*uGrain;
 }
 if(uType==28){
   vec2 uv=uBakeMode>0?pp.xy:vProcUv;
   float fieldX=uv.x*uScale,cell=floor(fieldX),cover=0.,bladeLight=0.,bladeHeight=0.;
   for(int layer=0;layer<3;layer++)for(int neighbor=-2;neighbor<=2;neighbor++){
     float id=cell+float(neighbor),l=float(layer);
     float r=hash31(vec3(id,l,uSurfaceSeed)),s=hash31(vec3(id+17.,l,19.));
     float lengthBlade=.5+r*.48,base=l*.012;
     float t=(uv.y-base)/lengthBlade;
     float center=id+.15+s*.7+(r-.5)*uBladeLean*4.*t*t;
     float width=uBladeWidth*pow(max(0.,1.-t),.65);
     float dist=abs(fieldX-center),aa=max(fwidth(fieldX),.002);
     float mask=(1.-smoothstep(width-aa,width+aa,dist))*smoothstep(0.,.035,t)*(1.-smoothstep(.97,1.,t))*step(0.,t);
     float roll=sqrt(max(0.,1.-pow(dist/max(width,.001),2.)));
     bladeLight=mix(bladeLight,.3+r*.3+roll*.3+clamp(t,0.,1.)*.1,mask);
     bladeHeight=mix(bladeHeight,roll*uGrain*.004,mask);cover=max(cover,mask);
   }
   surfaceCoverage=cover;
   diffuseColor.rgb=mix(diffuseColor.rgb,uSecondary,bladeLight*.6)*(.65+bladeLight*.5);
   surfaceHeight=bladeHeight;
 }
 if(uType==29){
   vec3 scratches=scratchField(surfaceUV(pp,weights));
   scratchMask=scratches.x;surfaceHeight=scratches.y;
   diffuseColor.rgb*=1.-scratchMask*.11;
 }
`;
}
