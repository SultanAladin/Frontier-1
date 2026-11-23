//! include "NativeTwilight.wgsl"
//! include "NightStars.wgsl"
// Analytic primary-ray celestial layer; intentionally excluded from diffuse GI LUTs.
fn SkyHash(P: vec3f) -> f32 { return fract(sin(dot(P, vec3f(127.1,311.7,74.7))) * 43758.5453); }
fn CelestialSky(D: vec3f, Atlas: texture_2d_array<f32>, AtlasSampler: sampler) -> vec3f {
    let Sun = Frame.SolarDisc.xyz;
    let Angle = acos(clamp(dot(D, Sun), -1.0, 1.0));
    let Footprint = max(0.00005, Frame.StarControl.x);
    let Radius = Frame.SolarDisc.w;
    let Limb = sqrt(max(0.0, 1.0 - pow(Angle / Radius, 2.0)));
    let Day = smoothstep(-0.02, 0.02, Sun.z);
    var Result = Frame.SolarRadiance.rgb * Frame.CelestialControl.z * Day * (1.0-smoothstep(Radius-Footprint,Radius+Footprint,Angle)) * (0.4+0.6*Limb) * 2500.0;
    Result += Frame.SolarRadiance.rgb*Frame.CelestialControl.z*Frame.SolarRadiance.w*(exp(-Angle*40.0)*0.35+exp(-Angle*9.0)*0.03+exp(-Angle*2.5)*0.004);
    Result += NativeTwilightLine(D,Sun,Frame.Twilight.x,Frame.Twilight.y,Frame.Twilight.z)*Frame.Twilight.w;
    let Night = (1.0-smoothstep(-12.0, -2.0, Frame.Night.w)) * smoothstep(0.0,0.12,D.z);
    Result+=ResolveNightStars(D,Footprint);

    // Native MoonAlong: sphere normal -> roll, axial tilt, spin -> sRGB atlas.
    for(var I=0u;I<4u;I++) {
        let M=Frame.MoonBodies[I];let Md=M.Direction.xyz;let RadiusM=max(1e-6,M.Params.x);
        if(M.Params.y<=0.0 || D.z<=0.0 || Md.z<=0.0){continue;}
        let A=acos(clamp(dot(D,Md),-1.0,1.0));
        if(A<RadiusM+Footprint) {
            let Axis=select(vec3f(0.0,0.0,1.0),vec3f(0.0,1.0,0.0),abs(Md.z)>0.9998);
            let U=normalize(cross(Md,Axis));let V=cross(U,Md);
            let LC=vec2f(dot(D,U),dot(D,V))/max(1e-6,sin(RadiusM));
            let Z=sqrt(max(0.0,1.0-dot(LC,LC)));
            let CR=cos(M.Direction.w);let SR=sin(M.Direction.w);
            let N=vec3f(LC.x*CR-LC.y*SR,LC.x*SR+LC.y*CR,Z);
            let CT=cos(M.Surface.y);let ST=sin(M.Surface.y);
            let NT=vec3f(N.x,N.y*CT-N.z*ST,N.y*ST+N.z*CT);
            let UV=vec2f(fract((atan2(NT.x,NT.z)+M.Surface.x)/6.2831853+0.5),0.5-asin(clamp(NT.y,-1.0,1.0))/3.14159265);
            let Albedo=pow(textureSampleLevel(Atlas,AtlasSampler,UV,i32(M.Atlas.x),0.0).rgb,vec3f(M.Surface.w));
            // Authored phase is days since new; native reference's full-at-zero convention is converted here.
            let Wrap=M.Surface.z*0.3;
            let Lit=max(0.0,(dot(N,vec3f(sin(M.Params.z),0.0,-cos(M.Params.z)))+Wrap)/(1.0+Wrap));
            let Limb=mix(1.0,0.5,pow(1.0-Z,2.0)*max(M.Surface.z,0.35));
            let Edge=1.0-smoothstep(RadiusM*mix(0.975,0.88,M.Surface.z)-Footprint,RadiusM*(1.0+M.Surface.z*0.06)+Footprint,A);
            let Rim=pow(max(0.0,1.0-Z),3.0)*M.Surface.z*0.08;
            Result*=1.0-Edge; // Opaque lunar disc occludes background stars.
            Result+=(Albedo*(pow(Lit,0.8)*Limb+0.012)+Rim)*M.Params.y*Edge*M.Atlas.yzw;
        }
        let Corona=(exp(-A/(RadiusM*1.4))*0.09+exp(-A*3.5)*0.005)*M.Params.w;
        Result+=vec3f(0.7,0.8,1.0)*Corona*M.Params.y*(0.4+0.6*(0.5-0.5*cos(M.Params.z)))*M.Atlas.yzw;
    }
    if (Frame.CelestialControl.w>0.0 && Night>0.0) {
        let Tints=array<vec3f,4>(vec3f(1.0,0.95,0.8),vec3f(1.0,0.85,0.65),vec3f(1.0,0.45,0.22),vec3f(0.85,0.78,0.55));
        let Gains=array<f32,4>(2.5,1.5,0.7,0.5);
        for(var I=0u;I<4u;I++) {
            let AngleP=acos(clamp(dot(D,Frame.Planets[I].xyz),-1.0,1.0));
            let SizeP=max(Footprint,0.00012);
            Result+=Tints[I]*exp(-pow(AngleP/SizeP,2.0))*Gains[I]*Night*Frame.CelestialControl.w*smoothstep(0.0,0.15,Frame.Planets[I].z);
        }
    }
    return Result;
}
