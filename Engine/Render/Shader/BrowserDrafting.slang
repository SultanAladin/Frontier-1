/*==========================================================================================================================================
                                                           BROWSERDRAFTING.SLANG
==========================================================================================================================================*/
// 🧩 Project Browser depth-tested drafting contours transcribed 1:1 from the reference WebGL2 renderer.
// 📝 Source: Experimental/Project browser/ReferenceRenderer.js DraftingVertexSource + DraftingFragmentSource.
// 📝 The host draws line lists twice (hidden GREATER ink .2 when enabled, then LEQUAL ink .85) with depth writes off.

#include "BrowserProjection.slang"

[[vk::push_constant]] ConstantBuffer<BrowserPreviewProjection> Projection;

struct BrowserDraftingInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] float3 Separation : TEXCOORD0;
    [[vk::location(2)]] float3 NormalA : TEXCOORD1;
    [[vk::location(3)]] float3 NormalB : TEXCOORD2;
    [[vk::location(4)]] float Crease : TEXCOORD3;
};
struct BrowserDraftingPixel
{
    float4 Position : SV_Position;
};

[shader("vertex")]
BrowserDraftingPixel BrowserDraftingVertex(BrowserDraftingInput Corner)
{
    BrowserDraftingPixel Pixel;
    float FacingA = dot(Corner.NormalA, Projection.ViewDirection.xyz);
    float FacingB = dot(Corner.NormalB, Projection.ViewDirection.xyz);
    bool Silhouette = FacingA * FacingB <= 0.0 && abs(FacingA - FacingB) > 0.00001;
    // Orthographic facing is constant along an edge: both endpoints make the same decision.
    Pixel.Position = Corner.Crease > 0.5 || Silhouette
                         ? mul(Projection.ClipFromSurface,
                               float4(Corner.Position + Corner.Separation * Projection.RasterOptions.x, 1))
                         : float4(2.0, 2.0, 2.0, 1.0);
    return Pixel;
}

[shader("fragment")]
float4 BrowserDraftingFragment(BrowserDraftingPixel Pixel) : SV_Target
{
    float Ink = Projection.RasterOptions.z;
    return float4(float3(Ink), 1.0);
}
