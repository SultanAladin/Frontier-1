//============================================================================================================================================
//                                                    MATERIALPREVIEWSURFACE.SLANG
//============================================================================================================================================
// 🧩 One persistent Browser-atlas tile: a fixed studio sphere rendered from PhysicalSurfacePacket constants.
//    The command-recording owner supplies the output image, tile origin and exact packet fingerprint.

[[vk::binding(0, 0)]] RWTexture2D<float4> PreviewAtlas;

struct PhysicalSurfacePacket
{
    float4 Albedo;
    float4 NormalIncidenceReflectance;
    float4 Scalars; // metallic, roughness, opacity, transmission
    float RefractionRatio;
    uint ActiveChannelMask;
    uint Closure;
    uint Features;
    uint Coverage;
    uint Wall;
    uint Interface;
    uint TwoSided;
    uint Reserved;
};

[[vk::binding(1, 0)]] ConstantBuffer<PhysicalSurfacePacket> Surface;

struct PreviewTilePush
{
    uint2 Origin;
    uint Extent;
    uint FingerprintLow;
};
[[vk::push_constant]] PreviewTilePush Tile;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Dispatched : SV_DispatchThreadID)
{
    if (Dispatched.x >= Tile.Extent || Dispatched.y >= Tile.Extent)
        return;

    const float2 UV = (float2(Dispatched.xy) + 0.5f) / float(Tile.Extent);
    const float2 Screen = UV * 2.0f - 1.0f;
    const float RadiusSquared = dot(Screen, Screen);
    const uint2 Target = Tile.Origin + Dispatched.xy;

    // The background stays transparent: Browser chrome owns its own circular presentation and clipping.
    if (RadiusSquared > 1.0f)
    {
        PreviewAtlas[Target] = float4(0.0f);
        return;
    }

    const float Z = sqrt(max(1.0f - RadiusSquared, 0.0f));
    const float3 Normal = normalize(float3(Screen.x, -Screen.y, Z));
    const float3 Light = normalize(float3(-0.45f, 0.65f, 0.72f));
    const float3 View = float3(0.0f, 0.0f, 1.0f);
    const float3 HalfVector = normalize(Light + View);
    const float Diffuse = max(dot(Normal, Light), 0.0f);
    const float Highlight = pow(max(dot(Normal, HalfVector), 0.0f),
                                max(2.0f, 128.0f * (1.0f - Surface.Scalars.y)));
    const float3 F0 = Surface.NormalIncidenceReflectance.rgb;
    const float3 Base = Surface.Albedo.rgb * (0.12f + 0.88f * Diffuse);
    const float3 Lit = Base * (1.0f - Surface.Scalars.x) + F0 * Highlight;
    PreviewAtlas[Target] = float4(Lit, Surface.Scalars.z);
}
