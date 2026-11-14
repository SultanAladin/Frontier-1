/*============================================================================================================================================
                                                     PARTICLELENSRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 optical gravitational-lens Einstein ring & thermal heat-shimmer screen distortion carrier

import ParticleBuffers;

struct LensVertexOut
{
    float4 ClipPosition : SV_Position;
    float2 LocalCoord   : TEXCOORD0;
    float4 TintLinear   : COLOR0;
    float  PhaseSec     : TEXCOORD1;
};

[[vk::binding(0, 1)]] StructuredBuffer<ParticleSlotGpu>        ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;

[shader("vertex")]
LensVertexOut ParticleLensVertexEntry(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    LensVertexOut Out;
    uint SlotIdx = RigParams.SlotOffset + InstanceId;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];

    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.LocalCoord   = float2(0.0, 0.0);
        Out.TintLinear   = float4(0.0, 0.0, 0.0, 0.0);
        Out.PhaseSec     = 0.0;
        return Out;
    }

    static const float2 Quad[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    float2 C = Quad[VertexId % 6u];
    float3 WPos = Slot.PositionMeters
                + GlobalParams.CameraRightMeters.xyz * (C.x * Slot.SizeMeters)
                + GlobalParams.CameraUpMeters.xyz    * (C.y * Slot.SizeMeters);

    Out.ClipPosition = mul(GlobalParams.ViewProjectionMatrix, float4(WPos, 1.0));
    Out.LocalCoord   = C;
    Out.TintLinear   = Slot.ColourLinear;
    Out.PhaseSec     = GlobalParams.ElapsedSeconds + Slot.SeedScalar * 10.0;
    return Out;
}

[shader("fragment")]
float4 ParticleLensFragmentEntry(LensVertexOut Input) : SV_Target
{
    float R = length(Input.LocalCoord);
    if (R > 1.0) discard;

    // Einstein gravitational lensing ring + dark photon-sphere core
    float Ring = exp(-pow((R - 0.56) * 11.0, 2.0));
    float Shimmer = 0.5 + 0.5 * sin(Input.LocalCoord.x * 14.0 + Input.PhaseSec * 6.0) * cos(Input.LocalCoord.y * 14.0 - Input.PhaseSec * 5.0);
    float CoreOcclusion = smoothstep(0.42, 0.34, R);

    float3 Col = lerp(Input.TintLinear.rgb * (Ring * 2.2 + Shimmer * 0.35), float3(0.01, 0.01, 0.02), CoreOcclusion);
    float Alpha = saturate(Ring * 0.95 + CoreOcclusion * 0.98 + (1.0 - R) * 0.15) * Input.TintLinear.a;
    return float4(Col, Alpha);
}
