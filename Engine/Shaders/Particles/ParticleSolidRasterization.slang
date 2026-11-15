/*============================================================================================================================================
                                                       PARTICLESOLIDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 PBR-shaded instanced 3D solid particle carrier (coins, cubes, glass shards, snowflakes, crystal beads, Voronoi ice/basalt)

import ParticleBuffers;

struct SolidVertexIn
{
    float3 LocalPos    : POSITION;
    float3 LocalNormal : NORMAL;
};

struct SolidVertexOut
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : NORMAL;
    float3 WorldPos     : TEXCOORD0;
    float4 TintLinear   : COLOR0;
    float  Metallic     : TEXCOORD1;
    float  Roughness    : TEXCOORD2;
};

[[vk::binding(0, 1)]] StructuredBuffer<ParticleSlotGpu>        ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;

float3x3 BuildSpinMatrix(float Angle, float Seed)
{
    float C = cos(Angle), S = sin(Angle);
    float T = Seed * 6.2831853;
    float Ct = cos(T), St = sin(T);
    return float3x3(
        C * Ct, -S, C * St,
        S * Ct,  C, S * St,
        -St,     0, Ct);
}

[shader("vertex")]
SolidVertexOut ParticleSolidVertexEntry(SolidVertexIn Input, uint InstanceId : SV_InstanceID)
{
    SolidVertexOut Out;
    uint SlotIdx = RigParams.SlotOffset + InstanceId;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];

    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.WorldNormal  = float3(0, 1, 0);
        Out.WorldPos     = float3(0, 0, 0);
        Out.TintLinear   = float4(0, 0, 0, 0);
        Out.Metallic     = 0.0;
        Out.Roughness    = 0.5;
        return Out;
    }

    float3x3 Rot = BuildSpinMatrix(Slot.SpinRadians, Slot.SeedScalar);
    float3 WPos  = Slot.PositionMeters + mul(Rot, Input.LocalPos * Slot.SizeMeters);
    float3 WNorm = normalize(mul(Rot, Input.LocalNormal));

    Out.ClipPosition = mul(GlobalParams.ViewProjectionMatrix, float4(WPos, 1.0));
    Out.WorldNormal  = WNorm;
    Out.WorldPos     = WPos;
    Out.TintLinear   = Slot.ColourLinear;
    Out.Metallic     = saturate(0.25 + 0.65 * Slot.AuxScalar);
    Out.Roughness    = saturate(0.18 + 0.35 * (1.0 - Slot.AuxScalar));
    return Out;
}

[shader("fragment")]
float4 ParticleSolidFragmentEntry(SolidVertexOut Input) : SV_Target
{
    float3 N = normalize(Input.WorldNormal);
    float3 V = normalize(GlobalParams.CameraPositionMeters.xyz - Input.WorldPos);
    float3 L = normalize(float3(0.45, 0.82, 0.36));
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));
    float NdotV = saturate(dot(N, V));

    float SpecPower = lerp(96.0, 12.0, Input.Roughness);
    float Specular  = pow(NdotH, SpecPower) * lerp(0.35, 1.15, Input.Metallic);
    float Fresnel   = pow(1.0 - NdotV, 4.0) * 0.42;

    float3 BaseCol = Input.TintLinear.rgb;
    float3 Lit = BaseCol * (0.24 + 0.76 * NdotL) + float3(Specular + Fresnel);
    return float4(Lit, Input.TintLinear.a);
}
