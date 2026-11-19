/*============================================================================================================================================
                                                       ANALYTICBARYCENTRICRESOLVE.SLANG
============================================================================================================================================*/
// 🧩 Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & Wave-Uniform OpenPBR Shade

module AnalyticBarycentricResolve;

import OpenPbrSurfaceScattering;

struct ResolveCameraUniforms
{
    float3 CurOrigin;
    float  CurTanHalfFovX;
    float3 CurRight;
    float  CurTanHalfFovY;
    float3 CurUp;
    uint   ViewportWidth;
    float3 CurForward;
    uint   ViewportHeight;

    float3 PrevOrigin;
    float  PrevTanHalfFovX;
    float3 PrevRight;
    float  PrevTanHalfFovY;
    float3 PrevUp;
    float  ReservedA;
    float3 PrevForward;
    float  ReservedB;
};

struct ResolveTriangleEntry
{
    float3 WorldP0;
    uint   MaterialSlot;
    float3 WorldP1;
    uint   PackedNormal0;
    float3 WorldP2;
    uint   PackedNormal1;
    float3 PrevWorldP0;
    uint   PackedNormal2;
    float3 PrevWorldP1;
    uint   ReservedPad0;
    float3 PrevWorldP2;
    uint   ReservedPad1;
    float2 UV0;
    float2 UV1;
    float2 UV2;
    float2 ReservedUV;
};

[[vk::binding(10, 0)]] ConstantBuffer<ResolveCameraUniforms>       ResolveCam;
[[vk::binding(11, 0)]] StructuredBuffer<uint64_t>                  OpaqueVisibility64;
[[vk::binding(12, 0)]] StructuredBuffer<ResolveTriangleEntry>     TriangleTable;
[[vk::binding(13, 0)]] StructuredBuffer<GpuCompactMaterialEntry>   MaterialTable;
[[vk::binding(14, 0)]] RWTexture2D<float4>                         ResolvedColorHdr;
[[vk::binding(15, 0)]] RWTexture2D<float2>                         MotionVectorsOut;

float3 UnpackOctNormal32(uint Word)
{
    int RawX = int(Word << 16) >> 16;
    int RawY = int(Word) >> 16;
    float2 Oct = clamp(float2(RawX, RawY) / 32767.0, float2(-1.0), float2(1.0));
    float3 Dir = float3(Oct.x, Oct.y, 1.0 - abs(Oct.x) - abs(Oct.y));
    if (Dir.z < 0.0)
    {
        float2 Fold = (1.0 - abs(Dir.yx)) * float2((Dir.x >= 0.0) ? 1.0 : -1.0, (Dir.y >= 0.0) ? 1.0 : -1.0);
        Dir.xy = Fold;
    }
    return normalize(Dir);
}

float3 PrimaryRay(float2 Subpixel)
{
    float NdcX = (Subpixel.x / float(ResolveCam.ViewportWidth)) * 2.0 - 1.0;
    float NdcY = 1.0 - (Subpixel.y / float(ResolveCam.ViewportHeight)) * 2.0;
    return normalize(ResolveCam.CurForward +
                     ResolveCam.CurRight * (NdcX * ResolveCam.CurTanHalfFovX) +
                     ResolveCam.CurUp * (NdcY * ResolveCam.CurTanHalfFovY));
}

