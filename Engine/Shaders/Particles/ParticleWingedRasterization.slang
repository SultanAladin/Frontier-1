/*============================================================================================================================================
                                                    PARTICLEWINGEDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 articulated dual-wing flapping carrier for birds, starlings, butterflies, and sinuous swimming fish schools

import ParticleBuffers;

struct WingVertexOut
{
    float4 ClipPosition : SV_Position;
    float2 LocalUv      : TEXCOORD0;
    float4 TintLinear   : COLOR0;
    float  WingSide     : TEXCOORD1;
};

[[vk::binding(0, 1)]] StructuredBuffer<ParticleSlotGpu>        ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;

[shader("vertex")]
WingVertexOut ParticleWingedVertexEntry(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    WingVertexOut Out;
    uint SlotIdx = RigParams.SlotOffset + InstanceId;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];

    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.LocalUv      = float2(0.0, 0.0);
        Out.TintLinear   = float4(0.0, 0.0, 0.0, 0.0);
        Out.WingSide     = 0.0;
        return Out;
    }

    // Two articulated triangular wings (6 verts: 0..2 left wing, 3..5 right wing)
    static const float2 WingCoords[6] = {
        float2( 0.0,  0.55), float2(-1.0,  0.15), float2( 0.0, -0.55),
        float2( 0.0,  0.55), float2( 0.0, -0.55), float2( 1.0,  0.15)
    };
    float2 C = WingCoords[VertexId % 6u];
    float Side = (VertexId < 3u) ? -1.0 : 1.0;

    float3 Vel = Slot.VelocityMps;
    float  Spd = length(Vel);
    float3 Fwd = (Spd > 0.02) ? (Vel / Spd) : float3(0.0, 0.0, 1.0);
    float3 Up  = float3(0.0, 1.0, 0.0);
    float3 Right = normalize(cross(Up, Fwd) + float3(1e-4, 0.0, 0.0));
    Up = normalize(cross(Fwd, Right));

    // Sinusoidal wingbeat modulated by particle seed & elapsed time
    float FlapAngle = sin(GlobalParams.ElapsedSeconds * 13.5 + Slot.SeedScalar * 18.0) * 0.78;
    float SpanX = abs(C.x);
    float3 LocalOffset = Right * (C.x * cos(FlapAngle))
                       + Up    * (SpanX * sin(FlapAngle) * 0.85)
                       + Fwd   * C.y;

    float3 WPos = Slot.PositionMeters + LocalOffset * (Slot.SizeMeters * 1.45);
    Out.ClipPosition = mul(GlobalParams.ViewProjectionMatrix, float4(WPos, 1.0));
    Out.LocalUv      = float2(SpanX, C.y * 0.5 + 0.5);
    Out.TintLinear   = Slot.ColourLinear;
    Out.WingSide     = Side;
    return Out;
}

[shader("fragment")]
float4 ParticleWingedFragmentEntry(WingVertexOut Input) : SV_Target
{
    // Subtle iridescent vein pattern on wing tips
    float Vein = 0.78 + 0.22 * sin(Input.LocalUv.x * 16.0 + Input.LocalUv.y * 9.0);
    float EdgeFade = smoothstep(1.0, 0.75, Input.LocalUv.x);
    return float4(Input.TintLinear.rgb * Vein, Input.TintLinear.a * max(0.35, EdgeFade));
}
