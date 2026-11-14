/*============================================================================================================================================
                                                    PARTICLELINEARCRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 glowing dielectric lightning arcs, plasma beams, & silk spiderweb tension strands with radial core corona falloff

import ParticleBuffers;

struct LineArcVertexOut
{
    float4 ClipPosition : SV_Position;
    float2 SegmentUv    : TEXCOORD0;
    float4 TintLinear   : COLOR0;
    float  Intensity    : TEXCOORD1;
};

[[vk::binding(0, 1)]] StructuredBuffer<ParticleSlotGpu>        ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;

[shader("vertex")]
LineArcVertexOut ParticleLineArcVertexEntry(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    LineArcVertexOut Out;
    uint SlotIdx  = RigParams.SlotOffset + InstanceId;
    uint NextIdx  = RigParams.SlotOffset + min(InstanceId + 1u, RigParams.SlotCapacity - 1u);
    ParticleSlotGpu A = ParticleSlots[SlotIdx];
    ParticleSlotGpu B = ParticleSlots[NextIdx];

    if ((A.PackedTraits & ParticleTraitAliveBit) == 0u || (B.PackedTraits & ParticleTraitAliveBit) == 0u)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.SegmentUv    = float2(0.0, 0.0);
        Out.TintLinear   = float4(0.0, 0.0, 0.0, 0.0);
        Out.Intensity    = 0.0;
        return Out;
    }

    static const float2 QuadUv[6] = {
        float2(0.0, -1.0), float2(1.0, -1.0), float2(1.0,  1.0),
        float2(0.0, -1.0), float2(1.0,  1.0), float2(0.0,  1.0)
    };
    float2 Uv = QuadUv[VertexId % 6u];

    float3 Seg   = B.PositionMeters - A.PositionMeters;
    float3 Mid   = 0.5 * (A.PositionMeters + B.PositionMeters);
    float3 ViewV = normalize(GlobalParams.CameraPositionMeters.xyz - Mid);
    float3 Side  = normalize(cross(Seg + float3(0.0, 1e-4, 0.0), ViewV));

    float Width = max(0.008, A.SizeMeters * 0.45);
    float3 WPos = lerp(A.PositionMeters, B.PositionMeters, Uv.x) + Side * (Uv.y * Width);

    Out.ClipPosition = mul(GlobalParams.ViewProjectionMatrix, float4(WPos, 1.0));
    Out.SegmentUv    = float2(Uv.x, Uv.y * 0.5 + 0.5);
    Out.TintLinear   = lerp(A.ColourLinear, B.ColourLinear, Uv.x);
    Out.Intensity    = 1.0 + 1.8 * A.AuxScalar;
    return Out;
}

[shader("fragment")]
float4 ParticleLineArcFragmentEntry(LineArcVertexOut Input) : SV_Target
{
    float Radial = abs(Input.SegmentUv.y * 2.0 - 1.0);
    float InnerBeam = exp(-Radial * Radial * 8.5);
    float Halo      = exp(-Radial * 2.4) * 0.55;
    float3 Col      = Input.TintLinear.rgb * (Halo + InnerBeam * Input.Intensity) + float3(InnerBeam * 0.65);
    return float4(Col, saturate((InnerBeam + Halo) * Input.TintLinear.a));
}
