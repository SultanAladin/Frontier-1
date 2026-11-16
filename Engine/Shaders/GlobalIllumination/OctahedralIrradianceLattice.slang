/*============================================================================================================================================
                                                      OCTAHEDRALIRRADIANCELATTICE.SLANG
============================================================================================================================================*/
// 🧩 Tier 2 RTX 1/pi-Normalized 8x8 Octahedral Irradiance + 16x16 Chebyshev Radial Depth Lattice Update via 64 Spherical Fibonacci Rays

module OctahedralIrradianceLattice;

import WideTreeRayQuery;
import BounceLayerSummary;

struct LatticeDispatchUniforms
{
    uint   CellCount;          // [cnt]   - Total 3D lattice cells
    uint   FibonacciRayCount;  // [cnt]   - Spherical Fibonacci rays per cell (default 64)
    uint   BvhEntryCount;      // [cnt]   - Active 128 B 4-wide BVH entries
    float  MaxDepthClamp;      // [m]     - Clamp for 16x16 Chebyshev depth moments
    float3 SunDirection;       // [-]     - Unit vector toward solar emitter
    float  TemporalHysteresis; // [0..1]  - Exponential blend factor
    float3 SolarIrradiance;    // [W/m^2] - Direct solar spectral irradiance
    float  Pad1;
    float3 SkyRadiance;        // [W/sr]  - Ambient sky radiance
    float  Pad2;
};

[[vk::binding(10, 0)]] ConstantBuffer<LatticeDispatchUniforms>       Uniforms;
[[vk::binding(11, 0)]] StructuredBuffer<float4>                      CellWorldPositions;
[[vk::binding(12, 0)]] StructuredBuffer<GpuWideTreeEntry128>         SceneBvhEntries;
[[vk::binding(13, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>   SceneTriangles;
[[vk::binding(14, 0)]] StructuredBuffer<GpuBounceSlabEntry>          SlabSummaries;
[[vk::binding(15, 0)]] RWStructuredBuffer<float4>                    OutIrradianceOverPi; // [CellCount * 64]
[[vk::binding(16, 0)]] RWStructuredBuffer<float2>                    OutChebyshevMoments; // [CellCount * 256]

float3 DecodeUnitOctahedral(float2 Uv)
{
    float2 Oct = Uv * 2.0 - 1.0;
    float3 Dir = float3(Oct.x, Oct.y, 1.0 - abs(Oct.x) - abs(Oct.y));
    if (Dir.z < 0.0)
    {
        float2 Fold = (1.0 - abs(Dir.yx)) * select(Dir.xy >= 0.0, float2(1.0), float2(-1.0));
        Dir.xy      = Fold;
    }
    return normalize(Dir);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void IrradianceLatticeComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint CellIdx = DispatchThreadID.x;
    if (CellIdx >= Uniforms.CellCount)
    {
        return;
    }

    float3 Origin  = CellWorldPositions[CellIdx].xyz;
    uint   RayCnt  = clamp(Uniforms.FibonacciRayCount, 16u, 64u);
    float3 RayDirs[64];
    float3 RayRads[64];
    float  RayDist[64];

    for (uint R = 0u; R < RayCnt; ++R)
    {
        float Z    = 1.0 - (2.0 * (float(R) + 0.5)) / float(RayCnt);
        float Rad  = sqrt(max(0.0, 1.0 - Z * Z));
        float Phi  = 6.2831853 * frac(float(R) * 0.61803398875);
        float3 Dir = float3(Rad * cos(Phi), Rad * sin(Phi), Z);
        RayDirs[R] = Dir;

        GpuRayHitSummary Hit = TraceWideBoundingTreeClosest(
            SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount, Origin, Dir, 1.0e-3, 60.0);
        if (!Hit.HitConfirmed)
        {
            RayRads[R] = Uniforms.SkyRadiance;
            RayDist[R] = Uniforms.MaxDepthClamp;
        }
        else
        {
            RayDist[R]     = min(Uniforms.MaxDepthClamp, Hit.HitDistanceMeters);
            float3 HitPos  = Origin + Dir * Hit.HitDistanceMeters;
            float3 HitNrm  = (dot(Hit.GeometricNormal, Dir) < 0.0) ? Hit.GeometricNormal : -Hit.GeometricNormal;
            float3 ExitRad = float3(0.0);
            if (dot(HitNrm, Uniforms.SunDirection) > 0.0)
            {
                GpuRayHitSummary SunCheck = TraceWideBoundingTreeClosest(
                    SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount, HitPos + HitNrm * 2.5e-3, Uniforms.SunDirection, 1.0e-3, 120.0);
                if (!SunCheck.HitConfirmed)
                {
                    ExitRad = EvaluateBounceSlabSurface(
                        SlabSummaries[Hit.MaterialOrdinal], HitNrm, -Dir, Uniforms.SunDirection, Uniforms.SolarIrradiance);
                }
            }
            RayRads[R] = ExitRad;
        }
    }

    // Convolve 8x8 1/pi-normalized clamped-cosine irradiance
    for (uint Tex = 0u; Tex < 64u; ++Tex)
    {
        float2 Uv  = (float2(float(Tex & 7u), float(Tex >> 3u)) + 0.5) * 0.125;
        float3 Nrm = DecodeUnitOctahedral(Uv);
        float3 Sum = float3(0.0);
        float  Wgt = 0.0;
        for (uint R = 0u; R < RayCnt; ++R)
        {
            float CosT = max(0.0, dot(Nrm, RayDirs[R]));
            Sum += RayRads[R] * CosT;
            Wgt += CosT;
        }
        float3 NewIrr  = (Wgt > 1.0e-5) ? (Sum / Wgt) : float3(0.0);
        uint   OutSlot = CellIdx * 64u + Tex;
        float3 PrevIrr = OutIrradianceOverPi[OutSlot].xyz;
        OutIrradianceOverPi[OutSlot] = float4(lerp(NewIrr, PrevIrr, Uniforms.TemporalHysteresis), 1.0);
    }

    // Convolve 16x16 Chebyshev radial distance moments (MeanR, MeanR^2)
    for (uint DTex = 0u; DTex < 256u; ++DTex)
    {
        float2 Uv  = (float2(float(DTex & 15u), float(DTex >> 4u)) + 0.5) * 0.0625;
        float3 Dir = DecodeUnitOctahedral(Uv);
        float  SR  = 0.0;
        float  SR2 = 0.0;
        float  SW  = 0.0;
        for (uint R = 0u; R < RayCnt; ++R)
        {
            float Align = max(0.0, dot(Dir, RayDirs[R]));
            if (Align > 0.0)
            {
                float W = pow(Align, 16.0);
                SR  += RayDist[R] * W;
                SR2 += (RayDist[R] * RayDist[R]) * W;
                SW  += W;
            }
        }
        OutChebyshevMoments[CellIdx * 256u + DTex] = (SW > 1.0e-6)
            ? float2(SR / SW, SR2 / SW)
            : float2(Uniforms.MaxDepthClamp, Uniforms.MaxDepthClamp * Uniforms.MaxDepthClamp);
    }
}
