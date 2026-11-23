//============================================================================================================================================
//                                                          VARIANCETEMPORAL.WGSL
//============================================================================================================================================
// 🧩 Temporal integration of a resolved signal with luminance moments, reprojected through motion and validated per tap.

// 📝 SVGF's temporal stage (Schied et al. 2017): the prior signal is gathered with four bilinear taps, each refused
//    unless the prior guide agrees in normal and depth, then mixed with α = max(1/N, MinimumWeight). Luminance
//    moments μ₁, μ₂ are integrated alongside; variance μ₂ − μ₁² is inflated while the tally is short.

//! include "FrameDescriptor.wgsl"
//! include "PackingCodec.wgsl"

struct VarianceRecord
{
    Step           : u32,   // [px]  wavelet step (unused here)
    MinimumWeight  : f32,   // [-]   floor of the temporal weight
    ColourSigma    : f32,   // [-]   luminance edge-stopping scale (unused here)
    TallyLimit     : f32,   // [-]   longest tally N
}

@group(0) @binding(1) var<uniform> Variance  : VarianceRecord;
@group(0) @binding(2) var CurrentSignal      : texture_2d<f32>;
@group(0) @binding(3) var SurfaceMotion      : texture_2d<f32>;
@group(0) @binding(4) var CurrentGuide       : texture_2d<f32>;
@group(0) @binding(5) var PreviousGuide      : texture_2d<f32>;
@group(0) @binding(6) var PriorSignal        : texture_2d<f32>;
@group(0) @binding(7) var PriorMoments       : texture_2d<f32>;
@group(0) @binding(8) var IntegratedTarget   : texture_storage_2d<rgba16float, write>;
@group(0) @binding(9) var MomentsTarget      : texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn IntegrateTemporal(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = vec2i(Invocation.xy);
    let Extent = vec2i(textureDimensions(CurrentSignal));
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Guide = textureLoad(CurrentGuide, TracePixel, 0);
    let Signal = textureLoad(CurrentSignal, TracePixel, 0).rgb;
    let Luminance = ResolveLuminance(Signal);
    if (Guide.w < 0.5)
    {
        textureStore(IntegratedTarget, TracePixel, vec4f(0.0));
        textureStore(MomentsTarget, TracePixel, vec4f(0.0));
        return;
    }
    let Normal = DecodeOctahedral(Guide.xy);
    let ScreenPixel = min(TracePixel * i32(Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
    let Motion = textureLoad(SurfaceMotion, ScreenPixel, 0).xy;
    let PriorPosition = (vec2f(ScreenPixel) + 0.5 - Motion) / f32(Frame.TraceScale) - 0.5;
    let Corner = floor(PriorPosition);
    let Fraction = PriorPosition - Corner;

    var PriorColour  = vec3f(0.0);
    var PriorMoment  = vec3f(0.0);
    var TotalWeight  = 0.0;
    for (var Tap = 0u; Tap < 4u; Tap++)
    {
        let Offset = vec2i(i32(Tap & 1u), i32(Tap >> 1u));
        let Texel = vec2i(Corner) + Offset;
        if (any(Texel < vec2i(0)) || any(Texel >= Extent))
        {
            continue;
        }
        let PriorGuide = textureLoad(PreviousGuide, Texel, 0);
        if (PriorGuide.w < 0.5 || dot(DecodeOctahedral(PriorGuide.xy), Normal) < 0.85
            || !ClassifyPriorDepth(PriorGuide.z, Guide.w - 1.0, Guide.z))
        {
            continue;
        }
        let Weight = select(1.0 - Fraction.x, Fraction.x, Offset.x == 1) * select(1.0 - Fraction.y, Fraction.y, Offset.y == 1);
        PriorColour += textureLoad(PriorSignal, Texel, 0).rgb * Weight;
        PriorMoment += textureLoad(PriorMoments, Texel, 0).xyz * Weight;
        TotalWeight += Weight;
    }

    var Tally = 1.0;
    var Colour = Signal;
    var Moments = vec2f(Luminance, Luminance * Luminance);
    if (TotalWeight > 0.01 && !FeatureEnabled(Frame, FeatureTemporalRestart))
    {
        PriorColour /= TotalWeight;
        PriorMoment /= TotalWeight;
        Tally = min(PriorMoment.z + 1.0, Variance.TallyLimit);
        let Fraction = max(1.0 / Tally, Variance.MinimumWeight);
        Colour  = mix(PriorColour, Signal, Fraction);
        Moments = mix(PriorMoment.xy, Moments, max(Fraction, 0.2));
    }
    var Spread = max(Moments.y - Moments.x * Moments.x, 0.0);
    Spread *= 4.0 / min(Tally, 4.0);
    textureStore(IntegratedTarget, TracePixel, vec4f(Colour, Spread));
    textureStore(MomentsTarget, TracePixel, vec4f(Moments, Tally, 0.0));
}
