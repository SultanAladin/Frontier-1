//============================================================================================================================================
//                                                           VARIANCEWAVELET.WGSL
//============================================================================================================================================
// 🧩 One à-trous wavelet iteration: a 5×5 B3-spline kernel at step 2^i, stopped at depth, normal and luminance edges.

// 📐 w(p, q) = h(q) · exp(−|z_p − z_q| / (σ_z·step·z_p)) · max(0, n_p·n_q)^σ_n · exp(−|l_p − l_q| / (σ_l·√Var_p + ε)),
//    h the separable B3 spline (1, 4, 6, 4, 1)/16. Variance is propagated as Σw²·Var / (Σw)² (Dammertz et al. 2010,
//    Schied et al. 2017). Signal alpha carries the variance between iterations.

//! include "FrameDescriptor.wgsl"
//! include "PackingCodec.wgsl"

struct VarianceRecord
{
    Step         : u32,   // [px]
    MinimumWeight : f32,   // [-]  (unused here)
    ColourSigma  : f32,   // [-]
    TallyLimit : f32,   // [-]  (unused here)
}

@group(0) @binding(1) var<uniform> Variance : VarianceRecord;
@group(0) @binding(2) var SourceSignal      : texture_2d<f32>;
@group(0) @binding(3) var CurrentGuide      : texture_2d<f32>;
@group(0) @binding(4) var WaveletTarget     : texture_storage_2d<rgba16float, write>;

const Spline : array<f32, 3> = array<f32, 3>(0.375, 0.25, 0.0625);

@compute @workgroup_size(8, 8)
fn ConvolveWavelet(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = vec2i(Invocation.xy);
    let Extent = vec2i(textureDimensions(SourceSignal));
    if (any(Pixel >= Extent))
    {
        return;
    }
    let Guide = textureLoad(CurrentGuide, Pixel, 0);
    let Centre = textureLoad(SourceSignal, Pixel, 0);
    if (Guide.w < 0.5)
    {
        textureStore(WaveletTarget, Pixel, Centre);
        return;
    }
    let Normal = DecodeOctahedral(Guide.xy);
    let Luminance = ResolveLuminance(Centre.rgb);
    let Deviation = sqrt(max(Centre.a, 0.0));
    let Step = i32(Variance.Step);
    var Sum = vec4f(0.0);
    var WeightSum = 0.0;
    var SpreadSum = 0.0;
    for (var Y = -2; Y <= 2; Y++)
    {
        for (var X = -2; X <= 2; X++)
        {
            let Texel = Pixel + vec2i(X, Y) * Step;
            if (any(Texel < vec2i(0)) || any(Texel >= Extent))
            {
                continue;
            }
            let TapGuide = textureLoad(CurrentGuide, Texel, 0);
            if (TapGuide.w < 0.5)
            {
                continue;
            }
            let Tap = textureLoad(SourceSignal, Texel, 0);
            let Kernel = Spline[abs(X)] * Spline[abs(Y)];
            let DepthWeight = exp(-abs(TapGuide.z - Guide.z) / (0.015 * f32(Step) * Guide.z + 1.0e-3));
            let NormalWeight = pow(max(dot(DecodeOctahedral(TapGuide.xy), Normal), 0.0), 64.0);
            let LuminanceWeight = exp(-abs(ResolveLuminance(Tap.rgb) - Luminance) / (Variance.ColourSigma * Deviation + 1.0e-3));
            let Weight = Kernel * DepthWeight * NormalWeight * LuminanceWeight;
            Sum += vec4f(Tap.rgb * Weight, 0.0);
            SpreadSum += Weight * Weight * Tap.a;
            WeightSum += Weight;
        }
    }
    if (WeightSum <= 1.0e-6)
    {
        textureStore(WaveletTarget, Pixel, Centre);
        return;
    }
    textureStore(WaveletTarget, Pixel, vec4f(Sum.rgb / WeightSum, SpreadSum / (WeightSum * WeightSum)));
}
