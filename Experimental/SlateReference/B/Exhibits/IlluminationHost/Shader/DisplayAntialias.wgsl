//============================================================================================================================================
//                                                          DISPLAYANTIALIAS.WGSL
//============================================================================================================================================
// 🧩 Temporal antialiasing: motion-reprojected prior colour, clamped to the surrounding 3×3 YCoCg extent.

//! include "FrameDescriptor.wgsl"

@group(0) @binding(1) var RecombinedRadiance : texture_2d<f32>;
@group(0) @binding(2) var PriorRadiance     : texture_2d<f32>;
@group(0) @binding(3) var SurfaceMotion     : texture_2d<f32>;
@group(0) @binding(4) var LinearClamp       : sampler;
@group(0) @binding(5) var AntialiasTarget   : texture_storage_2d<rgba16float, write>;

fn ProjectCoCg(Colour: vec3f) -> vec3f
{
    return vec3f(0.25 * Colour.r + 0.5 * Colour.g + 0.25 * Colour.b, 0.5 * Colour.r - 0.5 * Colour.b,
                 -0.25 * Colour.r + 0.5 * Colour.g - 0.25 * Colour.b);
}

fn ReconstructRgb(CoCg: vec3f) -> vec3f
{
    return vec3f(CoCg.x + CoCg.y - CoCg.z, CoCg.x + CoCg.z, CoCg.x - CoCg.y - CoCg.z);
}

// 📝 Range compression keeps a single bright texel from dominating the clamp box (Karis 2014).
fn CompressRange(Colour: vec3f) -> vec3f
{
    return Colour / (1.0 + ResolveLuminance(Colour));
}

fn ExpandRange(Colour: vec3f) -> vec3f
{
    return Colour / max(1.0 - ResolveLuminance(Colour), 1.0e-4);
}

@compute @workgroup_size(8, 8)
fn IntegrateAntialias(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = vec2i(Invocation.xy);
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    let Current = CompressRange(textureLoad(RecombinedRadiance, Pixel, 0).rgb);
    if (!FeatureEnabled(Frame, FeatureTemporalAntialias) || Frame.InspectionMode == 6u || Frame.InspectionMode == 7u)
    {
        textureStore(AntialiasTarget, Pixel, vec4f(ExpandRange(Current), 1.0));
        return;
    }
    var Minimum = vec3f(1.0e9);
    var Maximum = vec3f(-1.0e9);
    var Mean = vec3f(0.0);
    var Square = vec3f(0.0);
    let Limit = vec2i(Frame.ScreenExtent) - vec2i(1);
    for (var Y = -1; Y <= 1; Y++)
    {
        for (var X = -1; X <= 1; X++)
        {
            let Tap = ProjectCoCg(CompressRange(textureLoad(RecombinedRadiance, clamp(Pixel + vec2i(X, Y), vec2i(0), Limit), 0).rgb));
            Minimum = min(Minimum, Tap);
            Maximum = max(Maximum, Tap);
            Mean += Tap;
            Square += Tap * Tap;
        }
    }
    Mean /= 9.0;
    let Deviation = sqrt(max(Square / 9.0 - Mean * Mean, vec3f(0.0)));
    Minimum = max(Minimum, Mean - 1.25 * Deviation);
    Maximum = min(Maximum, Mean + 1.25 * Deviation);

    let Motion = textureLoad(SurfaceMotion, Pixel, 0).xy;
    let PriorCoordinates = (vec2f(Pixel) + 0.5 - Motion) * Frame.InverseScreenExtent;
    var Fraction = 0.1;
    var Prior = Current;
    if (any(PriorCoordinates < vec2f(0.0)) || any(PriorCoordinates > vec2f(1.0)) || Frame.FrameIndex < 2u
        || FeatureEnabled(Frame, FeatureTemporalRestart))
    {
        Fraction = 1.0;
    }
    else
    {
        Prior = CompressRange(textureSampleLevel(PriorRadiance, LinearClamp, PriorCoordinates, 0.0).rgb);
        Prior = ReconstructRgb(clamp(ProjectCoCg(Prior), Minimum, Maximum));
    }
    let Resolved = mix(Prior, Current, Fraction);
    textureStore(AntialiasTarget, Pixel, vec4f(ExpandRange(Resolved), 1.0));
}
