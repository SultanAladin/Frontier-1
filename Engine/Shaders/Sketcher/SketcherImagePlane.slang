/*====================================================================================================================================
                                                       SOLIDARCIMAGEPLANE.SLANG
====================================================================================================================================*/
// 🧩 Image planes: a blueprint crop on a world parallelogram, depth-tested behind the model, line art drawn as light ink

#include "Engine/Shaders/Sketcher/SketcherProjection.slang"

[[vk::push_constant]] ConstantBuffer<SketcherImagePlaneProjection> Projection;
[[vk::binding(0, 1)]] Sampler2D Picture;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT/OUTPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PlanePixel
{
    float4 Position : SV_Position;                                                           // [-]   - Clip position
    float2 Texel    : TEXCOORD0;                                                             // [0-1] - Picture uv
};

struct PlaneTargets
{
    float4 Colour : SV_Target0;                                                              // [0-1] - Blended over the scene
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
PlanePixel SketcherImagePlaneVertex(uint VertexIndex : SV_VulkanVertexID)
{
    // 📝 Two triangles: (0, 0) (1, 0) (1, 1) · (0, 0) (1, 1) (0, 1) in crop fractions (s across, t down).
    const float2 Corners[6] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0) };
    float2 Fraction = Corners[VertexIndex];
    float3 World    = Projection.Corner.xyz + Fraction.x * Projection.Right.xyz + Fraction.y * Projection.Down.xyz;
    PlanePixel Pixel;
    Pixel.Position = mul(Projection.ClipFromWorld, float4(World, 1.0));
    Pixel.Texel    = lerp(Projection.Crop.xy, Projection.Crop.zw, Fraction);
    return Pixel;
}

[shader("fragment")]
PlaneTargets SketcherImagePlaneFragment(PlanePixel Pixel)
{
    float4 Sampled = Picture.Sample(Pixel.Texel);
    float  Opacity = Projection.Corner.w;
    PlaneTargets Targets;
    if (Projection.Right.w > 0.5)
    {
        Targets.Colour = float4(Sampled.rgb, Opacity * Sampled.a);
        return Targets;
    }
    // 📝 Line art: ink = 1 − luminance, drawn in a pale blueprint tint; paper keeps a faint wash so the plane reads.
    float Ink = 1.0 - dot(Sampled.rgb, float3(0.2126, 0.7152, 0.0722));
    Targets.Colour = float4(float3(0.80, 0.88, 1.00), Opacity * max(saturate(Ink * 1.6), 0.07) * Sampled.a);
    return Targets;
}
