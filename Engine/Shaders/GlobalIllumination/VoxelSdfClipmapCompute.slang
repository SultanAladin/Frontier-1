/*============================================================================================================================================
                                                    VOXELSDFCLIPMAPCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 2-Cascade Voxel SDF Clipmap + Dynamic Local Object SDF Union + Multi-Bounce Surface Cache + Penumbra Shadows + Deterministic Cone GI

module VoxelSdfClipmapCompute;

static const float PiConst    = 3.14159265358979323846;
static const float InvPiConst = 0.31830988618379067154;
static const float TauConst   = 6.28318530717958647692;

struct VoxelSdfClipmapUniforms
{
    float4 Clip0CenterHalfExtent; // xyz = Clip0 center, w = Clip0 half-extent [m]
    float4 Clip1CenterHalfExtent; // xyz = Clip1 center, w = Clip1 half-extent [m]
    uint4  GridParams;            // x = Clip0Res, y = Clip1Res, z = DynamicObjectCount, w = PointLightCount
    float4 SunDirTanRadius;       // xyz = SunDir, w = tan(SunAngularRadius)
    float4 SunIrradiancePad;      // xyz = SunIrradiance, w = RadiosityStepMask
    float4 SkyRadiancePad;        // xyz = SkyRadiance, w = ConeStepClamp
    int4   DirtyMinVoxel;         // xyz = DirtyClip0Min, w = ViewportWidth
    int4   DirtyMaxVoxel;         // xyz = DirtyClip0Max, w = ViewportHeight
};

struct DynamicLocalSdfGpuEntry
{
    float4 WorldToLocalRow0; // xyz, tx
    float4 WorldToLocalRow1; // xyz, ty
    float4 WorldToLocalRow2; // xyz, tz
    float4 LocalCenterExtent; // xyz = LocalCenter, w = LocalHalfExtent
    float4 WorldAabbMin;      // xyz = WorldMin, w = LocalSdfResolution
    float4 WorldAabbMax;      // xyz = WorldMax, w = LocalBaseVoxelOffset
    float4 EmissiveRadiosity; // xyz = EmissiveRgb, w = Metallic
    float4 AlbedoRoughness;   // xyz = AlbedoRgb, w = Roughness
};

struct VoxelPointLightGpuEntry
{
    float4 PositionRange;  // xyz = WorldPos, w = RangeM
    float4 ColorIntensity; // xyz = Color, w = Intensity
    float4 RadiusPad;      // x = RadiusM
};

[[vk::binding(0, 0)]]  ConstantBuffer<VoxelSdfClipmapUniforms>    Uniforms;
[[vk::binding(1, 0)]]  StructuredBuffer<float>                     StaticClip0Distance;
[[vk::binding(2, 0)]]  StructuredBuffer<float>                     Clip1Distance;
[[vk::binding(3, 0)]]  RWStructuredBuffer<float>                   ActiveClip0Distance;
[[vk::binding(4, 0)]]  StructuredBuffer<float4>                    SurfaceCacheAlbedoMetallic0;
[[vk::binding(5, 0)]]  StructuredBuffer<float4>                    SurfaceCacheNormalRoughness0;
[[vk::binding(6, 0)]]  StructuredBuffer<float4>                    SurfaceCacheEmissive0;
[[vk::binding(7, 0)]]  StructuredBuffer<float4>                    SurfaceCacheRadiosityRead0;
[[vk::binding(8, 0)]]  RWStructuredBuffer<float4>                  SurfaceCacheRadiosityWrite0;
[[vk::binding(9, 0)]]  StructuredBuffer<float4>                    SurfaceCacheRadiosityRead1;
[[vk::binding(10, 0)]] StructuredBuffer<DynamicLocalSdfGpuEntry>   DynamicObjects;
[[vk::binding(11, 0)]] StructuredBuffer<float>                     LocalObjectSdfVoxels;
[[vk::binding(12, 0)]] StructuredBuffer<VoxelPointLightGpuEntry>   PointLights;
[[vk::binding(13, 0)]] StructuredBuffer<float4>                    PrimarySurfacePositions;
[[vk::binding(14, 0)]] StructuredBuffer<float4>                    PrimarySurfaceNormals;
[[vk::binding(15, 0)]] RWTexture2D<float4>                         OutClipmapConeGiOverPi;

float SampleGridTrilinear(StructuredBuffer<float> Grid, uint Res, float3 Center, float HalfExtent, float3 WorldPos, out bool OutInside)
{
    float3 MinW = Center - float3(HalfExtent);
    float3 MaxW = Center + float3(HalfExtent);
    OutInside = all(WorldPos >= MinW) && all(WorldPos <= MaxW);
    float Spacing = (2.0 * HalfExtent) / max(1.0, float(Res - 1u));
    float3 Rel = clamp((WorldPos - MinW) / max(1.0e-5, Spacing), float3(0.0), float3(float(Res - 1u)));
    int3 P0 = clamp(int3(floor(Rel)), int3(0), int3(int(Res) - 1));
    int3 P1 = min(P0 + int3(1), int3(int(Res) - 1));
    float3 F = saturate(Rel - float3(P0));
    uint SXY = Res * Res;

    float C00 = lerp(Grid[uint(P0.z) * SXY + uint(P0.y) * Res + uint(P0.x)],
                     Grid[uint(P0.z) * SXY + uint(P0.y) * Res + uint(P1.x)], F.x);
    float C10 = lerp(Grid[uint(P0.z) * SXY + uint(P1.y) * Res + uint(P0.x)],
                     Grid[uint(P0.z) * SXY + uint(P1.y) * Res + uint(P1.x)], F.x);
    float C01 = lerp(Grid[uint(P1.z) * SXY + uint(P0.y) * Res + uint(P0.x)],
                     Grid[uint(P1.z) * SXY + uint(P0.y) * Res + uint(P1.x)], F.x);
    float C11 = lerp(Grid[uint(P1.z) * SXY + uint(P1.y) * Res + uint(P0.x)],
                     Grid[uint(P1.z) * SXY + uint(P1.y) * Res + uint(P1.x)], F.x);
    return lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
}

float SampleRwGridTrilinear(RWStructuredBuffer<float> Grid, uint Res, float3 Center, float HalfExtent, float3 WorldPos, out bool OutInside)
{
    float3 MinW = Center - float3(HalfExtent);
    float3 MaxW = Center + float3(HalfExtent);
    OutInside = all(WorldPos >= MinW) && all(WorldPos <= MaxW);
    float Spacing = (2.0 * HalfExtent) / max(1.0, float(Res - 1u));
    float3 Rel = clamp((WorldPos - MinW) / max(1.0e-5, Spacing), float3(0.0), float3(float(Res - 1u)));
    int3 P0 = clamp(int3(floor(Rel)), int3(0), int3(int(Res) - 1));
    int3 P1 = min(P0 + int3(1), int3(int(Res) - 1));
    float3 F = saturate(Rel - float3(P0));
    uint SXY = Res * Res;

    float C00 = lerp(Grid[uint(P0.z) * SXY + uint(P0.y) * Res + uint(P0.x)],
                     Grid[uint(P0.z) * SXY + uint(P0.y) * Res + uint(P1.x)], F.x);
    float C10 = lerp(Grid[uint(P0.z) * SXY + uint(P1.y) * Res + uint(P0.x)],
                     Grid[uint(P0.z) * SXY + uint(P1.y) * Res + uint(P1.x)], F.x);
    float C01 = lerp(Grid[uint(P1.z) * SXY + uint(P0.y) * Res + uint(P0.x)],
                     Grid[uint(P1.z) * SXY + uint(P0.y) * Res + uint(P1.x)], F.x);
    float C11 = lerp(Grid[uint(P1.z) * SXY + uint(P1.y) * Res + uint(P0.x)],
                     Grid[uint(P1.z) * SXY + uint(P1.y) * Res + uint(P1.x)], F.x);
    return lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
}

float3 SampleGridFloat4Trilinear(StructuredBuffer<float4> Grid, uint Res, float3 Center, float HalfExtent, float3 WorldPos)
{
    float3 MinW = Center - float3(HalfExtent);
    float Spacing = (2.0 * HalfExtent) / max(1.0, float(Res - 1u));
    float3 Rel = clamp((WorldPos - MinW) / max(1.0e-5, Spacing), float3(0.0), float3(float(Res - 1u)));
    int3 P0 = clamp(int3(floor(Rel)), int3(0), int3(int(Res) - 1));
    int3 P1 = min(P0 + int3(1), int3(int(Res) - 1));
    float3 F = saturate(Rel - float3(P0));
    uint SXY = Res * Res;

    float3 C00 = lerp(Grid[uint(P0.z) * SXY + uint(P0.y) * Res + uint(P0.x)].xyz,
                      Grid[uint(P0.z) * SXY + uint(P0.y) * Res + uint(P1.x)].xyz, F.x);
    float3 C10 = lerp(Grid[uint(P0.z) * SXY + uint(P1.y) * Res + uint(P0.x)].xyz,
                      Grid[uint(P0.z) * SXY + uint(P1.y) * Res + uint(P1.x)].xyz, F.x);
    float3 C01 = lerp(Grid[uint(P1.z) * SXY + uint(P0.y) * Res + uint(P0.x)].xyz,
                      Grid[uint(P1.z) * SXY + uint(P0.y) * Res + uint(P1.x)].xyz, F.x);
    float3 C11 = lerp(Grid[uint(P1.z) * SXY + uint(P1.y) * Res + uint(P0.x)].xyz,
                      Grid[uint(P1.z) * SXY + uint(P1.y) * Res + uint(P1.x)].xyz, F.x);
    return lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
}

float SampleDynamicLocalObjectSdf(DynamicLocalSdfGpuEntry Obj, float3 WorldPos)
{
    float3 LocalPos = float3(
        dot(Obj.WorldToLocalRow0.xyz, WorldPos) + Obj.WorldToLocalRow0.w,
        dot(Obj.WorldToLocalRow1.xyz, WorldPos) + Obj.WorldToLocalRow1.w,
        dot(Obj.WorldToLocalRow2.xyz, WorldPos) + Obj.WorldToLocalRow2.w);
    float  HalfExt = Obj.LocalCenterExtent.w;
    float3 MinL    = Obj.LocalCenterExtent.xyz - float3(HalfExt);
    float3 MaxL    = Obj.LocalCenterExtent.xyz + float3(HalfExt);
    float3 Clamped = clamp(LocalPos, MinL, MaxL);
    float  OutDist = length(LocalPos - Clamped);

    uint   LRes    = max(2u, uint(Obj.WorldAabbMin.w + 0.5));
    uint   BaseOff = uint(Obj.WorldAabbMax.w + 0.5);
    float  Spacing = (2.0 * HalfExt) / float(LRes - 1u);
    float3 Rel     = clamp((Clamped - MinL) / max(1.0e-5, Spacing), float3(0.0), float3(float(LRes - 1u)));
    int3   P0      = clamp(int3(floor(Rel)), int3(0), int3(int(LRes) - 1));
    int3   P1      = min(P0 + int3(1), int3(int(LRes) - 1));
    float3 F       = saturate(Rel - float3(P0));
    uint   SXY     = LRes * LRes;

    float C00 = lerp(LocalObjectSdfVoxels[BaseOff + uint(P0.z) * SXY + uint(P0.y) * LRes + uint(P0.x)],
                     LocalObjectSdfVoxels[BaseOff + uint(P0.z) * SXY + uint(P0.y) * LRes + uint(P1.x)], F.x);
    float C10 = lerp(LocalObjectSdfVoxels[BaseOff + uint(P0.z) * SXY + uint(P1.y) * LRes + uint(P0.x)],
                     LocalObjectSdfVoxels[BaseOff + uint(P0.z) * SXY + uint(P1.y) * LRes + uint(P1.x)], F.x);
    float C01 = lerp(LocalObjectSdfVoxels[BaseOff + uint(P1.z) * SXY + uint(P0.y) * LRes + uint(P0.x)],
                     LocalObjectSdfVoxels[BaseOff + uint(P1.z) * SXY + uint(P0.y) * LRes + uint(P1.x)], F.x);
    float C11 = lerp(LocalObjectSdfVoxels[BaseOff + uint(P1.z) * SXY + uint(P1.y) * LRes + uint(P0.x)],
                     LocalObjectSdfVoxels[BaseOff + uint(P1.z) * SXY + uint(P1.y) * LRes + uint(P1.x)], F.x);
    float LocalD = lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
    return (OutDist > 0.0) ? (LocalD + OutDist) : LocalD;
}

void ConstructBasis(float3 N, out float3 T, out float3 B)
{
    float SignZ = (N.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + N.z);
    float D     = N.x * N.y * A;
    T = float3(1.0 + SignZ * N.x * N.x * A, SignZ * D, -SignZ * N.x);
    B = float3(D, SignZ + N.y * N.y * A, -N.y);
}

float SampleClipmapDistance(float3 WorldPos, out uint OutCascade, out float OutStepScale)
{
    bool In0 = false;
    float D0 = SampleRwGridTrilinear(
        ActiveClip0Distance, Uniforms.GridParams.x,
        Uniforms.Clip0CenterHalfExtent.xyz, Uniforms.Clip0CenterHalfExtent.w, WorldPos, In0);
    if (In0)
    {
        float3 Rel0 = abs(WorldPos - Uniforms.Clip0CenterHalfExtent.xyz) / max(1.0e-5, Uniforms.Clip0CenterHalfExtent.w);
        float  M0   = max(Rel0.x, max(Rel0.y, Rel0.z));
        OutCascade   = 0u;
        OutStepScale = (2.0 * Uniforms.Clip0CenterHalfExtent.w) / float(max(2u, Uniforms.GridParams.x) - 1u);
        if (M0 < 0.85) return D0;
        bool In1 = false;
        float D1 = SampleGridTrilinear(
            Clip1Distance, Uniforms.GridParams.y,
            Uniforms.Clip1CenterHalfExtent.xyz, Uniforms.Clip1CenterHalfExtent.w, WorldPos, In1);
        if (In1)
        {
            float B = smoothstep(0.85, 0.99, M0);
            return lerp(D0, D1, B);
        }
        return D0;
    }

    bool In1 = false;
    float D1 = SampleGridTrilinear(
        Clip1Distance, Uniforms.GridParams.y,
        Uniforms.Clip1CenterHalfExtent.xyz, Uniforms.Clip1CenterHalfExtent.w, WorldPos, In1);
    if (In1)
    {
        OutCascade   = 1u;
        OutStepScale = (2.0 * Uniforms.Clip1CenterHalfExtent.w) / float(max(2u, Uniforms.GridParams.y) - 1u);
        return D1;
    }

    OutCascade   = 2u;
    OutStepScale = (2.0 * Uniforms.Clip1CenterHalfExtent.w) / float(max(2u, Uniforms.GridParams.y) - 1u);
    float3 Min1  = Uniforms.Clip1CenterHalfExtent.xyz - float3(Uniforms.Clip1CenterHalfExtent.w);
    float3 Max1  = Uniforms.Clip1CenterHalfExtent.xyz + float3(Uniforms.Clip1CenterHalfExtent.w);
    float3 Clp   = clamp(WorldPos, Min1, Max1);
    return D1 + length(WorldPos - Clp);
}

// Incremental dirty-AABB dynamic local object SDF union into ActiveClip0
[shader("compute")]
[numthreads(64, 1, 1)]
void ComposeDynamicLocalSdfClip0Entry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Res = Uniforms.GridParams.x;
    uint Idx = DispatchThreadID.x;
    if (Res < 2u || Idx >= Res * Res * Res) return;

    uint SXY = Res * Res;
    uint IZ  = Idx / SXY;
    uint Rem = Idx - IZ * SXY;
    uint IY  = Rem / Res;
    uint IX  = Rem - IY * Res;

    if (int(IX) < Uniforms.DirtyMinVoxel.x || int(IX) > Uniforms.DirtyMaxVoxel.x ||
        int(IY) < Uniforms.DirtyMinVoxel.y || int(IY) > Uniforms.DirtyMaxVoxel.y ||
        int(IZ) < Uniforms.DirtyMinVoxel.z || int(IZ) > Uniforms.DirtyMaxVoxel.z)
    {
        return;
    }

    float  D        = StaticClip0Distance[Idx];
    float  Spacing  = (2.0 * Uniforms.Clip0CenterHalfExtent.w) / float(Res - 1u);
    float3 WorldPos = (Uniforms.Clip0CenterHalfExtent.xyz - float3(Uniforms.Clip0CenterHalfExtent.w))
                    + float3(float(IX), float(IY), float(IZ)) * Spacing;

    uint DynCount = Uniforms.GridParams.z;
    for (uint O = 0u; O < DynCount; ++O)
    {
        DynamicLocalSdfGpuEntry Obj = DynamicObjects[O];
        if (all(WorldPos >= Obj.WorldAabbMin.xyz) && all(WorldPos <= Obj.WorldAabbMax.xyz))
        {
            D = min(D, SampleDynamicLocalObjectSdf(Obj, WorldPos));
        }
    }
    ActiveClip0Distance[Idx] = D;
}

// Multi-bounce Unreal-style surface cache radiosity propagation (direct sun + point + emissive + 12 deterministic Fibonacci cones)
[shader("compute")]
[numthreads(64, 1, 1)]
void PropagateClipmapRadiosityBounceEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Res = Uniforms.GridParams.x;
    uint Idx = DispatchThreadID.x;
    if (Res < 2u || Idx >= Res * Res * Res) return;

    float4 AlbMet = SurfaceCacheAlbedoMetallic0[Idx];
    float4 NrmRgh = SurfaceCacheNormalRoughness0[Idx];
    float4 Emis   = SurfaceCacheEmissive0[Idx];
    if (NrmRgh.w < 0.0 && max(Emis.x, max(Emis.y, Emis.z)) <= 1.0e-5)
    {
        SurfaceCacheRadiosityWrite0[Idx] = float4(0.0);
        return;
    }

    uint SXY = Res * Res;
    uint IZ  = Idx / SXY;
    uint Rem = Idx - IZ * SXY;
    uint IY  = Rem / Res;
    uint IX  = Rem - IY * Res;

    float  Spacing  = (2.0 * Uniforms.Clip0CenterHalfExtent.w) / float(Res - 1u);
    float3 WorldPos = (Uniforms.Clip0CenterHalfExtent.xyz - float3(Uniforms.Clip0CenterHalfExtent.w))
                    + float3(float(IX), float(IY), float(IZ)) * Spacing;
    float3 N = normalize(NrmRgh.xyz);
    float3 T, B;
    ConstructBasis(N, T, B);

    float3 IndirectOverPi = float3(0.0);
    for (uint C = 0u; C < 12u; ++C)
    {
        float U1   = (float(C) + 0.5) / 12.0;
        float Phi  = float(C) * 2.39996323;
        float SinT = sqrt(saturate(U1));
        float CosT = sqrt(max(0.0, 1.0 - U1));
        float3 Dir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N * CosT;

        float  Dist   = Spacing * 1.15;
        float  Trans  = 1.0;
        float3 AccumB = float3(0.0);
        for (uint Step = 0u; Step < 20u && Dist < 8.0; ++Step)
        {
            float3 SPos = WorldPos + N * (Spacing * 0.85) + Dir * Dist;
            uint   Cas  = 0u;
            float  H    = Spacing;
            float  SdfD = SampleClipmapDistance(SPos, Cas, H);
            float  ConeR = max(H * 0.75, Dist * 0.42);
            if (SdfD < ConeR)
            {
                float Occ = saturate((1.0 - SdfD / ConeR) * 1.05);
                float3 BounceRad = (Cas == 0u)
                    ? SampleGridFloat4Trilinear(SurfaceCacheRadiosityRead0, Uniforms.GridParams.x, Uniforms.Clip0CenterHalfExtent.xyz, Uniforms.Clip0CenterHalfExtent.w, SPos)
                    : SampleGridFloat4Trilinear(SurfaceCacheRadiosityRead1, Uniforms.GridParams.y, Uniforms.Clip1CenterHalfExtent.xyz, Uniforms.Clip1CenterHalfExtent.w, SPos);
                AccumB += BounceRad * (Occ * Trans);
                Trans  *= (1.0 - Occ * 0.78);
                if (Trans < 0.04) { Trans = 0.0; break; }
            }
            Dist += max(H * 0.60, abs(SdfD) * 0.85);
        }
        IndirectOverPi += (Uniforms.SkyRadiancePad.xyz * (0.28 * Trans) + AccumB) * (1.0 / 12.0);
    }

    float3 DiffAlbedo = AlbMet.xyz * (1.0 - saturate(AlbMet.w));
    float3 PrevDirect = SurfaceCacheRadiosityRead0[Idx].xyz;
    SurfaceCacheRadiosityWrite0[Idx] = float4(max(PrevDirect, Emis.xyz + DiffAlbedo * IndirectOverPi), 1.0);
}

// Deterministic 8-cone SDF Clipmap Global Illumination pass (ZERO RNG!)
[shader("compute")]
[numthreads(8, 8, 1)]
void EvaluateClipmapConeGiEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(max(1, Uniforms.DirtyMinVoxel.w));
    uint Height = uint(max(1, Uniforms.DirtyMaxVoxel.w));
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height) return;

    uint PixelIdx = DispatchThreadID.y * Width + DispatchThreadID.x;
    float4 PosW   = PrimarySurfacePositions[PixelIdx];
    if (PosW.w <= 0.0)
    {
        OutClipmapConeGiOverPi[DispatchThreadID.xy] = float4(0.0);
        return;
    }

    float3 N = normalize(PrimarySurfaceNormals[PixelIdx].xyz);
    float3 T, B;
    ConstructBasis(N, T, B);

    float  Spacing0 = (2.0 * Uniforms.Clip0CenterHalfExtent.w) / float(max(2u, Uniforms.GridParams.x) - 1u);
    float3 Origin   = PosW.xyz + N * (Spacing0 * 0.85);
    float3 Accum    = float3(0.0);

    for (uint C = 0u; C < 8u; ++C)
    {
        float U1   = (float(C) + 0.5) / 8.0;
        float Phi  = float(C) * 2.39996323;
        float SinT = sqrt(saturate(U1));
        float CosT = sqrt(max(0.0, 1.0 - U1));
        float3 Dir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N * CosT;

        float  Dist   = Spacing0 * 0.70;
        float  Trans  = 1.0;
        float3 Bounce = float3(0.0);
        for (uint Step = 0u; Step < 24u && Dist < 9.5; ++Step)
        {
            float3 SPos = Origin + Dir * Dist;
            uint   Cas  = 0u;
            float  H    = Spacing0;
            float  SdfD = SampleClipmapDistance(SPos, Cas, H);
            float  ConeR = max(H * 0.65, Dist * 0.42);
            if (SdfD < ConeR)
            {
                float Occ = saturate((1.0 - SdfD / ConeR) * ((Dist < 0.9) ? 1.15 : 1.0));
                float3 VRad = (Cas == 0u)
                    ? SampleGridFloat4Trilinear(SurfaceCacheRadiosityRead0, Uniforms.GridParams.x, Uniforms.Clip0CenterHalfExtent.xyz, Uniforms.Clip0CenterHalfExtent.w, SPos)
                    : SampleGridFloat4Trilinear(SurfaceCacheRadiosityRead1, Uniforms.GridParams.y, Uniforms.Clip1CenterHalfExtent.xyz, Uniforms.Clip1CenterHalfExtent.w, SPos);
                Bounce += VRad * (Occ * Trans);
                Trans  *= (1.0 - Occ * 0.76);
                if (Trans < 0.035) { Trans = 0.0; break; }
            }
            Dist += max(H * 0.55, abs(SdfD) * 0.85);
        }
        Accum += (Uniforms.SkyRadiancePad.xyz * Trans + Bounce) * (1.0 / 8.0);
    }

    OutClipmapConeGiOverPi[DispatchThreadID.xy] = float4(Accum, 1.0);
}
