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
    uint4  Clip0Dims;             // xyz = Clip0 DimX/Y/Z, w = TotalClip0Voxels
    uint4  Clip1Dims;             // xyz = Clip1 DimX/Y/Z, w = TotalClip1Voxels
    float4 Clip0CenterSpacing;    // xyz = Clip0 center, w = Clip0 VoxelSpacingM
    float4 Clip0HalfExtents;      // xyz = Clip0 half-extents [m], w = 0
    float4 Clip1CenterSpacing;    // xyz = Clip1 center, w = Clip1 VoxelSpacingM
    float4 Clip1HalfExtents;      // xyz = Clip1 half-extents [m], w = 0
    uint4  GridParams;            // x = DynamicObjectCount, y = PointLightCount, z = ViewportWidth, w = ViewportHeight
    float4 SunDirTanRadius;       // xyz = SunDir, w = tan(SunAngularRadius)
    float4 SunIrradiancePad;      // xyz = SunIrradiance, w = RadiosityMaskFlags
    float4 SkyRadiancePad;        // xyz = SkyRadiance, w = ConeStepClamp
    int4   DirtyMinVoxel;         // xyz = DirtyClip0Min, w = 0
    int4   DirtyMaxVoxel;         // xyz = DirtyClip0Max, w = 0
};

