// Shader-only math. Kept dependency-free so the exact kernels can be embedded
// in exported Three.js modules and used by the flat-patch channel baker.
export function extendedSurfaceGLSL() {
  return `
    uniform int uBakeMode;
    uniform vec3 uSecondary;
    uniform float uMoisture,uScattering,uFreckles,uPaperRibs,uFiberContrast;
    uniform float uOxidation,uPanelMode,uBusbarWidth,uDimpleDepth,uDenimFade,uSlub,uPixelFill;
    uniform float uBakeHeightRange,uEmissionScale,uAoIntensity;
    float oxideMask=0.,contactMask=0.,emitterMask=0.,candyCrystalMask=0.,candySpeckleMask=0.,candySugarRough=0.,engMetalMask=0.,engPadMask=0.,engHoleMask=0.,engEmitMask=0.;
    vec3 candyFacetDir=vec3(0.,0.,1.),engEmitColor=vec3(0.);
    vec3 bakeSRGB(vec3 c){c=max(c,vec3(0.));return mix(c*12.92,1.055*pow(c,vec3(1./2.4))-.055,step(vec3(.0031308),c));}
    vec2 surfaceUV(vec3 p,vec3 w){if(w.x>w.y && w.x>w.z)return p.zy;if(w.y>w.z)return p.xz;return p.xy;}
    float segmentDistance(vec2 p,vec2 a,vec2 b){vec2 d=b-a;return length(p-a-d*clamp(dot(p-a,d)/dot(d,d),0.,1.));}
    float knit(vec2 uv){
      uv=rotateUV(uv)*uScale;
      vec2 q=fract(uv)-.5;
      float left=segmentDistance(q,vec2(-.34,.48),vec2(.02,-.44));
      float right=segmentDistance(q,vec2(.34,.48),vec2(-.02,-.44));
      float loopD=min(left,right);
      float aa=max(max(fwidth(uv.x),fwidth(uv.y))*.35,.005);
      float yarn=1.-smoothstep(.065-aa,.15+aa,loopD);
      float sub=sin((q.x+q.y*.35)*100.)*.5+.5;
      sub=mix(.5,sub,1.-smoothstep(.02,.1,max(fwidth(uv.x),fwidth(uv.y))));
      return mix(.47,yarn*(.82+.18*sub),1.-smoothstep(.3,1.4,max(fwidth(uv.x),fwidth(uv.y))));
    }
    float dimple(vec2 uv){
      uv*=uScale;vec2 row=vec2(uv.x,uv.y/ .8660254);vec2 cell=floor(row);float nearest=2.;
      for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
        vec2 id=cell+vec2(float(x),float(y));
        vec2 center=vec2(id.x+.5*mod(id.y,2.),id.y*.8660254);
        nearest=min(nearest,length(uv-center));
      }
      float r=nearest/.44;
      float bowl=pow(max(0.,1.-r*r),1.6);
      float resolved=1.-smoothstep(.3,1.2,max(fwidth(uv.x),fwidth(uv.y)));
      return mix(.24,bowl,resolved);
    }
  `;
}

