/*====================================================================================================================================
                                                          SOLIDARCGRID.SLANG
====================================================================================================================================*/
// 🧩 Analytic grid on the active drawing plane (adaptive decades, world-axis colours), depth-tested behind the model

#include "Engine/Shaders/Sketcher/SketcherProjection.slang"

[[vk::push_constant]] ConstantBuffer<SketcherGridProjection> Projection;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT/OUTPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GridPixel
{
    float4 Position : SV_Position;                                                           // [-] - Clip position
    float2 Device   : TEXCOORD0;                                                             // [-] - Normalised device xy
};

struct GridTargets
{
    float4 Colour : SV_Target0;                                                              // [0-1] - Blended over the scene
    float  Depth  : SV_Depth;                                                                // [0-1] - Depth of the plane point
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Line coverage of a periodic grid of spacing Spacing at Point (isotropic Euclidean screen footprint ~1 px wide).
float GridCoverage(float2 Point, float Spacing)
{
    float2 Scaled = Point / Spacing;
    float2 Du     = ddx(Scaled);
    float2 Dv     = ddy(Scaled);
    float2 Width  = sqrt(Du * Du + Dv * Dv);
    float2 Line   = abs(frac(Scaled - 0.5) - 0.5) / max(Width, float2(1.0e-6, 1.0e-6));
    return 1.0 - saturate(min(Line.x, Line.y));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
GridPixel SketcherGridVertex(uint VertexIndex : SV_VulkanVertexID)
{
    // 📝 One triangle covering the screen: (−1, −1), (3, −1), (−1, 3).
    float2 Device = float2((VertexIndex == 1u) ? 3.0 : -1.0, (VertexIndex == 2u) ? 3.0 : -1.0);
    GridPixel Pixel;
    Pixel.Position = float4(Device, 0.5, 1.0);
    Pixel.Device   = Device;
    return Pixel;
}

[shader("fragment")]
GridTargets SketcherGridFragment(GridPixel Pixel)
{
    float4 Near = mul(Projection.PlaneFromClip, float4(Pixel.Device, 0.0, 1.0));
    float4 Far  = mul(Projection.PlaneFromClip, float4(Pixel.Device, 1.0, 1.0));
    float3 From = Near.xyz / Near.w;
    float3 To   = Far.xyz / Far.w;
    float  Run  = To.z - From.z;
    // 📝 No discard before fwidth: derivatives need every lane of the 2 × 2 quad alive, so misses are dropped at the end.
    float  Reach    = abs(Run) < 1.0e-9 ? -1.0 : -From.z / Run;
    bool   HitCondition = Reach >= 0.0 && Reach <= 1.0;
    float3 Ground   = lerp(From, To, saturate(Reach));

    // Device depth where the homogeneous n coordinate vanishes (linear in device z).
    float DepthRun = Far.z - Near.z;
    float Depth    = abs(DepthRun) < 1.0e-12 ? 2.0 : -Near.z / DepthRun;
    float Weight   = lerp(Near.w, Far.w, saturate(Depth));

    // 📝 Adaptive 3-decade cascade: continuous across integer decade crossings (0.00 -> 0.42 -> 0.82 -> 0.82)
    //    with isotropic Euclidean derivatives so no polygonal brightness step appears on the plane.
    float2 GroundDu      = ddx(Ground.xy);
    float2 GroundDv      = ddy(Ground.xy);
    float2 Footprint     = sqrt(GroundDu * GroundDu + GroundDv * GroundDv);
    float  FootprintPeak = max(length(Footprint) * 0.70710678, 1.0e-6);
    float  Decade        = log10(FootprintPeak * 8.0);
    float  BaseDecade    = floor(Decade);
    float  Blend         = frac(Decade);
    float  SubMinor      = pow(10.0, BaseDecade);
    float  Minor         = SubMinor * 10.0;
    float  Major         = SubMinor * 100.0;
    float  SubLine       = GridCoverage(Ground.xy, SubMinor) * (0.42 * (1.0 - Blend));
    float  MinorLine     = GridCoverage(Ground.xy, Minor) * lerp(0.82, 0.42, Blend);
    float  MajorLine     = GridCoverage(Ground.xy, Major) * 0.82;
    float  Line          = max(SubLine, max(MinorLine, MajorLine));

    float3 Colour = float3(0.62, 0.66, 0.74);
    float  Alpha  = Line * 0.32;
    float  AxisU  = saturate(1.6 - abs(Ground.y) / max(Footprint.y, 1.0e-6));
    float  AxisV  = saturate(1.6 - abs(Ground.x) / max(Footprint.x, 1.0e-6));
    if (AxisU > 0.0) { Colour = lerp(Colour, Projection.AlongU.rgb, AxisU); Alpha = max(Alpha, AxisU * 0.85); } // axis along u (v = 0)
    if (AxisV > 0.0) { Colour = lerp(Colour, Projection.AlongV.rgb, AxisV); Alpha = max(Alpha, AxisV * 0.85); } // axis along v (u = 0)

    // 📝 Fade over the far part of the near → far range so the horizon never aliases.
    float Fade = saturate(1.0 - Reach * 2.5);
    if (!HitCondition || Depth < 0.0 || Depth > 1.0 || Weight <= 0.0 || Alpha * Fade < 0.004) discard;

    GridTargets Targets;
    Targets.Colour = float4(Colour, Alpha * Fade);
    Targets.Depth  = saturate(Depth);
    return Targets;
}