struct DynamicLocalSdfGpuEntry
{
    float4 WorldToLocalRow0; // xyz, tx
    float4 WorldToLocalRow1; // xyz, ty
    float4 WorldToLocalRow2; // xyz, tz
    float4 LocalMinSpacing;  // xyz = LocalMin, w = LocalVoxelSpacingM
    float4 LocalMaxPad;      // xyz = LocalMax, w = 0
    uint4  LocalDimsOffset;  // xyz = DimX/Y/Z, w = LocalBaseVoxelOffset
    float4 WorldAabbMin;     // xyz = WorldMin, w = 0
    float4 WorldAabbMax;     // xyz = WorldMax, w = 0
    float4 EmissiveRadiosity;// xyz = EmissiveRgb, w = Metallic
    float4 AlbedoRoughness;  // xyz = AlbedoRgb, w = Roughness
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

float SampleGridTrilinear(
    StructuredBuffer<float> Grid,
    uint3                   Dims,
    float3                  Center,
    float3                  HalfExtents,
    float                   Spacing,
    float3                  WorldPos,
    out bool                OutInside)
{
    float3 MinW = Center - HalfExtents;
    float3 MaxW = Center + HalfExtents;
    OutInside = all(WorldPos >= MinW) && all(WorldPos <= MaxW);
    float3 Rel = clamp((WorldPos - MinW) / max(1.0e-5, Spacing), float3(0.0), float3(Dims - uint3(1u)));
    int3 P0 = clamp(int3(floor(Rel)), int3(0), int3(Dims) - int3(1));
    int3 P1 = min(P0 + int3(1), int3(Dims) - int3(1));
    float3 F = saturate(Rel - float3(P0));
    uint SXY = Dims.x * Dims.y;

    float C00 = lerp(Grid[uint(P0.z) * SXY + uint(P0.y) * Dims.x + uint(P0.x)],
                     Grid[uint(P0.z) * SXY + uint(P0.y) * Dims.x + uint(P1.x)], F.x);
    float C10 = lerp(Grid[uint(P0.z) * SXY + uint(P1.y) * Dims.x + uint(P0.x)],
                     Grid[uint(P0.z) * SXY + uint(P1.y) * Dims.x + uint(P1.x)], F.x);
    float C01 = lerp(Grid[uint(P1.z) * SXY + uint(P0.y) * Dims.x + uint(P0.x)],
                     Grid[uint(P1.z) * SXY + uint(P0.y) * Dims.x + uint(P1.x)], F.x);
    float C11 = lerp(Grid[uint(P1.z) * SXY + uint(P1.y) * Dims.x + uint(P0.x)],
                     Grid[uint(P1.z) * SXY + uint(P1.y) * Dims.x + uint(P1.x)], F.x);
    return lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
}

float SampleRwGridTrilinear(
    RWStructuredBuffer<float> Grid,
    uint3                     Dims,
    float3                    Center,
    float3                    HalfExtents,
    float                     Spacing,
    float3                    WorldPos,
    out bool                  OutInside)
{
    float3 MinW = Center - HalfExtents;
    float3 MaxW = Center + HalfExtents;
    OutInside = all(WorldPos >= MinW) && all(WorldPos <= MaxW);
    float3 Rel = clamp((WorldPos - MinW) / max(1.0e-5, Spacing), float3(0.0), float3(Dims - uint3(1u)));
    int3 P0 = clamp(int3(floor(Rel)), int3(0), int3(Dims) - int3(1));
    int3 P1 = min(P0 + int3(1), int3(Dims) - int3(1));
    float3 F = saturate(Rel - float3(P0));
    uint SXY = Dims.x * Dims.y;

    float C00 = lerp(Grid[uint(P0.z) * SXY + uint(P0.y) * Dims.x + uint(P0.x)],
                     Grid[uint(P0.z) * SXY + uint(P0.y) * Dims.x + uint(P1.x)], F.x);
    float C10 = lerp(Grid[uint(P0.z) * SXY + uint(P1.y) * Dims.x + uint(P0.x)],
                     Grid[uint(P0.z) * SXY + uint(P1.y) * Dims.x + uint(P1.x)], F.x);
    float C01 = lerp(Grid[uint(P1.z) * SXY + uint(P0.y) * Dims.x + uint(P0.x)],
                     Grid[uint(P1.z) * SXY + uint(P0.y) * Dims.x + uint(P1.x)], F.x);
    float C11 = lerp(Grid[uint(P1.z) * SXY + uint(P1.y) * Dims.x + uint(P0.x)],
                     Grid[uint(P1.z) * SXY + uint(P1.y) * Dims.x + uint(P1.x)], F.x);
    return lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
}

float3 SampleGridFloat4Trilinear(
    StructuredBuffer<float4> Grid,
    uint3                    Dims,
    float3                   Center,
    float3                   HalfExtents,
    float                    Spacing,
    float3                   WorldPos)
{
    float3 MinW = Center - HalfExtents;
    float3 Rel = clamp((WorldPos - MinW) / max(1.0e-5, Spacing), float3(0.0), float3(Dims - uint3(1u)));
    int3 P0 = clamp(int3(floor(Rel)), int3(0), int3(Dims) - int3(1));
    int3 P1 = min(P0 + int3(1), int3(Dims) - int3(1));
    float3 F = saturate(Rel - float3(P0));
    uint SXY = Dims.x * Dims.y;

    float3 C00 = lerp(Grid[uint(P0.z) * SXY + uint(P0.y) * Dims.x + uint(P0.x)].xyz,
                      Grid[uint(P0.z) * SXY + uint(P0.y) * Dims.x + uint(P1.x)].xyz, F.x);
    float3 C10 = lerp(Grid[uint(P0.z) * SXY + uint(P1.y) * Dims.x + uint(P0.x)].xyz,
                      Grid[uint(P0.z) * SXY + uint(P1.y) * Dims.x + uint(P1.x)].xyz, F.x);
    float3 C01 = lerp(Grid[uint(P1.z) * SXY + uint(P0.y) * Dims.x + uint(P0.x)].xyz,
                      Grid[uint(P1.z) * SXY + uint(P0.y) * Dims.x + uint(P1.x)].xyz, F.x);
    float3 C11 = lerp(Grid[uint(P1.z) * SXY + uint(P1.y) * Dims.x + uint(P0.x)].xyz,
                      Grid[uint(P1.z) * SXY + uint(P1.y) * Dims.x + uint(P1.x)].xyz, F.x);
    return lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
}

float SampleDynamicLocalObjectSdf(DynamicLocalSdfGpuEntry Obj, float3 WorldPos)
{
    float3 LocalPos = float3(
        dot(Obj.WorldToLocalRow0.xyz, WorldPos) + Obj.WorldToLocalRow0.w,
        dot(Obj.WorldToLocalRow1.xyz, WorldPos) + Obj.WorldToLocalRow1.w,
        dot(Obj.WorldToLocalRow2.xyz, WorldPos) + Obj.WorldToLocalRow2.w);
    float3 MinL    = Obj.LocalMinSpacing.xyz;
    float3 MaxL    = Obj.LocalMaxPad.xyz;
    float3 Clamped = clamp(LocalPos, MinL, MaxL);
    float  OutDist = length(LocalPos - Clamped);

    uint3  LDims   = max(uint3(2u), Obj.LocalDimsOffset.xyz);
    uint   BaseOff = Obj.LocalDimsOffset.w;
    float  Spacing = max(1.0e-5, Obj.LocalMinSpacing.w);
    float3 Rel     = clamp((Clamped - MinL) / Spacing, float3(0.0), float3(LDims - uint3(1u)));
    int3   P0      = clamp(int3(floor(Rel)), int3(0), int3(LDims) - int3(1));
    int3   P1      = min(P0 + int3(1), int3(LDims) - int3(1));
    float3 F       = saturate(Rel - float3(P0));
    uint   SXY     = LDims.x * LDims.y;

    float C00 = lerp(LocalObjectSdfVoxels[BaseOff + uint(P0.z) * SXY + uint(P0.y) * LDims.x + uint(P0.x)],
                     LocalObjectSdfVoxels[BaseOff + uint(P0.z) * SXY + uint(P0.y) * LDims.x + uint(P1.x)], F.x);
    float C10 = lerp(LocalObjectSdfVoxels[BaseOff + uint(P0.z) * SXY + uint(P1.y) * LDims.x + uint(P0.x)],
                     LocalObjectSdfVoxels[BaseOff + uint(P0.z) * SXY + uint(P1.y) * LDims.x + uint(P1.x)], F.x);
    float C01 = lerp(LocalObjectSdfVoxels[BaseOff + uint(P1.z) * SXY + uint(P0.y) * LDims.x + uint(P0.x)],
                     LocalObjectSdfVoxels[BaseOff + uint(P1.z) * SXY + uint(P0.y) * LDims.x + uint(P1.x)], F.x);
    float C11 = lerp(LocalObjectSdfVoxels[BaseOff + uint(P1.z) * SXY + uint(P1.y) * LDims.x + uint(P0.x)],
                     LocalObjectSdfVoxels[BaseOff + uint(P1.z) * SXY + uint(P1.y) * LDims.x + uint(P1.x)], F.x);
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
        ActiveClip0Distance, Uniforms.Clip0Dims.xyz,
        Uniforms.Clip0CenterSpacing.xyz, Uniforms.Clip0HalfExtents.xyz, Uniforms.Clip0CenterSpacing.w, WorldPos, In0);
    if (In0)
    {
        float3 Rel0 = abs(WorldPos - Uniforms.Clip0CenterSpacing.xyz) / max(float3(1.0e-5), Uniforms.Clip0HalfExtents.xyz);
        float  M0   = max(Rel0.x, max(Rel0.y, Rel0.z));
        OutCascade   = 0u;
        OutStepScale = Uniforms.Clip0CenterSpacing.w;
        if (M0 < 0.85) return D0;
        bool In1 = false;
        float D1 = SampleGridTrilinear(
            Clip1Distance, Uniforms.Clip1Dims.xyz,
            Uniforms.Clip1CenterSpacing.xyz, Uniforms.Clip1HalfExtents.xyz, Uniforms.Clip1CenterSpacing.w, WorldPos, In1);
        if (In1)
        {
            float B = smoothstep(0.85, 0.99, M0);
            return lerp(D0, D1, B);
        }
        return D0;
    }

