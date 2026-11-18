/*============================================================================================================================================
                                                     SIGNEDDISTANCECONEILLUMINATION.SLANG
============================================================================================================================================*/
// 🧩 Tier 1 GTX 6-Cone Cosine-Weighted SignedDistanceVolume Global Illumination Compute Pass

module SignedDistanceConeIllumination;

struct SdfConeGiUniforms
{
    uint   ViewportWidth;
    uint   ViewportHeight;
    float  SdfVoxelSpacing;
    float  Pad0;
    float3 SdfMinCorner;
    float  Pad1;
    uint3  SdfDimensions;
    uint   Pad2;
    float3 SkyRadiance;
    float  Pad3;
};

[[vk::binding(0, 0)]] ConstantBuffer<SdfConeGiUniforms> Uniforms;
[[vk::binding(1, 0)]] StructuredBuffer<float4>          SurfacePositions;
[[vk::binding(2, 0)]] StructuredBuffer<float4>          SurfaceNormals;
[[vk::binding(3, 0)]] StructuredBuffer<float>           SignedDistanceVoxels;
[[vk::binding(4, 0)]] RWTexture2D<float4>               OutIndirectOverPi;
[[vk::binding(5, 0)]] StructuredBuffer<float4>          SdfRadiosityVoxels;

float SampleSdf(float3 WorldPos)
{
    float3 Rel = (WorldPos - Uniforms.SdfMinCorner) / max(1.0e-5, Uniforms.SdfVoxelSpacing);
    int3   P0  = clamp(int3(floor(Rel)), int3(0), int3(Uniforms.SdfDimensions) - int3(1));
    int3   P1  = min(P0 + int3(1), int3(Uniforms.SdfDimensions) - int3(1));
    float3 T   = saturate(Rel - float3(P0));
    uint   SX  = Uniforms.SdfDimensions.x;
    uint   SXY = SX * Uniforms.SdfDimensions.y;

    float C00 = lerp(SignedDistanceVoxels[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P0.x)],
                     SignedDistanceVoxels[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P1.x)], T.x);
    float C10 = lerp(SignedDistanceVoxels[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P0.x)],
                     SignedDistanceVoxels[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P1.x)], T.x);
    float C01 = lerp(SignedDistanceVoxels[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P0.x)],
                     SignedDistanceVoxels[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P1.x)], T.x);
    float C11 = lerp(SignedDistanceVoxels[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P0.x)],
                     SignedDistanceVoxels[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P1.x)], T.x);
    return lerp(lerp(C00, C10, T.y), lerp(C01, C11, T.y), T.z);
}

float3 SampleRadiosity(float3 WorldPos)
{
    float3 Rel = (WorldPos - Uniforms.SdfMinCorner) / max(1.0e-5, Uniforms.SdfVoxelSpacing);
    int3   P0  = clamp(int3(floor(Rel)), int3(0), int3(Uniforms.SdfDimensions) - int3(1));
    int3   P1  = min(P0 + int3(1), int3(Uniforms.SdfDimensions) - int3(1));
    float3 T   = saturate(Rel - float3(P0));
    uint   SX  = Uniforms.SdfDimensions.x;
    uint   SXY = SX * Uniforms.SdfDimensions.y;

    float3 C00 = lerp(SdfRadiosityVoxels[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P0.x)].xyz,
                      SdfRadiosityVoxels[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P1.x)].xyz, T.x);
    float3 C10 = lerp(SdfRadiosityVoxels[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P0.x)].xyz,
                      SdfRadiosityVoxels[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P1.x)].xyz, T.x);
    float3 C01 = lerp(SdfRadiosityVoxels[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P0.x)].xyz,
                      SdfRadiosityVoxels[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P1.x)].xyz, T.x);
    float3 C11 = lerp(SdfRadiosityVoxels[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P0.x)].xyz,
                      SdfRadiosityVoxels[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P1.x)].xyz, T.x);
    return lerp(lerp(C00, C10, T.y), lerp(C01, C11, T.y), T.z);
}

float3 TraceConeRadiosityAndSky(float3 Origin, float3 Dir)
{
    float  Spacing = max(0.04, Uniforms.SdfVoxelSpacing);
    float  Dist    = Spacing * 0.85;
    float  Vis     = 1.0;
    float3 Bounce  = float3(0.0);
    for (uint Step = 0u; Step < 24u && Dist < 8.5; ++Step)
    {
        float3 SPos   = Origin + Dir * Dist;
        float  Sdf    = SampleSdf(SPos);
        float  Radius = max(Spacing * 0.6, 0.42 * Dist);
        if (Sdf < Radius)
        {
            float  Occ  = saturate((1.0 - Sdf / Radius) * ((Dist < 0.85) ? 1.15 : 1.0));
            float3 VRad = SampleRadiosity(SPos);
            Bounce     += VRad * (Occ * Vis);
            Vis        *= (1.0 - Occ * 0.76);
            if (Vis < 0.035)
            {
                Vis = 0.0;
                break;
            }
        }
        Dist += max(Spacing * 0.55, abs(Sdf) * 0.85);
    }
    return Uniforms.SkyRadiance * Vis + Bounce;
}

void ConstructBasis(float3 N, out float3 T, out float3 B)
{
    float SignZ = (N.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + N.z);
    float D     = N.x * N.y * A;
    T = float3(1.0 + SignZ * N.x * N.x * A, SignZ * D, -SignZ * N.x);
    B = float3(D, SignZ + N.y * N.y * A, -N.y);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SdfConeGiComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= Uniforms.ViewportWidth || DispatchThreadID.y >= Uniforms.ViewportHeight)
    {
        return;
    }

    uint   PixelIdx = DispatchThreadID.y * Uniforms.ViewportWidth + DispatchThreadID.x;
    float4 PosW     = SurfacePositions[PixelIdx];
    if (PosW.w <= 0.0)
    {
        OutIndirectOverPi[DispatchThreadID.xy] = float4(0.0);
        return;
    }

    float3 N = normalize(SurfaceNormals[PixelIdx].xyz);
    float3 T, B;
    ConstructBasis(N, T, B);

    // 8 deterministic cosine-weighted Fibonacci cones (Zero RNG!)
    float3 Origin = PosW.xyz + N * (Uniforms.SdfVoxelSpacing * 0.9);
    float3 Accum  = float3(0.0);
    for (uint K = 0u; K < 8u; ++K)
    {
        float U1       = (float(K) + 0.5) / 8.0;
        float Phi      = float(K) * 2.39996323;
        float SinT     = sqrt(saturate(U1));
        float CosT     = sqrt(max(0.0, 1.0 - U1));
        float3 WorldDir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N * CosT;
        Accum += TraceConeRadiosityAndSky(Origin, WorldDir) * (1.0 / 8.0);
    }

    OutIndirectOverPi[DispatchThreadID.xy] = float4(Accum, 1.0);
}
