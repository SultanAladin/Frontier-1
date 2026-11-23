// Native optical model with wavelength-dependent ghost radii; mirrors LensFlare.wgsl.
// One linear-radiance kernel for the native inspector preview and HDR export.
const clamp=(X,A,B)=>Math.max(A,Math.min(B,X)),fract=X=>X-Math.floor(X),smooth=(A,B,X)=>{const T=clamp((X-A)/(B-A),0,1);return T*T*(3-2*T);};
const hue=(H,S)=>clamp(Math.abs(fract(H+S)*6-3)-1,0,1);
export const NativeFlarePresets={
 'Cinematic':{FlareAnamorphic:true,FlareWarmStreak:false,FlareBurst:true,FlareRays:8,FlareGhosts:6,FlareGhostBrightness:.35,FlareGhostShape:0,FlareGhostSpacing:1,FlareHaloBrightness:.5,FlareStreak:1},
 'Anamorphic':{FlareAnamorphic:true,FlareWarmStreak:false,FlareBurst:false,FlareRays:2,FlareGhosts:4,FlareGhostBrightness:.3,FlareGhostShape:0,FlareGhostSpacing:1,FlareHaloBrightness:.2,FlareStreak:1.5},
 'Starburst':{FlareAnamorphic:false,FlareWarmStreak:false,FlareBurst:true,FlareRays:8,FlareGhosts:3,FlareGhostBrightness:.2,FlareGhostShape:0,FlareGhostSpacing:1,FlareHaloBrightness:.1,FlareStreak:0},
 'Halo':{FlareAnamorphic:false,FlareWarmStreak:false,FlareBurst:false,FlareRays:6,FlareGhosts:0,FlareGhostBrightness:0,FlareGhostShape:0,FlareGhostSpacing:1,FlareHaloBrightness:1,FlareStreak:0}
};
export function LFRender(P,U,V,SunU,SunV,Aspect,Visibility=1){
 if(Visibility<=0||P.FlareIntensity<=0)return [0,0,0];
 const Edge=smooth(-.15,.05,SunU)*(1-smooth(.95,1.15,SunU))*smooth(-.15,.05,SunV)*(1-smooth(.95,1.15,SunV));
 const X=(U-.5)*Aspect,Y=V-.5,SX=(SunU-.5)*Aspect,SY=SunV-.5,DX=X-SX,DY=Y-SY,Spread=clamp(P.FlareSpread,.25,2),D=Math.hypot(DX,DY),A=P.FlareRotation*Math.PI/180,RX=DX*Math.cos(A)+DY*Math.sin(A),RY=-DX*Math.sin(A)+DY*Math.cos(A),C=P.FlareChromatic;
 const Cool=Math.exp(-Math.abs(DY)*95/Spread)*Math.exp(-Math.abs(DX)*2.2/Spread)*.55*(P.FlareAnamorphic?1:0)*P.FlareStreak;
 const Warm=Math.exp(-Math.abs(RY)*160/Spread)*Math.exp(-Math.abs(RX)*3/Spread)*.65*(P.FlareWarmStreak?1:0)*P.FlareStreak;
 const Angle=D>1e-8?Math.atan2(RY,RX):0,Pairs=Math.floor(clamp(P.FlarePreset==='Custom layers'?P.FlareRays:P.FlareAperture,2,12)+.5),Burst=Math.abs(Math.sin(Angle*Pairs))**32*Math.exp(-D*5/Spread)*.6*(P.FlareBurst?1:0),Core=(P.FlareAnamorphic||P.FlareWarmStreak||P.FlareBurst)?Math.exp(-D*80/Spread)*.8+Math.exp(-D*9/Spread)*.035:0;
 const RGB=[Cool*(1-.55*C)+Warm+Burst+Core,Cool*(1-.35*C)+Warm*.8+Burst*.95+Core*.9,Cool+Warm*.55+Burst*.82+Core*.75];
 const Count=Math.round(clamp(P.FlareGhosts,0,24));
 for(let I=0;I<Count;I++){
  const K=1-(.6+(I+1)/Count*(1.7+P.FlareGhostSpacing)),GX=X-SX*K,GY=Y-SY*K;let GD=Math.hypot(GX,GY);
  if(P.FlareGhostShape>=3){const Step=Math.PI*2/P.FlareGhostShape,T=GD>1e-8?Math.atan2(GY,GX):0,Fold=T-Step*Math.floor((T+Step*.5)/Step);GD*=Math.cos(Fold)/Math.cos(Math.PI/P.FlareGhostShape);}
  const Radius=(.025+.06*fract(I*.618+.31))*Spread,H=fract(I*.23+.5);
  for(let J=0;J<3;J++){const R=Radius*(1+(J-1)*C*.035),Spectral=(smooth(R,R*.35,GD)*.6+Math.exp(-Math.abs(GD-R)*350))*.14*P.FlareGhostBrightness;RGB[J]+=Spectral*(1+(hue(H,[0,.6666667,.3333333][J])-1)*C);}
 }
 const HX=X-SX*.25,HY=Y-SY*.25,HD=Math.hypot(HX,HY),Offset=Math.abs(HD-P.FlareHaloRadius),Halo=smooth(clamp(P.FlareHaloWidth,.005,.2),0,Offset)*.09*P.FlareHaloBrightness,H=fract((HD>1e-8?Math.atan2(HY,HX):0)/(Math.PI*2)+Offset*8);
 for(let J=0;J<3;J++)RGB[J]+=Halo*(1+(hue(H,[0,.6666667,.3333333][J])-1)*C*.8);
 return RGB.map(V=>V*P.FlareIntensity*Visibility*Edge);
}
export function RenderFlareImage(S,W=640,H=280){const Result=new Float32Array(W*H*3);for(let Y=0;Y<H;Y++)for(let X=0;X<W;X++)Result.set(LFRender(S,(X+.5)/W,(Y+.5)/H,S.FlarePreviewX,S.FlarePreviewY,W/H), (Y*W+X)*3);return Result;}
