//============================================================================================================================================
//                                                           DISPLAYEXPOSURE.WGSL
//============================================================================================================================================
// 🧩 Eye adaptation: log-average luminance of a 64×64 lattice of the frame, approached exponentially by the exposure.

//! include "FrameDescriptor.wgsl"

@group(0) @binding(1) var ResolvedRadiance                 : texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> Adaptation  : array<f32, 4>;

var<workgroup> LaneSums : array<f32, 256>;

@compute @workgroup_size(256)
fn IntegrateExposure(@builtin(local_invocation_index) Lane: u32)
{
    var Sum = 0.0;
    for (var Sample = 0u; Sample < 16u; Sample++)
    {
        let Index = Lane * 16u + Sample;
        let Lattice = vec2f(f32(Index % 64u) + 0.5, f32(Index / 64u) + 0.5) / 64.0;
        let Pixel = vec2i(Lattice * Frame.ScreenExtent);
        let Luminance = ResolveLuminance(textureLoad(ResolvedRadiance, Pixel, 0).rgb);
        Sum += log2(max(Luminance, 1.0e-5));
    }
    LaneSums[Lane] = Sum;
    workgroupBarrier();
    for (var Stride = 128u; Stride > 0u; Stride >>= 1u)
    {
        if (Lane < Stride)
        {
            LaneSums[Lane] += LaneSums[Lane + Stride];
        }
        workgroupBarrier();
    }
    if (Lane == 0u)
    {
        let Logarithm = LaneSums[0] / 4096.0;
        let Average = exp2(Logarithm);
        // 📐 Middle grey 0.18 at the log-average luminance — but below a floor of 0.1 the eye adapts only partially
        //    (two thirds of each further stop), so a moonlit course stays a night scene with headlights in it rather
        //    than being lifted to daylight.
        let Floor = log2(0.1);
        let Adapted = select(Logarithm, Floor + (Logarithm - Floor) * 0.66, Logarithm < Floor);
        let Target = clamp(0.18 / exp2(Adapted), 0.02, 60.0) * exp2(Frame.ExposureCompensation);
        let Prior = Adaptation[0];
        var Exposure = Target;
        if (Prior > 0.0 && Frame.FrameIndex > 2u)
        {
            Exposure = exp2(mix(log2(Prior), log2(Target), 0.06));
        }
        if (!FeatureEnabled(Frame, FeatureAutoExposure))
        {
            Exposure = 0.55 * exp2(Frame.ExposureCompensation);
        }
        Adaptation[0] = Exposure;
        Adaptation[1] = Average;
    }
}
