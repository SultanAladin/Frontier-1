//============================================================================================================================================
//                                                           DIRECTRADIANCE.WGSL
//============================================================================================================================================
// 🧩 Direct sun radiance, tyre-fire and light-grid arrivals at every primary surface, cascaded shadow depth, and sky background.

//! include "FrameDescriptor.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "SkyProjection.wgsl"
//! include "FireIllumination.wgsl"
//! include "DistanceTraversal.wgsl"
//! include "LightClusterSpace.wgsl"

struct CascadeReceiver
{
    LightViewProjection : array<mat4x4f, 3>,
    Splits              : vec4f,   // [m]   xyz far distance of each cascade, w cascade count
    TexelWorld          : vec4f,   // [m]   world size of one shadow texel per cascade
}

@group(0) @binding(1) var<storage, read> Materials     : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes            : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth                 : texture_depth_2d;
@group(0) @binding(4) var ShadowDepth                  : texture_depth_2d_array;
@group(0) @binding(5) var ShadowSampler                : sampler_comparison;
@group(0) @binding(6) var<uniform> Receiver            : CascadeReceiver;
@group(0) @binding(7) var SkyView                      : texture_2d<f32>;
@group(0) @binding(8) var SkySampler                   : sampler;
@group(0) @binding(9) var DirectTarget                 : texture_storage_2d<rgba16float, write>;   // rgb radiance, w sun visibility
@group(0) @binding(10) var<storage, read> SceneLights   : LightClusterRecords;
@group(0) @binding(11) var DirectIrradianceTarget      : texture_storage_2d<rgba16float, write>;   // rgb diffuse irradiance of fire and lights

// 📐 3×3 percentage-closer taps, each a hardware bilinear comparison, after a normal offset of 1.5 texels.
fn ResolveCascadeVisibility(Position: vec3f, Normal: vec3f) -> f32
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = u32(Receiver.Splits.w);
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    if (ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    let Offset = Position + Normal * (1.5 * Receiver.TexelWorld[Cascade]);
    let Clip = Receiver.LightViewProjection[Cascade] * vec4f(Offset, 1.0);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < vec2f(0.0)) || any(Coordinates > vec2f(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    let Texel = 1.0 / vec2f(textureDimensions(ShadowDepth).xy);
    var Lit = 0.0;
    for (var Y = -1; Y <= 1; Y++)
    {
        for (var X = -1; X <= 1; X++)
        {
            Lit += textureSampleCompareLevel(ShadowDepth, ShadowSampler, Coordinates + vec2f(f32(X), f32(Y)) * Texel, Cascade, Ndc.z);
        }
    }
    return Lit / 9.0;
}

fn ResolveBackgroundRadiance(Direction: vec3f) -> vec3f
{
    var Radiance = textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(Direction), 0.0).rgb * Frame.SkyScale
                 + Frame.NightSkyRadiance;
    // 📝 The solar disc (0.27° radius) is added only here, never to escaped rays, so it is not counted twice.
    let Cosine = dot(Direction, Frame.SunDirection);
    if (Cosine > 0.99998)
    {
        Radiance += Frame.SunIlluminance * 2500.0;
    }
    return Radiance;
}

@compute @workgroup_size(8, 8)
fn IntegrateDirectRadiance(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = Invocation.xy;
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    let Depth = textureLoad(SurfaceDepth, vec2i(Pixel), 0);
    let Centre = vec2f(Pixel) + 0.5;
    if (Depth <= 0.0)
    {
        let Far = ReconstructWorldPosition(Centre, 1.0e-6);
        textureStore(DirectTarget, vec2i(Pixel), vec4f(ResolveBackgroundRadiance(normalize(Far - Frame.CameraPosition)), 0.0));
        textureStore(DirectIrradianceTarget, vec2i(Pixel), vec4f(0.0));
        return;
    }
    let Position   = ReconstructWorldPosition(Centre, Depth);
    let Attributes = textureLoad(SurfaceAttributes, vec2i(Pixel), 0);
    let Normal     = DecodeOctahedral(Attributes.xy);
    let Surface    = ProjectSurfaceSample(Materials[u32(Attributes.z + 0.5)]);
    let View       = normalize(Frame.CameraPosition - Position);
    var Visibility = 1.0;
    if (FeatureEnabled(Frame, FeatureDirectShadow))
    {
        Visibility = ResolveCascadeVisibility(Position, Normal);
    }
    var Radiance = Frame.SunIlluminance * Visibility * ResolveDirectReflectance(Surface, Normal, View, Frame.SunDirection);

    // 📝 Irradiance here is diffuse (cosine-weighted, unscaled by the albedo): the display's screen exitance and the cards
    //    both need it, and the radiance above carries only the specular-inclusive response for direct shading.
    var Irradiance = vec3f(0.0);
    let Fire = ResolveFireArrival(Position, Normal);
    if (any(Fire.Illuminance > vec3f(0.0)))
    {
        let FireCosine = max(dot(Normal, Fire.Direction), 0.0);
        Radiance += Fire.Illuminance * ResolveDirectReflectance(Surface, Normal, View, Fire.Direction);
        Irradiance += Fire.Illuminance * FireCosine;
    }

    // 📝 Only the lights whose reach touches this point's grid cell are evaluated (LightClusterSpace.wgsl).
    var Candidates = ResolveLightCandidates(Position);
    while (Candidates != 0u)
    {
        let Index = firstTrailingBit(Candidates);
        Candidates &= Candidates - 1u;
        let Light = SceneLights.Lights[Index];
        let Illuminance = EvaluateLightArrival(Light, Position, Normal);
        if (any(Illuminance > vec3f(0.0)))
        {
            let Toward = Light.PositionType.xyz - Position;
            var Shade = 1.0;
            if (FeatureEnabled(Frame, FeatureDirectShadow))
            {
                let Bias = ResolveSurfaceBias(Position);
                Shade = TraverseSceneShadow(Position + Normal * Bias, normalize(Toward), 0.5 * Bias, max(length(Toward) - 0.5, 0.0), 4.0);
            }
            let Lit = Illuminance * Shade;
            Irradiance += Lit;
            Radiance += Lit * ResolveDirectReflectance(Surface, Normal, View, normalize(Toward));
        }
    }

    textureStore(DirectTarget, vec2i(Pixel), vec4f(Radiance, Visibility));
    textureStore(DirectIrradianceTarget, vec2i(Pixel), vec4f(Irradiance, 1.0));
}
