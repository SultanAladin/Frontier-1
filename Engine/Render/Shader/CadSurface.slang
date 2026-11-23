/*==========================================================================================================================================
                                                             CADSURFACE.SLANG
==========================================================================================================================================*/
// 🧩 Minimal CAD surface rasterization; no material archive, visibility buffer, lighting history or ray-tracing dependency.

#include "SurfaceProjection.slang"

[[vk::push_constant]] ConstantBuffer<SurfaceProjection> Projection;

//----------------------------------------------------------------------------------------------------------------------
//                                               SURFACE INPUT AND OUTPUT
//----------------------------------------------------------------------------------------------------------------------

struct SurfaceInput
{
    float3 Position : POSITION0;
    float3 Normal : NORMAL0;
};
struct SurfacePixel
{
    float4 Position : SV_Position;
    float3 Normal : NORMAL0;
};

[shader("vertex")]
SurfacePixel CadSurfaceVertex(SurfaceInput Corner)
{
    SurfacePixel Pixel;
    Pixel.Position = mul(Projection.ClipFromSurface, float4(Corner.Position, 1));
    Pixel.Normal = Corner.Normal;
    return Pixel;
}

[shader("fragment")]
float4 CadSurfaceFragment(SurfacePixel Pixel) : SV_Target
{
    // 📝 A fixed technical face tint, not a PBR material or illumination approximation advertised as one.
    float Facing = 0.65 + 0.35 * abs(dot(normalize(Pixel.Normal), normalize(float3(0.35, -0.45, 0.8))));
    return float4(Projection.SurfaceColour.rgb * Facing, Projection.SurfaceColour.a);
}