export function extendedSurfaceColor() {
  return `
    if(uType==4 && uWeave==8){
      // Undyed yarn core becomes visible on raised indigo warp yarns. Along-yarn
      // slubs vary slowly, independently of the over/under construction.
      float slubNoise=noise3(vec3(yarnUV.x*1.4,yarnUV.y*.09,21.));
      float fade=(.15+.85*smoothstep(.3,.8,slubNoise))*uDenimFade*(1.-yarnDirection);
      diffuseColor.rgb=mix(diffuseColor.rgb,uWeftColor*.85,fade);
      diffuseColor.rgb*=1.+(slubNoise-.5)*uSlub*.4;
      surfaceHeight+=(slubNoise-.5)*uSlub*.001*(1.-yarnDirection);
    }
    if(uType==12){
      float aggregate=noise3(pp*uScale)+.35*noise3(pp*uScale*3.7);
      float hand=noise3(pp*2.7);
      diffuseColor.rgb*=(.8+.23*aggregate+.08*hand)*(1.-uMoisture*.18);
      surfaceHeight=(aggregate-.5)*uGrain*.006*(1.-uMoisture*.5);
    }
    if(uType==13){
      float marble=noise3(pp*uScale*.12+vec3(noise3(pp*5.7)*2.));
      diffuseColor.rgb*=.9+marble*uGrain*.28;
      surfaceHeight=(noise3(pp*uScale)-.5)*uGrain*.0015;
    }
    if(uType==14){
      vec4 pores=cellular3(pp*uScale,63.7);
      float pit=pow(smoothstep(.48,.88,noise3(pp*uScale)),2.);
      float pigment=noise3(pp*7.3);
      float freckles=smoothstep(1.-max(.0001,uFreckles*.45),1.,noise3(pp*uScale*.27));
      diffuseColor.rgb=mix(diffuseColor.rgb,uSecondary,pigment*.13+freckles*.5);
      diffuseColor.rgb*=.94+.09*noise3(pp*3.1);
      surfaceHeight=-pit*uGrain*.0007+noise3(pp*uScale*2.1)*.00015;
    }
    if(uType==15){
      float fibers=noise3(pp*vec3(uScale*.12,uScale*3.7,uScale));
      float crossing=noise3(pp*vec3(uScale*2.1,uScale*.2,uScale));
      float pulp=smoothstep(.58,.87,fibers)*(.4+.6*crossing);
      diffuseColor.rgb=mix(diffuseColor.rgb,uSecondary,pulp*uFiberContrast);
      diffuseColor.rgb*=.95+grain*.08;
      float wave=sin(pp.x*22.)*.5+.5;
      surfaceHeight=(fibers*.6+crossing*.4)*uGrain*.0015+wave*uPaperRibs*.005;
    }
    if(uType==16){
      float patchiness=noise3(pp*uScale*.1)*.7+noise3(pp*uScale*.31)*.3;
      oxideMask=uOxidation<=0.?0.:uOxidation>=1.?1.:smoothstep(1.-uOxidation-.15,1.-uOxidation+.15,patchiness);
      float crust=noise3(pp*uScale)+.3*noise3(pp*uScale*3.);
      vec3 oxide=uSecondary*(.65+crust*.7);
      diffuseColor.rgb=mix(diffuseColor.rgb,oxide,oxideMask);
      surfaceHeight=oxideMask*(.002+crust*uGrain*.008);
    }
    if(uType==17){
      vec2 uv=surfaceUV(pp,weights)*uScale;vec2 q=fract(uv);
      float aa=max(fwidth(uv.x),fwidth(uv.y));
      float edge=min(min(q.x,1.-q.x),min(q.y,1.-q.y));
      float cellMask=smoothstep(.013,.013+max(aa,.004),edge);
      float bus=1.-smoothstep(uBusbarWidth,uBusbarWidth+max(aa,.002),abs(fract(uv.x*3.)-.5)/3.);
      float fingers=1.-smoothstep(.025,.025+max(aa*36.,.025),abs(fract(uv.y*36.)-.5));
      fingers=mix(.05,fingers,1.-smoothstep(.2,.8,aa*36.));
      contactMask=cellMask*max(bus,fingers*.7);
      vec3 silicon=diffuseColor.rgb;
      if(uPanelMode>.5 && uPanelMode<1.5){vec4 crystal=cellular3(vec3(uv*9.,.3),7.);silicon*=.65+crystal.y*.75;}
      diffuseColor.rgb=mix(vec3(.008),silicon,cellMask);
      diffuseColor.rgb=mix(diffuseColor.rgb,uSecondary,contactMask);
      surfaceHeight=cellMask*.001+contactMask*.0007;
    }
    if(uType==18){
      float bowl=dimple(pp.yz)*weights.x+dimple(pp.xz)*weights.y+dimple(pp.xy)*weights.z;
      surfaceHeight=-bowl*uDimpleDepth;
      diffuseColor.rgb*=.97+grain*.04;
    }
    if(uType==19){
      float loops=knit(pp.yz)*weights.x+knit(pp.xz)*weights.y+knit(pp.xy)*weights.z;
      if(uCloth==1)loops=knit(vProcUv*4.9);
      float melange=noise3(pp*uScale*vec3(.25,3.,2.));
      diffuseColor.rgb=mix(diffuseColor.rgb,uSecondary,smoothstep(.55,.9,melange)*.32)*(.48+loops*.57);
      surfaceHeight=loops*uGrain*.008/max(sqrt(uScale),.1)+noise3(pp*uScale*13.)*uFuzz*.0004;
    }
    if(uType==20){
      vec2 uv=surfaceUV(pp,weights)*uScale,q=fract(uv)-.5;
      float aa=max(max(fwidth(uv.x),fwidth(uv.y)),.0015);
      vec2 bevel=max(abs(q)-vec2(.31,.27),0.);
      float housing=1.-smoothstep(.035-aa,.035+aa,length(bevel));
      float recess=boxMask(q,vec2(.265,.235),aa);
      float radius=uPixelFill*.33,r=length(q)/max(radius,.01);
      ledLens=1.-smoothstep(1.-aa/radius,1.+aa/radius,r);
      float dome=sqrt(max(0.,1.-r*r));
      float pads=boxMask(vec2(abs(q.x)-.423,q.y),vec2(.044,.11),aa);
      float traces=boxMask(vec2(q.x,abs(q.y)-.447),vec2(.5,.007),aa);
      float wire=1.-smoothstep(.003,.003+aa,segmentDistance(q,vec2(-.22,.13),vec2(.04,0.)));
      wire*=recess*(1.-ledLens*.7);
      float die=boxMask(q,vec2(radius*.45,radius*.38),aa);
      float subDies=1.-smoothstep(.03,.1,abs(fract(q.x/max(radius,.01)*4.)-.5));
      float ring=(1.-smoothstep(.018,.018+aa,abs(length(q)-radius*1.13)))*(1.-ledLens);
      ledContact=max(pads,max(traces*.65,wire));
      vec3 board=diffuseColor.rgb*(.88+.15*noise3(pp*120.));
      diffuseColor.rgb=mix(board,vec3(.055,.062,.07),housing);
      diffuseColor.rgb=mix(diffuseColor.rgb,vec3(.008,.012,.017),recess);
      diffuseColor.rgb=mix(diffuseColor.rgb,vec3(.13,.15,.17),ring*.8);
      diffuseColor.rgb=mix(diffuseColor.rgb,vec3(.66,.47,.17),ledContact);
      diffuseColor.rgb=mix(diffuseColor.rgb,vec3(.34,.3,.18),die);
      emitterMask=ledLens*(.2+die*(.65+.15*subDies))*(.88+.12*hash31(vec3(floor(uv),19.)));
      surfaceHeight=(housing*.003-recess*.001+ledLens*dome*uLensDome*.012+pads*.002)*uPackageDepth;
    }
    if(uType==39){
      vec4 crystal=cellular3(pp*uScale*1.35,29.7);
      vec4 fineCrystal=cellular3(pp*uScale*3.1,47.1);
      float facetEdge=smoothstep(.04,.34,crystal.x);
      float frost=step(1.-uGrain*.88,crystal.y)*facetEdge;
      float microGlint=step(1.-uGrain*.55,fineCrystal.y)*smoothstep(.08,.3,fineCrystal.x);
      candyCrystalMask=clamp(max(frost*.9,microGlint),0.,1.);
      float sugarAngle=crystal.z*6.2831853;
      candyFacetDir=vec3(cos(sugarAngle)*sin(crystal.w*1.1),sin(sugarAngle)*sin(crystal.w*1.1),cos(crystal.w*1.1));
      vec3 glowTint=mix(diffuseColor.rgb,uTertiary,smoothstep(.25,.85,noise3(pp*uScale*.18))*.45);
      diffuseColor.rgb=mix(glowTint,uSecondary,candyCrystalMask*(.55+.35*crystal.w));
      surfaceHeight=(crystal.x*frost+fineCrystal.x*microGlint*.5)*uGrain*.012/max(sqrt(uScale*.1),.25);
    }
    if(uType==40){
      float warp=noise3(pp*uScale*.35)*1.45+noise3(pp*uScale*.9)*.42;
      float phase=(pp.y*1.95+pp.x*1.35+warp)*uScale*2.2;
      float stripe=sin(phase)*.5+.5;
      float aa=max(fwidth(phase)*.35,.012);
      float ribbon=smoothstep(.5-aa*.7,.5+aa*.7,stripe);
      float secondaryBand=smoothstep(.76-aa,.88+aa,sin(phase*2.+1.1)*.5+.5);
      vec3 bodyCol=mix(diffuseColor.rgb,uSecondary,ribbon);
      bodyCol=mix(bodyCol,uTertiary,secondaryBand*.65);
      float spunMicro=noise3(pp*vec3(uScale*14.,uScale*2.5,uScale*14.));
      diffuseColor.rgb=bodyCol*(.94+.1*spunMicro);
      surfaceHeight=(sin(phase)*.0012+(spunMicro-.5)*.001*uGrain);
    }
    if(uType==41){
      vec4 bubbles=cellular3(pp*uScale*.68,51.3);
      float bubbleRim=1.-smoothstep(.08,.22,bubbles.x);
      float poreFrost=noise3(pp*uScale*2.4);
      candyCrystalMask=step(1.-uGrain*.35,poreFrost)*smoothstep(.35,.8,noise3(pp*uScale*4.7));
      vec3 syrupCore=mix(diffuseColor.rgb,uTertiary,smoothstep(.35,.85,noise3(pp*uScale*.25))*.32);
      syrupCore=mix(syrupCore,uSecondary,bubbleRim*(.28+bubbles.w*.25)+candyCrystalMask*.48);
      diffuseColor.rgb=syrupCore;
      surfaceHeight=(poreFrost-.5)*uGrain*.0025-bubbleRim*.0012;
    }
    if(uType==42){
      vec4 nonpareil=cellular3(pp*uScale*1.15,83.1);
      float aa=max(fwidth(nonpareil.x)*.55,.01);
      float bead=1.-smoothstep(.24-aa,.24+aa,nonpareil.x);
      float occ=step(1.-clamp(uFlakes,.15,1.)*.78,nonpareil.y);
      candySpeckleMask=bead*occ;
      vec3 confetti=mix(pickColor(nonpareil.z),mix(uSecondary,uTertiary,step(.5,nonpareil.w)),.28);
      diffuseColor.rgb=mix(diffuseColor.rgb,confetti,candySpeckleMask);
      surfaceHeight=candySpeckleMask*max(0.,.24-nonpareil.x)*.012;
    }
    if(uType==43){
      vec2 uv=surfaceUV(pp,weights)*uScale;
      vec2 q=fract(uv)-.5;
      float aa=max(max(fwidth(uv.x),fwidth(uv.y)),.0012);
      vec2 cid=floor(uv);
      float h=hash31(vec3(cid,7.3));
      float h2=hash31(vec3(cid,19.1));
      float bw=max(uBusbarWidth,.018);
      float busX=1.-smoothstep(bw-aa,bw+aa,abs(q.x));
      float busY=1.-smoothstep(bw-aa,bw+aa,abs(q.y));
      float diag=1.-smoothstep(bw*.8-aa,bw*.8+aa,abs(q.x+q.y-0.0)*.707);
      float traceA=h>.45?busX:busY;
      float traceB=h2>.5?diag:busY*step(.35,h2);
      float parallel=1.-smoothstep(bw*.62-aa,bw*.62+aa,abs(abs(q.x)-.19));
      float traces=max(max(traceA,traceB),parallel*step(.42,h));
      vec2 vp=fract(uv*2.)-.5;
      float vr=length(vp);
      float pad=1.-smoothstep(.24-aa*1.5,.24+aa*1.5,vr);
      float drill=1.-smoothstep(.105-aa*1.5,.105+aa*1.5,vr);
      float viaOcc=step(.52,hash31(vec3(floor(uv*2.),31.7)));
      float ring=max(0.,pad-drill)*viaOcc;
      float hole=drill*viaOcc;
      float weave=sin(uv.x*64.)*sin(uv.y*64.)*.5+.5;
      float silk=boxMask(q-vec2(.32,-.32),vec2(.045,.045),aa)*step(.7,h2);
      vec3 solder=diffuseColor.rgb*(.88+.16*weave);
      vec3 copperTrace=mix(solder,uSecondary,.52+traces*.42);
      diffuseColor.rgb=mix(solder,copperTrace,traces*(1.-hole));
      diffuseColor.rgb=mix(diffuseColor.rgb,uTertiary,ring);
      diffuseColor.rgb=mix(diffuseColor.rgb,vec3(.02,.025,.022),hole);
      diffuseColor.rgb=mix(diffuseColor.rgb,vec3(.92,.93,.9),silk);
      engMetalMask=clamp(max(ring*.96,traces*.35),0.,1.);
      engPadMask=ring;
      engHoleMask=hole;
      surfaceHeight=(traces*.00045+ring*.0008-hole*.0024);
    }
    if(uType==44){
      vec2 uv=surfaceUV(pp,weights)*uScale;
      vec2 q=fract(uv)-.5;
      vec2 cid=floor(uv);
      float aa=max(max(fwidth(uv.x),fwidth(uv.y)),.0012);
      float border=1.-boxMask(q,vec2(.465,.465),aa);
      vec2 sub=fract(uv*4.)-.5;
      float subGap=1.-boxMask(sub,vec2(.42,.42),aa*4.);
      float sectorHash=hash31(vec3(floor(uv*4.),cid.x*3.+cid.y));
      float logicGrid=sin(uv.x*78.)*sin(uv.y*78.)*.5+.5;
      logicGrid=mix(.5,logicGrid,1.-smoothstep(.08,.45,aa*26.));
      float pads=boxMask(vec2(abs(q.x)-.43,fract(uv.y*12.)-.5),vec2(.022,.22),aa*2.);
      pads=max(pads,boxMask(vec2(fract(uv.x*12.)-.5,abs(q.y)-.43),vec2(.22,.022),aa*2.));
      float dieZone=mix(.68,1.18,sectorHash)*(.82+.24*logicGrid);
      vec3 waferCol=mix(diffuseColor.rgb*dieZone,uSecondary,subGap*.65);
      waferCol=mix(waferCol,uSecondary*.5,border*.8);
      waferCol=mix(waferCol,uTertiary,pads*.92);
      diffuseColor.rgb=waferCol;
      engMetalMask=clamp(.68+pads*.3+subGap*.18,0.,1.);
      engPadMask=clamp(pads+border*.5,0.,1.);
      surfaceHeight=(pads*.0006-border*.0012-subGap*.0004);
    }
    if(uType==45){
      vec2 uv=surfaceUV(pp,weights)*uScale;
      float aa=max(max(fwidth(uv.x),fwidth(uv.y)),.0015);
      if(uPanelMode<.5){
        vec2 cell=fract(uv)-.5;
        vec2 sub=vec2(fract(uv.x*3.)-.5,cell.y);
        float pxMask=boxMask(cell,vec2(.44,.44),aa);
        float sMask=boxMask(sub,vec2(.35,.38),aa*2.2)*pxMask;
        float chan=floor(mod(uv.x*3.,3.));
        vec3 subCol=chan<.5?vec3(1.,.1,.08):chan<1.5?vec3(.08,1.,.18):vec3(.12,.34,1.);
        float lum=.82+.18*hash31(vec3(floor(uv),floor(uv.x*3.)));
        engEmitColor=subCol*lum;
        engEmitMask=sMask;
        diffuseColor.rgb=mix(vec3(.012,.014,.018),subCol*.16,sMask);
        engMetalMask=0.;
        surfaceHeight=-.0005*(1.-sMask);
      }else if(uPanelMode<1.5){
        float fin=sin(uv.x*6.2831853*3.)*.5+.5;
        float slot=smoothstep(.25,.72,fin);
        float crossChannel=1.-smoothstep(.04,.09,abs(fract(uv.y*.5)-.5));
        float brushed=noise3(pp*vec3(uScale*1.5,uScale*24.,uScale*1.5));
        diffuseColor.rgb=mix(uTertiary,mix(diffuseColor.rgb,uSecondary,.35+.5*brushed),slot*(1.-crossChannel*.6));
        engMetalMask=.94;
        engHoleMask=clamp(1.-slot+crossChannel*.6,0.,1.);
        surfaceHeight=(slot*(1.-crossChannel*.75)-.5)*.008*uGrain;
      }else{
        vec2 row=vec2(uv.x,uv.y/.8660254);
        vec2 c0=floor(row);
        float best=9.;vec2 bestId=c0;
        for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
          vec2 id=c0+vec2(float(x),float(y));
          vec2 ctr=vec2(id.x+.5*mod(id.y,2.),id.y*.8660254);
          float d=length(uv-ctr);
          if(d<best){best=d;bestId=id;}
        }
        float coreR=uPixelFill*.34;
        float core=1.-smoothstep(coreR*.58-aa,coreR*.58+aa,best);
        float clad=1.-smoothstep(coreR*1.12-aa,coreR*1.12+aa,best);
        float rnd=.65+.35*hash31(vec3(bestId,11.3));
        engEmitColor=uSecondary*rnd;
        engEmitMask=core;
        diffuseColor.rgb=mix(diffuseColor.rgb,uTertiary,clad*(1.-core));
        diffuseColor.rgb=mix(diffuseColor.rgb,uSecondary*.25,core);
        surfaceHeight=(clad*.0012+core*.0008);
      }
    }
    if(uType==46){
      vec2 uv=surfaceUV(pp,weights)*uScale;
      float aa=max(max(fwidth(uv.x),fwidth(uv.y)),.0015);
      if(uPanelMode<.5){
        vec2 row=vec2(uv.x,uv.y/.8660254);
        vec2 c0=floor(row);
        float best=9.;
        for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
          vec2 id=c0+vec2(float(x),float(y));
          vec2 ctr=vec2(id.x+.5*mod(id.y,2.),id.y*.8660254);
          best=min(best,length(uv-ctr));
        }
        float holeR=uPixelFill*.37;
        float hole=1.-smoothstep(holeR-aa,holeR+aa,best);
        float rim=1.-smoothstep(holeR+.06-aa,holeR+.06+aa,best);
        diffuseColor.rgb=mix(diffuseColor.rgb,uTertiary,rim*(1.-hole)*.55);
        diffuseColor.rgb=mix(diffuseColor.rgb,uSecondary,hole);
        engMetalMask=(1.-hole)*.94;
        engHoleMask=hole;
        surfaceHeight=(1.-hole)*.0024-hole*.003;
      }else{
        float phase=uv.x*6.2831853*2.4;
        float groove=sin(phase)*.5+.5;
        float aaG=1.-smoothstep(.45,2.2,fwidth(phase));
        float ruled=mix(.5,groove,aaG);
        diffuseColor.rgb=mix(uTertiary,mix(diffuseColor.rgb,uSecondary,ruled),(.55+.45*ruled));
        engMetalMask=.95;
        surfaceHeight=(ruled-.5)*.0016;
      }
    }
  `;
}
