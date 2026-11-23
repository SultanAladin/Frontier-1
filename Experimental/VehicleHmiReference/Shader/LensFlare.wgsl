// Analytic sensor-space optics. Occlusion uses reversed-Z depth at the solar disc.


fn LFSmooth(A:f32,B:f32,X:f32)->f32{let T=clamp((X-A)/(B-A),0.0,1.0);return T*T*(3.0-2.0*T);}
fn LFHue(H:f32,Shift:f32)->f32{return clamp(abs(fract(H+Shift)*6.0-3.0)-1.0,0.0,1.0);}
fn LFColour(H:f32)->vec3f{return vec3f(LFHue(H,0.0),LFHue(H,0.6666667),LFHue(H,0.3333333));}
fn ResolveLensFlare(Pixel: vec2f) -> vec3f {
    if (Frame.Flare.x <= 0.0 || Frame.InspectionMode != 0u || Frame.SolarDisc.z <= 0.0) { return vec3f(0.0); }
    let Clip = Frame.UnjitteredViewProjection * vec4f(Frame.SolarDisc.xyz, 0.0);
    if (Clip.w <= 0.0) { return vec3f(0.0); }
    let SunUV = vec2f(Clip.x/Clip.w * 0.5+0.5, 0.5-Clip.y/Clip.w*0.5);
    if (any(SunUV < vec2f(0.0)) || any(SunUV > vec2f(1.0))) { return vec3f(0.0); }
    let Edge = min(min(SunUV.x,1.0-SunUV.x),min(SunUV.y,1.0-SunUV.y));
    let Visibility=1.0;
    if(Visibility<=0.0){return vec3f(0.0);}
    // LFRender transcription: native-based spectral formulas mirrored in NativeLensFlareKernel.js.
    let Aspect=Frame.ScreenExtent.x/Frame.ScreenExtent.y;
    let UV=Pixel/Frame.ScreenExtent;
    let XY=(UV-0.5)*vec2f(Aspect,1.0);let SXY=(SunUV-0.5)*vec2f(Aspect,1.0);
    let Delta=XY-SXY;let D=length(Delta);let Spread=clamp(Frame.Flare.y,0.25,2.0);
    let A=Frame.FlareOptics.x;let RX=Delta.x*cos(A)+Delta.y*sin(A);let RY=-Delta.x*sin(A)+Delta.y*cos(A);let Chromatic=Frame.FlareOptics.z;
    let Cool=exp(-abs(Delta.y)*95.0/Spread)*exp(-abs(Delta.x)*2.2/Spread)*0.55*Frame.FlareLayers.z*Frame.FlareHalo.w;
    let Warm=exp(-abs(RY)*160.0/Spread)*exp(-abs(RX)*3.0/Spread)*0.65*Frame.FlareLayers.w*Frame.FlareHalo.w;
    let Angle=atan2(RY,RX);
    let Pairs=select(Frame.CelestialControl.y,Frame.FlareOptics.y,Frame.FlareOptics.w>0.5);
    let Burst=pow(abs(sin(Angle*floor(clamp(Pairs,2.0,12.0)+0.5))),32.0)*exp(-D*5.0/Spread)*0.6*Frame.CelestialControl.x;
    var Core=0.0;if(Frame.FlareLayers.z+Frame.FlareLayers.w+Frame.CelestialControl.x>0.0){Core=exp(-D*80.0/Spread)*0.8+exp(-D*9.0/Spread)*0.035;}
    var Light=vec3f(Cool*(1.0-0.55*Chromatic),Cool*(1.0-0.35*Chromatic),Cool)+vec3f(1.0,0.8,0.55)*Warm+vec3f(1.0,0.95,0.82)*Burst+vec3f(1.0,0.9,0.75)*Core;
    let Count=min(24u,u32(Frame.Flare.z+0.5));
    for(var I=0u;I<Count;I++) {
        let Index=f32(I);let K=1.0-(0.6+(Index+1.0)/f32(Count)*(1.7+Frame.FlareLayers.x));
        let Ghost=XY-SXY*K;var GD=length(Ghost);let Sides=Frame.FlareLayers.y;
        if(Sides>=3.0){let Step=6.2831853/Sides;let T=atan2(Ghost.y,Ghost.x);let Fold=T-Step*floor((T+Step*0.5)/Step);GD*=cos(Fold)/cos(3.14159265/Sides);}
        let Radius=(0.025+0.06*fract(Index*0.618+0.31))*Spread;
        // Wavelength-dependent ghost radius, rather than merely tinting an achromatic ring.
        let Radii=Radius*(vec3f(1.0)+vec3f(-1.0,0.0,1.0)*Chromatic*.035);
        let T=clamp((vec3f(GD)-Radii)/(-.65*Radii),vec3f(0),vec3f(1));
        let Shape=(T*T*(vec3f(3)-2.0*T)*.6+exp(-abs(vec3f(GD)-Radii)*350.0))*.14*Frame.Flare.w;
        Light+=Shape*mix(vec3f(1.0),LFColour(fract(Index*0.23+0.5)),Chromatic);
    }
    let H=XY-SXY*0.25;let HD=length(H);let Offset=abs(HD-Frame.FlareHalo.x);
    let Halo=LFSmooth(clamp(Frame.FlareHalo.y,0.005,0.2),0.0,Offset)*0.09*Frame.FlareHalo.z;
    let Hue=fract(atan2(H.y,H.x)/6.2831853+Offset*8.0);
    Light+=Halo*mix(vec3f(1.0),LFColour(Hue),Chromatic*0.8);
    return Light*Frame.Flare.x*Visibility*smoothstep(0.0,0.06,Edge)*min(2.0,length(Frame.SolarRadiance.rgb)*0.1);
}
