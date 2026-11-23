/*==========================================================================================================================================
                                                          PREVIEWPBRSURFACE.SLANG
==========================================================================================================================================*/
// 🧩 Opaque GGX scattering strata, attenuating dielectric coatings, directional light and sampled analytical sky in scene-linear RGB.

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED PROJECTION AND SCATTERING ABI
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceNormalProjection
{
    column_major float4x4 ClipFromSurface;
    float4 ViewNormalRow0;
    float4 ViewNormalRow1;
    float4 ViewNormalRow2;
    float4 SurfaceTint;
};
#include "ScatteringEvaluation.slang"
[[vk::push_constant]] ConstantBuffer<SurfaceNormalProjection> Projection;
[[vk::binding(0,0)]] Texture2D<float4> AlbedoTexture;
[[vk::binding(1,0)]] SamplerState SurfaceSampler;
[[vk::binding(0,1)]] ConstantBuffer<PreviewScattering> Scattering;

struct PreviewInput
{
    float3 Position : POSITION0;
    float3 Normal : NORMAL0;
};
struct PreviewPixel
{
    float4 Position : SV_Position;
    float3 ViewPosition : TEXCOORD0;
    float3 ViewNormal : NORMAL0;
    float2 Coordinates : TEXCOORD1;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     GGX AND ORDERED SCATTERING
//------------------------------------------------------------------------------------------------------------------------

float3 DecodeAlbedo(float3 Encoded)
{
    return select(Encoded<=.04045,Encoded/12.92,pow((Encoded+.055)/1.055,float3(2.4)));
}
//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED GEOMETRY RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
PreviewPixel PreviewPbrSurfaceVertex(PreviewInput Corner)
{
    PreviewPixel Pixel;
    Pixel.Position = mul(Projection.ClipFromSurface,float4(Corner.Position,1));
    Pixel.ViewPosition = mul(Scattering.ViewFromSurface,float4(Corner.Position,1)).xyz;
    Pixel.ViewNormal = float3(dot(Projection.ViewNormalRow0.xyz,Corner.Normal),
                              dot(Projection.ViewNormalRow1.xyz,Corner.Normal),
                              dot(Projection.ViewNormalRow2.xyz,Corner.Normal));
    // 📝 Explicit planar object-space texture coordinates until authored UVs are connected in full material integration.
    Pixel.Coordinates = Corner.Position.xy*float2(.5,-.5)+.5;
    return Pixel;
}
[shader("fragment")]
float4 PreviewPbrSurfaceFragment(PreviewPixel Pixel) : SV_Target
{
    float3 Normal = NormalizeDirection(Pixel.ViewNormal);
    float3 View = Scattering.Configuration.z != 0 ? NormalizeDirection(-Pixel.ViewPosition) : float3(0,0,1);
    float3 Light = NormalizeDirection(Scattering.LightDirectionIntensity.xyz);
    float3 AlbedoMultiplier = Projection.SurfaceTint.rgb;
    if (Scattering.Configuration.y != 0)
        AlbedoMultiplier *= DecodeAlbedo(AlbedoTexture.SampleLevel(SurfaceSampler,Pixel.Coordinates,0).rgb);
    float3 Radiance = EvaluateScatteringSequence(Scattering,Normal,View,Light,AlbedoMultiplier)*saturate(dot(Normal,Light))*
                      Scattering.LightDirectionIntensity.w*Scattering.LightColourExposure.rgb;
    Radiance += IntegrateEnvironment(Scattering,Normal,View,AlbedoMultiplier);
    return float4(EncodeDisplay(Radiance,Scattering.LightColourExposure.w),1);
}
