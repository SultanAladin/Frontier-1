/*============================================================================================================================================
                                                         SPECULARGLASSRAYTRACE.SLANG
============================================================================================================================================*/
// 🧩 Tier 2 RTX Coherent VNDF Specular Reflection & Abbe-Dispersed Dielectric Glass Refraction Compute Pass

module SpecularGlassRaytrace;

import WideTreeRayQuery;
import BounceLayerSummary;

struct SpecularRaytraceUniforms
{
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   BvhEntryCount;
    uint   FrameIndex;
    float3 CameraOrigin;
    float  Pad1;
    float3 SunDirection;
    float  Pad2;
    float3 SolarIrradiance;
    float  Pad3;
    float3 SkyRadiance;
    float  Pad4;
};

[[vk::binding(10, 0)]] ConstantBuffer<SpecularRaytraceUniforms>      Uniforms;
[[vk::binding(11, 0)]] StructuredBuffer<float4>                      PrimaryHitPositions; // xyz = WorldPos, w = Roughness (<0 sky)
[[vk::binding(12, 0)]] StructuredBuffer<float4>                      PrimaryHitNormals;   // xyz = WorldNrm, w = IOR (>1.01 glass)
[[vk::binding(13, 0)]] StructuredBuffer<GpuWideTreeEntry128>         SceneBvhEntries;
[[vk::binding(14, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>   SceneTriangles;
[[vk::binding(15, 0)]] StructuredBuffer<GpuBounceSlabEntry>          SlabSummaries;
[[vk::binding(16, 0)]] RWTexture2D<float4>                           OutSpecularGlass;

float3 ShadeSecondaryRay(float3 Origin, float3 Dir)
{
    GpuRayHitSummary Hit = TraceWideBoundingTreeClosest(
        SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount, Origin, Dir, 1.5e-3, 90.0);
    if (!Hit.HitConfirmed)
    {
        float Up = saturate(Dir.z * 0.5 + 0.5);
        return Uniforms.SkyRadiance * (0.65 + 0.70 * Up);
    }

    float3 HitPos = Origin + Dir * Hit.HitDistanceMeters;
    float3 HitNrm = (dot(Hit.GeometricNormal, Dir) < 0.0) ? Hit.GeometricNormal : -Hit.GeometricNormal;
    if (dot(HitNrm, Uniforms.SunDirection) > 0.0)
    {
        GpuRayHitSummary SunCheck = TraceWideBoundingTreeClosest(
            SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount, HitPos + HitNrm * 2.5e-3, Uniforms.SunDirection, 1.0e-3, 120.0);
        if (!SunCheck.HitConfirmed)
        {
            return EvaluateBounceSlabSurface(
                SlabSummaries[Hit.MaterialOrdinal], HitNrm, -Dir, Uniforms.SunDirection, Uniforms.SolarIrradiance);
        }
    }
    return float3(0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SpecularRaytraceComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= Uniforms.ViewportWidth || DispatchThreadID.y >= Uniforms.ViewportHeight)
    {
        return;
    }

    uint   PixelIdx = DispatchThreadID.y * Uniforms.ViewportWidth + DispatchThreadID.x;
    float4 PosRough = PrimaryHitPositions[PixelIdx];
    if (PosRough.w < 0.0)
    {
        OutSpecularGlass[DispatchThreadID.xy] = float4(0.0);
        return;
    }

    float4 NrmIor  = PrimaryHitNormals[PixelIdx];
    float3 Nrm     = normalize(NrmIor.xyz);
    float3 ViewDir = normalize(Uniforms.CameraOrigin - PosRough.xyz);
    float3 ReflDir = reflect(-ViewDir, Nrm);

    float3 SpecRad = ShadeSecondaryRay(PosRough.xyz + Nrm * 2.5e-3, ReflDir);

    // Solid Abbe-dispersed refraction when glass IOR > 1.01 encoded in NrmIor.w
    if (NrmIor.w > 1.01)
    {
        float  CosI    = saturate(dot(Nrm, ViewDir));
        float  F0      = pow((NrmIor.w - 1.0) / (NrmIor.w + 1.0), 2.0);
        float  Fresnel = F0 + (1.0 - F0) * pow(1.0 - CosI, 5.0);
        float3 RefrR   = refract(-ViewDir, Nrm, 1.0 / (NrmIor.w - 0.012));
        float3 RefrB   = refract(-ViewDir, Nrm, 1.0 / (NrmIor.w + 0.015));
        float  RR      = ShadeSecondaryRay(PosRough.xyz - Nrm * 3.0e-3, RefrR).r;
        float  RG      = ShadeSecondaryRay(PosRough.xyz - Nrm * 3.0e-3, refract(-ViewDir, Nrm, 1.0 / NrmIor.w)).g;
        float  RB      = ShadeSecondaryRay(PosRough.xyz - Nrm * 3.0e-3, RefrB).b;
        SpecRad        = lerp(float3(RR, RG, RB), SpecRad, Fresnel);
    }

    OutSpecularGlass[DispatchThreadID.xy] = float4(SpecRad, 1.0);
}
