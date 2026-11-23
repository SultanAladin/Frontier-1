//============================================================================================================================================
//                                                          VARIANCEUPSAMPLE.WGSL
//============================================================================================================================================
// 🧩 Joint bilateral projection of a resampling-resolution signal onto every screen pixel, guided by depth and normal.

//! include "FrameDescriptor.wgsl"
//! include "PackingCodec.wgsl"

@group(0) @binding(1) var SourceSignal      : texture_2d<f32>;
@group(0) @binding(2) var SourceGuide       : texture_2d<f32>;
@group(0) @binding(3) var SurfaceAttributes : texture_2d<f32>;
@group(0) @binding(4) var SurfaceDepth      : texture_depth_2d;
@group(0) @binding(5) var ScreenTarget      : texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn ProjectScreenSignal(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = vec2i(Invocation.xy);
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    let Depth = textureLoad(SurfaceDepth, Pixel, 0);
    if (Depth <= 0.0)
    {
        textureStore(ScreenTarget, Pixel, vec4f(0.0));
        return;
    }
    let ViewDepth = ResolveViewDepth(ReconstructWorldPosition(vec2f(Pixel) + 0.5, Depth));
    let Normal = DecodeOctahedral(textureLoad(SurfaceAttributes, Pixel, 0).xy);
    let Extent = vec2i(textureDimensions(SourceSignal));
    let Position = vec2f(Pixel) / f32(Frame.TraceScale);
    let Corner = vec2i(floor(Position));
    let Fraction = Position - floor(Position);
    var Sum = vec3f(0.0);
    var WeightSum = 0.0;
    var Nearest = vec3f(0.0);
    var NearestScore = 1.0e30;
    for (var Tap = 0u; Tap < 4u; Tap++)
    {
        let Offset = vec2i(i32(Tap & 1u), i32(Tap >> 1u));
        let Texel = clamp(Corner + Offset, vec2i(0), Extent - vec2i(1));
        let Guide = textureLoad(SourceGuide, Texel, 0);
        if (Guide.w < 0.5)
        {
            continue;
        }
        let Signal = textureLoad(SourceSignal, Texel, 0).rgb;
        let DepthDifference = abs(Guide.z - ViewDepth) / max(ViewDepth, 1.0e-3);
        let Facing = max(dot(DecodeOctahedral(Guide.xy), Normal), 0.0);
        let Bilinear = select(1.0 - Fraction.x, Fraction.x, Offset.x == 1) * select(1.0 - Fraction.y, Fraction.y, Offset.y == 1);
        let Weight = (Bilinear + 1.0e-3) * exp(-DepthDifference * 40.0) * pow(Facing, 16.0);
        Sum += Signal * Weight;
        WeightSum += Weight;
        let Score = DepthDifference + (1.0 - Facing);
        if (Score < NearestScore)
        {
            NearestScore = Score;
            Nearest = Signal;
        }
    }
    let Projected = select(Nearest, Sum / WeightSum, WeightSum > 1.0e-4);
    textureStore(ScreenTarget, Pixel, vec4f(Projected, 1.0));
}