float3 RayPlaneBarycentrics(float3 Origin, float3 Dir, float3 P0, float3 E1, float3 E2)
{
    float3 PVec = cross(Dir, E2);
    float  Det  = dot(E1, PVec);
    if (abs(Det) <= 1.0e-10) return float3(1.0 / 3.0);
    float  InvD = 1.0 / Det;
    float3 TVec = Origin - P0;
    float  U    = dot(TVec, PVec) * InvD;
    float  V    = dot(Dir, cross(TVec, E1)) * InvD;
    return float3(1.0 - U - V, U, V);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void AnalyticResolveComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= ResolveCam.ViewportWidth || DispatchThreadID.y >= ResolveCam.ViewportHeight)
    {
        return;
    }

    uint PixOff    = DispatchThreadID.y * ResolveCam.ViewportWidth + DispatchThreadID.x;
    uint64_t Vis64 = OpaqueVisibility64[PixOff];
    uint Token32   = uint(Vis64 & 0xFFFFFFFFull);
    if (Token32 == 0u)
    {
        ResolvedColorHdr[DispatchThreadID.xy] = float4(0.05, 0.08, 0.14, 1.0);
        MotionVectorsOut[DispatchThreadID.xy] = float2(0.0, 0.0);
        return;
    }

    uint TriIndex = Token32 & 0x1FFFFFu;
    ResolveTriangleEntry Rec = TriangleTable[TriIndex];
    GpuCompactMaterialEntry MatEntry = MaterialTable[Rec.MaterialSlot];

    // Wavefront-coherent lobe mask union across all active threads in the 8x8 tile!
    uint WaveCombinedLobeMask = WaveActiveBitOr(MatEntry.ActiveLobeMask);

    float2 Center = float2(DispatchThreadID.xy) + 0.5;
    float3 E1     = Rec.WorldP1 - Rec.WorldP0;
    float3 E2     = Rec.WorldP2 - Rec.WorldP0;

    float3 RayC  = PrimaryRay(Center);
    float3 BaryC = RayPlaneBarycentrics(ResolveCam.CurOrigin, RayC, Rec.WorldP0, E1, E2);
    float3 BaryX = RayPlaneBarycentrics(ResolveCam.CurOrigin, PrimaryRay(Center + float2(1.0, 0.0)), Rec.WorldP0, E1, E2);
    float3 BaryY = RayPlaneBarycentrics(ResolveCam.CurOrigin, PrimaryRay(Center + float2(0.0, 1.0)), Rec.WorldP0, E1, E2);

    float2 UvC  = Rec.UV0 * BaryC.x + Rec.UV1 * BaryC.y + Rec.UV2 * BaryC.z;
    float2 DuDx = (Rec.UV0 * BaryX.x + Rec.UV1 * BaryX.y + Rec.UV2 * BaryX.z) - UvC;
    float2 DuDy = (Rec.UV0 * BaryY.x + Rec.UV1 * BaryY.y + Rec.UV2 * BaryY.z) - UvC;

    float3 Nrm = normalize(
        UnpackOctNormal32(Rec.PackedNormal0) * BaryC.x +
        UnpackOctNormal32(Rec.PackedNormal1) * BaryC.y +
        UnpackOctNormal32(Rec.PackedNormal2) * BaryC.z);
    float3 UpRef = (abs(Nrm.z) < 0.999) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 TanU  = normalize(cross(UpRef, Nrm));
    float3 BitV  = cross(Nrm, TanU);

    float3 ViewWorld  = -RayC;
    float3 LightWorld = normalize(float3(-0.48, -0.58, 0.66));
    float3 LocalWo    = float3(dot(ViewWorld, TanU), dot(ViewWorld, BitV), max(1.0e-4, dot(ViewWorld, Nrm)));
    float3 LocalWi    = float3(dot(LightWorld, TanU), dot(LightWorld, BitV), dot(LightWorld, Nrm));

    float3 Radiance = float3(0.0);
    if (LocalWi.z > 0.0)
    {
        Radiance = EvaluateCompactOpenPbrBsdf(MatEntry, WaveCombinedLobeMask, LocalWo, LocalWi, UvC, DuDx, DuDy) * (3.4 * LocalWi.z);
    }

    // Differential sub-pixel motion vector
    float3 PrevWorld = Rec.PrevWorldP0 * BaryC.x + Rec.PrevWorldP1 * BaryC.y + Rec.PrevWorldP2 * BaryC.z;
    float3 PrevRel   = PrevWorld - ResolveCam.PrevOrigin;
    float  PrevZ     = max(1.0e-4, dot(PrevRel, ResolveCam.PrevForward));
    float2 PrevNdc   = float2(dot(PrevRel, ResolveCam.PrevRight) / (PrevZ * ResolveCam.PrevTanHalfFovX),
                              dot(PrevRel, ResolveCam.PrevUp) / (PrevZ * ResolveCam.PrevTanHalfFovY));
    float2 PrevPx    = float2((PrevNdc.x * 0.5 + 0.5) * float(ResolveCam.ViewportWidth),
                              (0.5 - PrevNdc.y * 0.5) * float(ResolveCam.ViewportHeight));

    ResolvedColorHdr[DispatchThreadID.xy] = float4(Radiance, 1.0);
    MotionVectorsOut[DispatchThreadID.xy] = Center - PrevPx;
}
