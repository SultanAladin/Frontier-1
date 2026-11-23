/*==========================================================================================================================================
                                                            MATCAPSURFACE.SLANG
==========================================================================================================================================*/
// 🧩 Sample a resident lit-sphere texture by normalized view-space normal, independently of CAD edges or PBR lighting.

//------------------------------------------------------------------------------------------------------------------------
//                                                     PUSH CONSTANTS AND TEXTURE LOOKUP
//------------------------------------------------------------------------------------------------------------------------

struct MatCapProjection
{
    column_major float4x4 ClipFromSurface;
    float4 ViewNormalRow0;
    float4 ViewNormalRow1;
    float4 ViewNormalRow2;
    float4 SurfaceTint;
};
[[vk::push_constant]] ConstantBuffer<MatCapProjection> Projection;
[[vk::binding(0, 0)]] Texture2D<float4> MatCapTexture;
[[vk::binding(1, 0)]] SamplerState MatCapSampler;

struct MatCapInput
{
    float3 Position : POSITION0;
    float3 Normal : NORMAL0;
};
struct MatCapPixel
{
    float4 Position : SV_Position;
    float3 ViewNormal : NORMAL0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SURFACE RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
MatCapPixel MatCapSurfaceVertex(MatCapInput Corner)
{
    MatCapPixel Pixel;
    Pixel.Position = mul(Projection.ClipFromSurface, float4(Corner.Position, 1));
    Pixel.ViewNormal = float3(dot(Projection.ViewNormalRow0.xyz, Corner.Normal),
                              dot(Projection.ViewNormalRow1.xyz, Corner.Normal),
                              dot(Projection.ViewNormalRow2.xyz, Corner.Normal));
    return Pixel;
}

[shader("fragment")]
float4 MatCapSurfaceFragment(MatCapPixel Pixel) : SV_Target
{
    // 📝 Normalize after interpolation. Scaling first avoids overflow; cancelling normals use a deterministic centre lookup.
    float Maximum = max(abs(Pixel.ViewNormal.x), max(abs(Pixel.ViewNormal.y), abs(Pixel.ViewNormal.z)));
    float3 Normal = Maximum > 1e-20 ? Pixel.ViewNormal / Maximum : float3(0, 0, 1);
    Normal = normalize(Normal);
    // 📝 Positive camera Y maps to the top of a top-row-first texture. Source alpha is not surface transparency.
    float2 Coordinates = Normal.xy * float2(0.5, -0.5) + 0.5;
    return float4(MatCapTexture.SampleLevel(MatCapSampler, Coordinates, 0).rgb * Projection.SurfaceTint.rgb, 1);
}