    bool In1 = false;
    float D1 = SampleGridTrilinear(
        Clip1Distance, Uniforms.Clip1Dims.xyz,
        Uniforms.Clip1CenterSpacing.xyz, Uniforms.Clip1HalfExtents.xyz, Uniforms.Clip1CenterSpacing.w, WorldPos, In1);
    if (In1)
    {
        OutCascade   = 1u;
        OutStepScale = Uniforms.Clip1CenterSpacing.w;
        return D1;
    }

    OutCascade   = 2u;
    OutStepScale = Uniforms.Clip1CenterSpacing.w;
    float3 Min1  = Uniforms.Clip1CenterSpacing.xyz - Uniforms.Clip1HalfExtents.xyz;
    float3 Max1  = Uniforms.Clip1CenterSpacing.xyz + Uniforms.Clip1HalfExtents.xyz;
    float3 Clp   = clamp(WorldPos, Min1, Max1);
    return D1 + length(WorldPos - Clp);
}

// Incremental dirty-AABB dynamic local object SDF union into ActiveClip0
[shader("compute")]
[numthreads(64, 1, 1)]
void ComposeDynamicLocalSdfClip0Entry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint3 Dims = Uniforms.Clip0Dims.xyz;
    uint  Idx  = DispatchThreadID.x;
    uint  Tot  = Dims.x * Dims.y * Dims.z;
    if (Dims.x < 2u || Idx >= Tot) return;

    uint SXY = Dims.x * Dims.y;
    uint IZ  = Idx / SXY;
    uint Rem = Idx - IZ * SXY;
    uint IY  = Rem / Dims.x;
    uint IX  = Rem - IY * Dims.x;

    if (int(IX) < Uniforms.DirtyMinVoxel.x || int(IX) > Uniforms.DirtyMaxVoxel.x ||
        int(IY) < Uniforms.DirtyMinVoxel.y || int(IY) > Uniforms.DirtyMaxVoxel.y ||
        int(IZ) < Uniforms.DirtyMinVoxel.z || int(IZ) > Uniforms.DirtyMaxVoxel.z)
    {
        return;
    }

    float  D        = StaticClip0Distance[Idx];
    float  Spacing  = Uniforms.Clip0CenterSpacing.w;
    float3 WorldPos = (Uniforms.Clip0CenterSpacing.xyz - Uniforms.Clip0HalfExtents.xyz)
                    + float3(float(IX), float(IY), float(IZ)) * Spacing;

    uint DynCount = Uniforms.GridParams.x;
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

