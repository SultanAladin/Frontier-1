// Frozen previous-release field: regression guard for the accepted cut shape.
vec3 scratchField(vec2 uv){
   if(uScratchDensity<=0.)return vec3(0.);
   uv*=uScratchScale;vec2 cell=floor(uv);float coverage=0.,height=0.,rimTotal=0.;
   float aa=max(length(fwidth(uv))*.4,.00015);
   for(int layer=0;layer<4;layer++)for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++){
     vec2 id=cell+vec2(float(x),float(y));float layerSeed=float(layer)*113.7+uSurfaceSeed;
     float r=hash31(vec3(id,layerSeed));
     if(r<uScratchDensity*.25){
       float a=hash31(vec3(id+17.3,layerSeed+3.)),b=hash31(vec3(id+39.7,layerSeed+11.));
       vec2 center=id+.08+.84*vec2(a,b);
       float angle=uWeaveAngle+(hash31(vec3(id+81.2,layerSeed))-.5)*3.14159265*uScratchSpread;
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
   float resolved=1.-smoothstep(.3,1.2,max(fwidth(uv.x),fwidth(uv.y)));
   return vec3(mix(uScratchDensity*uScratchWidth*uScratchLength*.75,coverage,resolved),(height+rimTotal)*resolved,rimTotal);
 }
