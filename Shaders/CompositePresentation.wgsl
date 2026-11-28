struct ViewUniforms { width: f32, height: f32, exposure: f32, diagnostic: u32, sunDirection: vec4<f32>, sunRadiance: vec4<f32>, sceneOptions: vec4<f32>, cameraOrigin: vec4<f32> };
struct SurfelLight { originArea: vec4<f32>, normal: vec4<f32>, albedo: vec4<f32>, directRadiance: vec4<f32> };
@group(0) @binding(0) var baseTexture: texture_2d<f32>;
@group(0) @binding(1) var normalTexture: texture_2d<f32>;
@group(0) @binding(2) var positionTexture: texture_2d<f32>;
@group(0) @binding(3) var glowTexture: texture_2d<f32>;
@group(0) @binding(4) var linearSampler: sampler;
@group(0) @binding(5) var<uniform> settings: ViewUniforms;
@group(1) @binding(0) var<storage, read> surfels: array<SurfelLight>;
@group(1) @binding(1) var<storage, read> bouncedRadiance: array<vec4<f32>>;
struct FullscreenOutput { @builtin(position) clipPosition: vec4<f32>, @location(0) uv: vec2<f32> };
@vertex fn vertexMain(@builtin(vertex_index) index: u32) -> FullscreenOutput {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));
    var output: FullscreenOutput;let p=positions[index];output.clipPosition=vec4<f32>(p,0.0,1.0);output.uv=vec2<f32>((p.x+1.0)*0.5,1.0-(p.y+1.0)*0.5);return output;
}
fn filmic(x: vec3<f32>) -> vec3<f32> { let a=2.51;let b=0.03;let c=2.43;let d=0.59;let e=0.14;return clamp((x*(a*x+b))/(x*(c*x+d)+e),vec3<f32>(0.0),vec3<f32>(1.0)); }
fn fresnelSchlick(cosine: f32, f0: vec3<f32>) -> vec3<f32> { return f0+(vec3<f32>(1.0)-f0)*pow(1.0-clamp(cosine,0.0,1.0),5.0); }
fn ggxDistribution(nDotH: f32, roughness: f32) -> f32 { let alpha=max(roughness*roughness,0.002);let alphaSquared=alpha*alpha;let denominator=nDotH*nDotH*(alphaSquared-1.0)+1.0;return alphaSquared/(3.14159265*denominator*denominator); }
fn smithMasking(nDotV: f32, nDotL: f32, roughness: f32) -> f32 { let k=(roughness+1.0)*(roughness+1.0)/8.0;let view=nDotV/(nDotV*(1.0-k)+k);let light=nDotL/(nDotL*(1.0-k)+k);return view*light; }
@fragment fn fragmentMain(input: FullscreenOutput) -> @location(0) vec4<f32> {
    let base=textureSample(baseTexture,linearSampler,input.uv);let normalSample=textureSample(normalTexture,linearSampler,input.uv);let normal=normalize(normalSample.xyz);let positionSample=textureSample(positionTexture,linearSampler,input.uv);let position=positionSample.xyz;let glowSample=textureSample(glowTexture,linearSampler,input.uv);let glow=glowSample.rgb;let barycentric=vec3<f32>(positionSample.w,glowSample.a,1.0-positionSample.w-glowSample.a);let wireEdge=min(barycentric.x,min(barycentric.y,barycentric.z));
    var indirect=vec3<f32>(0.0);var directField=vec3<f32>(0.0);var totalWeight=0.0;let count=min(arrayLength(&surfels),32u);let pixelSeed=u32(input.clipPosition.x*17.0+input.clipPosition.y*31.0);
    for(var k=0u;k<count;k=k+1u){let index=(pixelSeed+k*97u)%max(arrayLength(&surfels),1u);let source=surfels[index];let delta=source.originArea.xyz-position;let distance=length(delta);let planeDistance=abs(dot(delta,normal));let orientation=max(dot(normal,source.normal.xyz),0.0);let weight=exp(-distance*1.45)*exp(-planeDistance*14.0)*pow(orientation,3.0);indirect+=bouncedRadiance[index].xyz*weight;directField+=bouncedRadiance[index].xyz*weight;totalWeight+=weight;}
    if(totalWeight>1.0e-5){indirect/=totalWeight;directField/=totalWeight;}
    let lightDirection=normalize(settings.sunDirection.xyz);let viewDirection=normalize(settings.cameraOrigin.xyz-position);let halfDirection=normalize(lightDirection+viewDirection);let nDotL=max(dot(normal,lightDirection),0.0);let nDotV=max(dot(normal,viewDirection),0.001);let nDotH=max(dot(normal,halfDirection),0.0);let vDotH=max(dot(viewDirection,halfDirection),0.0);let metallic=clamp(base.a,0.0,1.0);let roughness=clamp(normalSample.a,0.035,1.0);let f0=mix(vec3<f32>(0.04),base.rgb,metallic);let fresnel=fresnelSchlick(vDotH,f0);let distribution=ggxDistribution(nDotH,roughness);let geometry=smithMasking(nDotV,nDotL,roughness);let specular=fresnel*(distribution*geometry/max(4.0*nDotV*max(nDotL,0.001),0.001));let diffuse=base.rgb*(vec3<f32>(1.0)-fresnel)*(1.0-metallic)/3.14159265;
    let clearcoatFresnel=0.04+0.96*pow(1.0-vDotH,5.0);let clearcoat=vec3<f32>(clearcoatFresnel*ggxDistribution(nDotH,0.16)*smithMasking(nDotV,nDotL,0.16)*0.018);
    let direct=(diffuse*nDotL+specular*nDotL+clearcoat*nDotL)*settings.sunRadiance.xyz;var color=base.rgb*(vec3<f32>(0.09)+indirect*settings.sceneOptions.z)+direct*settings.sceneOptions.w+glow*settings.sceneOptions.w;
    if(settings.diagnostic==1u){color=indirect*3.0+glow;}if(settings.diagnostic==2u){color=indirect*3.0;}if(settings.diagnostic==3u){color=glow+directField*0.4;}if(settings.diagnostic==4u){color=directField*0.7+specular*settings.sunRadiance.xyz;}if(settings.diagnostic==5u){let lineWidth=max(fwidth(wireEdge),0.0012);let wire=1.0-smoothstep(lineWidth,lineWidth*2.2,wireEdge);color=mix(normal*0.5+0.5+clearcoat*0.2,vec3<f32>(0.015,0.017,0.019),wire);}if(settings.diagnostic==6u){color=base.rgb*(0.1+max(nDotL,0.0));}
    color=filmic(color*settings.exposure);color=pow(color,vec3<f32>(1.0/2.2));return vec4<f32>(color,1.0);
}