// Multi-bounce Unreal-style surface cache radiosity propagation (12 deterministic Fibonacci cones)
[shader("compute")]
[numthreads(64, 1, 1)]
void PropagateClipmapRadiosityBounceEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint3 Dims = Uniforms.Clip0Dims.xyz;
    uint  Idx  = DispatchThreadID.x;
    uint  Tot  = Dims.x * Dims.y * Dims.z;
    if (Dims.x < 2u || Idx >= Tot) return;

    float4 AlbMet = SurfaceCacheAlbedoMetallic0[Idx];
    float4 NrmRgh = SurfaceCacheNormalRoughness0[Idx];
    float4 Emis   = SurfaceCacheEmissive0[Idx];
    if (NrmRgh.w < 0.0 && max(Emis.x, max(Emis.y, Emis.z)) <= 1.0e-5)
    {
        SurfaceCacheRadiosityWrite0[Idx] = float4(0.0);
        return;
    }

    uint SXY = Dims.x * Dims.y;
    uint IZ  = Idx / SXY;
    uint Rem = Idx - IZ * SXY;
    uint IY  = Rem / Dims.x;
    uint IX  = Rem - IY * Dims.x;

    float  Spacing  = Uniforms.Clip0CenterSpacing.w;
    float3 WorldPos = (Uniforms.Clip0CenterSpacing.xyz - Uniforms.Clip0HalfExtents.xyz)
                    + float3(float(IX), float(IY), float(IZ)) * Spacing;
    float3 N = normalize(NrmRgh.xyz);
    float3 T, B;
    ConstructBasis(N, T, B);

    float3 IndirectOverPi = float3(0.0);
    for (uint C = 0u; C < 12u; ++C)
    {
        float U1   = (float(C) + 0.5) / 12.0;
        float Phi  = float(C) * 2.39996323 + 0.31;
        float SinT = sqrt(clamp(U1, 0.015, 0.96));
        float CosT = sqrt(max(0.0, 1.0 - SinT * SinT));
        float3 Dir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N * CosT;

        float  Dist   = Spacing * 1.15;
        float  Trans  = 1.0;
        float3 AccumB = float3(0.0);
        for (uint Step = 0u; Step < 20u && Dist < 8.5; ++Step)
        {
            float3 SPos = WorldPos + N * (Spacing * 0.85) + Dir * Dist;
            uint   Cas  = 0u;
            float  H    = Spacing;
            float  SdfD = SampleClipmapDistance(SPos, Cas, H);
            if (Cas == 2u) break;
            float  ConeR = max(H * 0.80, Dist * 0.34);
            if (SdfD < ConeR)
            {
                float Occ = saturate((1.0 - SdfD / ConeR) * 1.05);
                float3 BounceRad = (Cas == 0u)
                    ? SampleGridFloat4Trilinear(SurfaceCacheRadiosityRead0, Uniforms.Clip0Dims.xyz, Uniforms.Clip0CenterSpacing.xyz, Uniforms.Clip0HalfExtents.xyz, Uniforms.Clip0CenterSpacing.w, SPos)
                    : SampleGridFloat4Trilinear(SurfaceCacheRadiosityRead1, Uniforms.Clip1Dims.xyz, Uniforms.Clip1CenterSpacing.xyz, Uniforms.Clip1HalfExtents.xyz, Uniforms.Clip1CenterSpacing.w, SPos);
                AccumB += min(BounceRad, float3(8.0)) * (Occ * Trans);
                Trans  *= (1.0 - Occ * 0.78);
                if (Trans < 0.04) { Trans = 0.0; break; }
            }
            Dist += max(H * 0.65, abs(SdfD) * 0.82);
        }
        IndirectOverPi += (Uniforms.SkyRadiancePad.xyz * (0.35 * Trans) + AccumB) * (1.0 / 12.0);
    }

    float3 DiffAlbedo = AlbMet.xyz * (1.0 - saturate(AlbMet.w));
    float3 PrevDirect = SurfaceCacheRadiosityRead0[Idx].xyz;
    SurfaceCacheRadiosityWrite0[Idx] = float4(max(PrevDirect, Emis.xyz + DiffAlbedo * IndirectOverPi), 1.0);
}

