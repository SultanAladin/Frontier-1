struct AreaLight { centerRange: vec4<f32>, rightHalfWidth: vec4<f32>, upHalfHeight: vec4<f32>, radianceProfile: vec4<f32> };
struct RaySettings { pointCount: u32, rayCount: u32, shadowEnabled: u32, padding: u32 };
@group(0) @binding(0) var<storage, read> points: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read> normals: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> lights: array<AreaLight>;
@group(0) @binding(3) var<storage, read_write> directOut: array<vec4<f32>>;
@group(0) @binding(4) var<uniform> settings: RaySettings;
@group(0) @binding(5) var<storage, read> wideEntries: array<u32>;
@group(0) @binding(6) var<storage, read> trianglePositions: array<f32>;
@group(0) @binding(7) var<storage, read> iesProfiles: array<f32>;
fn surfaceValue(origin: vec3<f32>, direction: vec3<f32>, triangle: u32) -> f32 {
    let base = triangle * 9u;
    let a = vec3<f32>(trianglePositions[base], trianglePositions[base+1u], trianglePositions[base+2u]);
    let b = vec3<f32>(trianglePositions[base+3u], trianglePositions[base+4u], trianglePositions[base+5u]);
    let c = vec3<f32>(trianglePositions[base+6u], trianglePositions[base+7u], trianglePositions[base+8u]);
    let edgeOne = b-a; let edgeTwo = c-a; let p = cross(direction,edgeTwo); let determinant = dot(edgeOne,p);
    if(abs(determinant)<1.0e-8){return -1.0;}let invDet=1.0/determinant;let tVec=origin-a;let u=dot(tVec,p)*invDet;if(u<0.0||u>1.0){return -1.0;}let q=cross(tVec,edgeOne);let v=dot(direction,q)*invDet;if(v<0.0||u+v>1.0){return -1.0;}let distance=dot(edgeTwo,q)*invDet;return select(-1.0,distance,distance>1.0e-4);
}
fn traceBvh4Any(origin: vec3<f32>, direction: vec3<f32>, maxDistance: f32) -> bool {
    if(arrayLength(&wideEntries)<16u){return false;}
    var stack: array<u32,32>;var top: i32=0;stack[0]=0u;
    loop {if(top<0){break;}let entryIndex=stack[u32(top)];top=top-1;let base=entryIndex*16u;let originBox=vec3<f32>(bitcast<f32>(wideEntries[base]),bitcast<f32>(wideEntries[base+1u]),bitcast<f32>(wideEntries[base+2u]));let exponents=wideEntries[base+3u];let scale=vec3<f32>(exp2(f32(i32(exponents&255u)-127)),exp2(f32(i32((exponents>>8u)&255u)-127)),exp2(f32(i32((exponents>>16u)&255u)-127)));
        for(var lane=0u;lane<4u;lane=lane+1u){let link=wideEntries[base+12u+lane];if(link==0xffffffffu){continue;}var nearT=0.0;var farT=maxDistance;var visible=true;for(var axis=0u;axis<3u;axis=axis+1u){let qLo=(wideEntries[base+4u+axis*2u]>>(lane*8u))&255u;let qHi=(wideEntries[base+5u+axis*2u]>>(lane*8u))&255u;let lower=originBox[axis]+f32(qLo)*scale[axis]/255.0;let upper=originBox[axis]+f32(qHi)*scale[axis]/255.0;if(abs(direction[axis])<1.0e-8){if(origin[axis]<lower||origin[axis]>upper){visible=false;}}else{let ta=(lower-origin[axis])/direction[axis];let tb=(upper-origin[axis])/direction[axis];nearT=max(nearT,min(ta,tb));farT=min(farT,max(ta,tb));if(farT<nearT){visible=false;}}}if(!visible){continue;}if((link&0x80000000u)!=0u){let first=link&0x7fffffffu;let packed=wideEntries[base+10u];let count=(packed>>(lane*8u))&255u;for(var p=0u;p<count;p=p+1u){if(surfaceValue(origin,direction,first+p)>0.0&&surfaceValue(origin,direction,first+p)<maxDistance){return true;}}}else if(top<30){top=top+1;stack[u32(top)]=link;}}
    }
    return false;
}
fn evaluateClippedStokesFormFactor(origin: vec3<f32>, normal: vec3<f32>, p0: vec3<f32>, p1: vec3<f32>, p2: vec3<f32>, p3: vec3<f32>) -> f32 {
    var source: array<vec3<f32>,4>;source[0]=p0-origin;source[1]=p1-origin;source[2]=p2-origin;source[3]=p3-origin;var clipped: array<vec3<f32>,8>;var count=0u;
    for(var i=0u;i<4u;i=i+1u){let a=source[i];let b=source[(i+1u)%4u];let da=dot(normal,a);let db=dot(normal,b);if(da>=0.0){clipped[count]=a;count=count+1u;}if((da<0.0&&db>0.0)||(da>0.0&&db<0.0)){let t=da/(da-db);clipped[count]=mix(a,b,t);count=count+1u;}}
    if(count<3u){return 0.0;}var contour=0.0;for(var i=0u;i<count;i=i+1u){let a=normalize(clipped[i]);let b=normalize(clipped[(i+1u)%count]);let edge=cross(a,b);let edgeLength=length(edge);if(edgeLength>1.0e-7){let angle=atan2(edgeLength,clamp(dot(a,b),-1.0,1.0));contour+=dot(normal,edge)*(angle/edgeLength);}}return max(contour/(2.0*3.14159265),0.0);
}
fn evaluateLtcGloss(normal: vec3<f32>, viewDirection: vec3<f32>, lightDirection: vec3<f32>, roughness: f32) -> f32 {let halfVector=normalize(viewDirection+lightDirection);let exponent=mix(128.0,3.0,clamp(roughness,0.02,1.0));return pow(max(dot(normal,halfVector),0.0),exponent)*max(dot(normal,lightDirection),0.0);}
fn profileGain(profile: u32, zeroAxis: vec3<f32>, upAxis: vec3<f32>, beamAxis: vec3<f32>, towardPoint: vec3<f32>) -> f32 {let base=profile*4096u;if(base+4096u>arrayLength(&iesProfiles)){return 1.0;}let theta=acos(clamp(dot(beamAxis,towardPoint),-1.0,1.0));let azimuth=atan2(dot(towardPoint,upAxis),dot(towardPoint,zeroAxis));let v=u32(clamp(round(theta*127.0/3.14159265),0.0,127.0));let h=u32((i32(round((azimuth+3.14159265)*32.0/6.28318530))+32)%32);return iesProfiles[base+h*128u+v];}
fn blockerVisibility(origin: vec3<f32>, target: vec3<f32>, pointOrdinal: u32, lightOrdinal: u32) -> f32 {let delta=target-origin;let distance=length(delta);if(distance<1.0e-4){return 1.0;}let direction=delta/distance;let offset=direction*1.0e-3;let hidden=traceBvh4Any(origin+offset,direction,distance-2.0e-3);return select(1.0,0.0,hidden);}
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let index=gid.x;if(index>=settings.pointCount){return;}let origin=points[index].xyz;let normal=normalize(normals[index].xyz);var sum=vec3<f32>(0.0);
    for(var lightIndex=0u;lightIndex<arrayLength(&lights);lightIndex=lightIndex+1u){let light=lights[lightIndex];let area=4.0*light.rightHalfWidth.w*light.upHalfHeight.w;let lightNormal=normalize(cross(light.rightHalfWidth.xyz,light.upHalfHeight.xyz));let twoSided=dot(lightNormal,origin-light.centerRange.xyz)>0.0;let emitCosine=select(max(dot(lightNormal,origin-light.centerRange.xyz),0.0),abs(dot(lightNormal,origin-light.centerRange.xyz)),twoSided);if(emitCosine<=0.0&&area>1.0e-5){continue;}
        var visibility=1.0;let rayCount=clamp(settings.rayCount,1u,16u);if(settings.shadowEnabled!=0u){var lit=0.0;for(var ray=0u;ray<rayCount;ray=ray+1u){let angle=f32(ray)*2.39996323;let radius=sqrt((f32(ray)+0.5)/f32(rayCount));let target=light.centerRange.xyz+light.rightHalfWidth.xyz*(cos(angle)*radius*light.rightHalfWidth.w)+light.upHalfHeight.xyz*(sin(angle)*radius*light.upHalfHeight.w);lit+=blockerVisibility(origin,target,index,lightIndex);}visibility=lit/f32(rayCount);}
        if(area>1.0e-5){let p0=light.centerRange.xyz-light.rightHalfWidth.xyz*light.rightHalfWidth.w-light.upHalfHeight.xyz*light.upHalfHeight.w;let p1=light.centerRange.xyz+light.rightHalfWidth.xyz*light.rightHalfWidth.w-light.upHalfHeight.xyz*light.upHalfHeight.w;let p2=light.centerRange.xyz+light.rightHalfWidth.xyz*light.rightHalfWidth.w+light.upHalfHeight.xyz*light.upHalfHeight.w;let p3=light.centerRange.xyz-light.rightHalfWidth.xyz*light.rightHalfWidth.w+light.upHalfHeight.xyz*light.upHalfHeight.w;let form=evaluateClippedStokesFormFactor(origin,normal,p0,p1,p2,p3);let toLight=normalize(light.centerRange.xyz-origin);let gloss=evaluateLtcGloss(normal,vec3<f32>(0.0,0.0,1.0),toLight,0.3);sum+=light.radianceProfile.rgb*(form+gloss*0.035)*visibility*settings.lightPower;}
        else{let toLight=light.centerRange.xyz-origin;let distance=length(toLight);if(distance<light.centerRange.w){let direction=toLight/max(distance,1.0e-4);let beam=normalize(cross(light.rightHalfWidth.xyz,light.upHalfHeight.xyz));let profile=profileGain(u32(max(light.radianceProfile.w,0.0)),light.rightHalfWidth.xyz,light.upHalfHeight.xyz,beam,-direction);let falloff=pow(max(1.0-distance/light.centerRange.w,0.0),2.0)/max(distance*distance,0.025);let cosine=max(dot(normal,direction),0.0);sum+=light.radianceProfile.rgb*profile*falloff*cosine*visibility*settings.lightPower;}}
    }
    directOut[index]=vec4<f32>(sum,1.0);
}
