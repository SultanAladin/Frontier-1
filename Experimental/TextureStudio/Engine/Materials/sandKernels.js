// Shared by the material viewport, flat-patch baker and portable shader export.
// Grain geometry is a seeded surface/normal approximation, not particle meshes.
export function sandGLSL() {
  return `
#if uType == 37 || uType == 38
 uniform int uSandCount,uSandBandsMode;
 uniform float uSandScale,uSandSeed,uSandBandFrequency,uSandBandWarp,uSandBandAngle;
 uniform vec3 uSandColors[4],uSandPale[4];
 uniform float uSandLayerSeeds[4];
 uniform vec4 uSandSize[4],uSandFinish[4],uSandShape[4];
 // size: nominal mm, variation, coverage, relative band thickness.
 // finish: roughness, roughness variation, specular, specular variation.
 // shape: IOR, roundness, facet tilt, relief.
 float sandRoughness=.88,sandSpecular=.8,sandIOR=1.544,sandResolved=0.;
 vec2 sandFacet=vec2(0.);
 struct SandCell { float metric; float second; float random; float rough; float spec; float radius; vec2 delta; vec2 id; };
 SandCell sandCellAt(vec2 q,float seed,float variation,float nominal){
   vec2 cell=floor(q),f=fract(q);SandCell best;
   best.metric=1e8;best.second=1e8;best.random=.5;best.rough=.5;best.spec=.5;best.radius=.5;best.delta=vec2(0.);best.id=vec2(0.);
   float lo=max(.0625,nominal*(1.-variation*.75))/nominal;
   float hi=min(2.,nominal*(1.+variation*.75))/nominal;
   for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++){
     vec2 id=cell+vec2(float(x),float(y));
     float sizeRandom=hash31(vec3(id,seed+9.3));
     float radius=.55*mix(lo,hi,sizeRandom);
     vec2 site=.08+.84*vec2(hash31(vec3(id,seed)),hash31(vec3(id+31.7,seed+17.1)));
     vec2 delta=vec2(float(x),float(y))+site-f;
     float metric=dot(delta,delta)/max(radius*radius,.0001);
     if(metric<best.metric){best.second=best.metric;best.metric=metric;best.delta=delta;best.id=id;best.radius=radius;
       best.random=hash31(vec3(id,seed+53.9));best.rough=hash31(vec3(id+7.1,seed+113.));best.spec=hash31(vec3(id+21.7,seed+181.));
     }else if(metric<best.second)best.second=metric;
   }
   return best;
 }
 float sandUnitVariation(float mean,float spread,float random,float lower){return clamp(mean+(random-.5)*spread,lower,1.);}
 void evaluateSand(vec2 uv,inout vec3 targetColor){
   vec3 color=targetColor;float height=0.,rough=min(1.,uSandFinish[uSandCount-1].x+.16),spec=uSandFinish[uSandCount-1].z,ior=uSandShape[uSandCount-1].x;vec2 facet=vec2(0.);float resolvedTotal=0.;
   float totalBand=0.;for(int j=0;j<4;j++)if(j<uSandCount)totalBand+=uSandSize[j].w;
   float c=cos(uSandBandAngle),s=sin(uSandBandAngle);
   vec2 bandUV=mat2(c,-s,s,c)*uv;
   float band=fract(bandUV.y*uSandBandFrequency+(noise3(vec3(bandUV*.7,uSandSeed*.013))-.5)*uSandBandWarp);
   float bandEdge=0.;int selected=0;
   for(int j=0;j<4;j++)if(j<uSandCount){float end=bandEdge+uSandSize[j].w/max(totalBand,.001);if(band>=bandEdge && band<end)selected=j;bandEdge=end;}
   // Bed 0 is uppermost: lower beds are evaluated first and are occluded by
   // the selected top grain's coverage. Bands evaluate only their own bed.
   for(int reverse=0;reverse<4;reverse++){
     int layer=3-reverse;
     if(layer>=uSandCount || (uSandBandsMode==1 && layer!=selected))continue;
     vec4 sizes=uSandSize[layer],finish=uSandFinish[layer],shape=uSandShape[layer];
     float bedSeed=uSandLayerSeeds[layer];
     float coverage=sizes.z;
     if(coverage<=0.)continue;
     float frequency=110.*uSandScale/max(sizes.x,.0625);
     vec2 domain=uv*frequency+vec2(bedSeed*71.3,bedSeed*29.7);
     domain+=(vec2(noise3(vec3(domain*.19,uSandSeed)),noise3(vec3(domain*.19+19.1,uSandSeed)))-.5)*.45;
     float footprint=max(length(dFdx(domain)),length(dFdy(domain)));
     float resolved=1.-smoothstep(.55,1.8,footprint);
     vec3 meanColor=uSandColors[layer]*.81+uSandPale[layer]*.17+uSandColors[layer]*.38*.02;
     vec3 tint=meanColor;float opacity=coverage*.78,cap=0.,roughGrain=finish.x,specGrain=finish.z;vec2 tilt=vec2(0.);
     if(resolved>.001){
       SandCell cell=sandCellAt(domain,uSandSeed+bedSeed*97.1,sizes.y,sizes.x);
       float distance=sqrt(cell.metric),aa=max(fwidth(distance)*.65,.01);
       float contact=smoothstep(0.,max(.04,aa),sqrt(cell.second)-distance);
       float outline=1.-smoothstep(1.-aa,1.+aa,distance);
       float occupied=step(1.-coverage,hash31(vec3(cell.id,uSandSeed+bedSeed*97.1+271.)));
       float grainMask=outline*contact*occupied;
       tint=mix(uSandColors[layer],uSandPale[layer],smoothstep(.7,.92,cell.random));
       tint=mix(tint,tint*.38,step(.97,cell.random));
       tint*=.86+.28*cell.rough;
       float rounded=pow(max(0.,1.-cell.metric),mix(.2,.85,shape.y));
       cap=rounded*grainMask*sizes.x*.01*shape.w*.62/max(uSandScale,.1);
       roughGrain=sandUnitVariation(finish.x,finish.y,cell.rough,.04);
       specGrain=sandUnitVariation(finish.z,finish.w,cell.spec,0.);
       float angle=cell.random*6.2831853;
       tilt=vec2(cos(angle),sin(angle))*shape.z*(.35+.65*cell.spec);
       opacity=mix(opacity,grainMask,resolved);
       tint=mix(meanColor,tint,resolved);
       roughGrain=mix(finish.x,roughGrain,resolved);specGrain=mix(finish.z,specGrain,resolved);
     }
     color=mix(color,tint,opacity);height=mix(height,cap,opacity);
     rough=mix(rough,roughGrain,opacity);spec=mix(spec,specGrain,opacity);ior=mix(ior,shape.x,opacity);
     facet=mix(facet,tilt*resolved,opacity);resolvedTotal=mix(resolvedTotal,resolved,opacity);
   }
   targetColor=color;surfaceHeight=height;
   sandRoughness=rough;sandSpecular=spec;sandIOR=ior;sandFacet=facet;sandResolved=resolvedTotal;
 }
#endif
`;
}
export function sandColor() {
  return `
#if uType == 37 || uType == 38
 evaluateSand(surfaceUV(pp,weights),diffuseColor.rgb);
#endif
`;
}