// Deterministic 24-cone SDF Clipmap Global Illumination pass (ZERO RNG!)
[shader("compute")]
[numthreads(8, 8, 1)]
void EvaluateClipmapConeGiEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = max(1u, Uniforms.GridParams.z);
    uint Height = max(1u, Uniforms.GridParams.w);
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

    float  Spacing0 = Uniforms.Clip0CenterSpacing.w;
    float3 Origin   = PosW.xyz + N * max(0.032, Spacing0 * 0.75);
    float3 Accum    = float3(0.0);

    for (uint C = 0u; C < 24u; ++C)
    {
        float U1   = (float(C) + 0.5) / 24.0;
        float Phi  = float(C) * 2.39996323 + 0.31;
        float SinT = sqrt(clamp(U1, 0.015, 0.96));
        float CosT = sqrt(max(0.0, 1.0 - SinT * SinT));
        float3 Dir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N * CosT;

        float  Dist   = max(0.055, Spacing0 * 0.90);
        float  Trans  = 1.0;
        float3 Bounce = float3(0.0);
        for (uint Step = 0u; Step < 24u && Dist < 12.0; ++Step)
        {
            float3 SPos = Origin + Dir * Dist;
            uint   Cas  = 0u;
            float  H    = Spacing0;
            float  SdfD = SampleClipmapDistance(SPos, Cas, H);
            if (Cas == 2u) break;
            float  ConeR = max(H * 0.85, Dist * 0.32);
            if (SdfD < ConeR)
            {
                float Occ = saturate((1.0 - SdfD / ConeR) * ((Dist < 0.85) ? 1.20 : 1.0));
                float3 VRad0 = SampleGridFloat4Trilinear(SurfaceCacheRadiosityRead0, Uniforms.Clip0Dims.xyz, Uniforms.Clip0CenterSpacing.xyz, Uniforms.Clip0HalfExtents.xyz, Uniforms.Clip0CenterSpacing.w, SPos);
                float3 VRad1 = SampleGridFloat4Trilinear(SurfaceCacheRadiosityRead1, Uniforms.Clip1Dims.xyz, Uniforms.Clip1CenterSpacing.xyz, Uniforms.Clip1HalfExtents.xyz, Uniforms.Clip1CenterSpacing.w, SPos);
                float  MipW  = saturate((ConeR - H * 1.1) / max(0.04, H * 2.2));
                float3 VRad  = min((Cas == 0u) ? lerp(VRad0, VRad1, MipW * 0.55) : VRad1, float3(8.0));
                Bounce += VRad * (Occ * Trans);
                Trans  *= (1.0 - Occ * 0.76);
                if (Trans < 0.035) { Trans = 0.0; break; }
            }
            Dist += max(H * 0.68, abs(SdfD) * 0.78);
        }
        float SkyHem = 0.5 + 0.5 * Dir.z;
        float3 SkyAmb = Uniforms.SkyRadiancePad.xyz * (0.45 + 0.85 * SkyHem);
        Accum += (SkyAmb * Trans + Bounce) * (1.0 / 24.0);
    }

    OutClipmapConeGiOverPi[DispatchThreadID.xy] = float4(Accum, 1.0);
}
