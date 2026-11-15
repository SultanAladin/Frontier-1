/*============================================================================================================================================
                                                           BOUNCELAYERSUMMARY.SLANG
============================================================================================================================================*/
// 🧩 Fast 5-ALU 32-byte BounceSlabEntry evaluation for secondary IrradianceLattice & multi-bounce specular rays

module BounceLayerSummary;

public struct GpuBounceSlabEntry
{
    public uint  DiffuseAlbedoRgHalf;            // [fp16] - R (lo16), G (hi16)
    public uint  DiffuseAlbedoBAndRoughnessHalf; // [fp16] - B (lo16), EffectiveRoughness (hi16)
    public uint  SpecularF0RgHalf;               // [fp16] - R (lo16), G (hi16)
    public uint  SpecularF0BAndMetalnessHalf;    // [fp16] - B (lo16), Metalness (hi16)
    public uint  EmissiveExitanceRgHalf;         // [fp16] - R (lo16), G (hi16)
    public uint  EmissiveExitanceBAndTransHalf;  // [fp16] - B (lo16), TransmissionFraction (hi16)
    public float MeanHemisphericalReflectance;   // [-]    - Total scalar hemispherical reflectance
    public uint  SummaryFlags;                   // [bits] - bit0 = Transmissive, bit1 = Emissive
};

float2 UnpackHalfPair(uint Word)
{
    return float2(f16tof32(Word & 0xFFFFu), f16tof32((Word >> 16u) & 0xFFFFu));
}

public float3 EvaluateBounceSlabSurface(
    GpuBounceSlabEntry Summary,
    float3             SurfaceNormal,
    float3             OutgoingDirection,
    float3             IncidentLightDirection,
    float3             IncidentRadiance)
{
    float2 DiffRg = UnpackHalfPair(Summary.DiffuseAlbedoRgHalf);
    float2 DiffBr = UnpackHalfPair(Summary.DiffuseAlbedoBAndRoughnessHalf);
    float2 SpecRg = UnpackHalfPair(Summary.SpecularF0RgHalf);
    float2 SpecBm = UnpackHalfPair(Summary.SpecularF0BAndMetalnessHalf);
    float2 EmisRg = UnpackHalfPair(Summary.EmissiveExitanceRgHalf);
    float2 EmisBt = UnpackHalfPair(Summary.EmissiveExitanceBAndTransHalf);

    float3 DiffAlbedo = float3(DiffRg.x, DiffRg.y, DiffBr.x);
    float3 SpecF0     = float3(SpecRg.x, SpecRg.y, SpecBm.x);
    float3 Emissive   = float3(EmisRg.x, EmisRg.y, EmisBt.x);

    float NdotL = max(0.0, dot(SurfaceNormal, IncidentLightDirection));
    if (NdotL <= 1.0e-6)
    {
        return Emissive;
    }

    float3 HalfVec = normalize(OutgoingDirection + IncidentLightDirection);
    float NdotH    = max(0.0, dot(SurfaceNormal, HalfVec));
    float VdotH    = saturate(dot(OutgoingDirection, HalfVec));
    float Rough2   = DiffBr.y * DiffBr.y;
    float AlphaSq  = max(1.0e-3, Rough2 * Rough2);
    float Denom    = NdotH * NdotH * (AlphaSq - 1.0) + 1.0;
    float SpecLobe = AlphaSq / max(1.0e-5, 12.5663706 * Denom * Denom);
    float3 Fresnel = SpecF0 + (float3(1.0) - SpecF0) * pow(1.0 - VdotH, 5.0);

    return Emissive + (DiffAlbedo * 0.318309886 * (float3(1.0) - SpecF0) + Fresnel * SpecLobe) * IncidentRadiance * NdotL;
}

struct BounceVerificationRequest
{
    uint   MaterialSlot;
    float3 Normal;
    float3 OutgoingDir;
    float3 LightDir;
};

[[vk::binding(0, 0)]] StructuredBuffer<GpuBounceSlabEntry>        BounceSummaries;
[[vk::binding(1, 0)]] StructuredBuffer<BounceVerificationRequest> Requests;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>                  EvaluatedRadiance;

[shader("compute")]
[numthreads(64, 1, 1)]
void BounceSummaryComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Count = 0u, Stride = 0u;
    Requests.GetDimensions(Count, Stride);
    if (DispatchThreadID.x >= Count)
    {
        return;
    }

    BounceVerificationRequest Req = Requests[DispatchThreadID.x];
    GpuBounceSlabEntry Summary    = BounceSummaries[Req.MaterialSlot];
    float3 Radiance               = EvaluateBounceSlabSurface(Summary, Req.Normal, Req.OutgoingDir, Req.LightDir, float3(1.0));
    EvaluatedRadiance[DispatchThreadID.x] = float4(Radiance, Summary.MeanHemisphericalReflectance);
}
