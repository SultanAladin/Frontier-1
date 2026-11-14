/*============================================================================================================================================
                                                       PARTICLESPRITERASTER.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 instanced camera-aligned billboard vertex & fragment shaders importing ParticleBuffers & ParticleSpriteCutouts

import ParticleBuffers;
import ParticleSpriteCutouts;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] StructuredBuffer<ParticleSlotGpu>        ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;

//------------------------------------------------------------------------------------------------------------------------
//                                                      RASTER STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpriteVertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 LocalUv      : TEXCOORD0;
    float4 ColourLinear : COLOR0;
    float  SeedScalar   : TEXCOORD1;
};

static const float2 QuadCorners[6] = {
    float2(-1.0, -1.0),
    float2( 1.0, -1.0),
    float2( 1.0,  1.0),
    float2(-1.0, -1.0),
    float2( 1.0,  1.0),
    float2(-1.0,  1.0)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     VERTEX ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
SpriteVertexOutput ParticleSpriteVertexEntry(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    SpriteVertexOutput Out;
    uint SlotIdx = RigParams.SlotOffset + InstanceId;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];

    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u || InstanceId >= RigParams.SlotCapacity)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.LocalUv      = float2(0.0, 0.0);
        Out.ColourLinear = float4(0.0, 0.0, 0.0, 0.0);
        Out.SeedScalar   = 0.0;
        return Out;
    }

    float2 Corner = QuadCorners[VertexId % 6u];
    float Cs = cos(Slot.SpinRadians);
    float Sn = sin(Slot.SpinRadians);
    float2 Rot = float2(Corner.x * Cs - Corner.y * Sn, Corner.x * Sn + Corner.y * Cs);

    float HalfSize = max(0.002, Slot.SizeMeters);
    float3 WorldPos = Slot.PositionMeters
                    + GlobalParams.CameraRightMeters.xyz * (Rot.x * HalfSize)
                    + GlobalParams.CameraUpMeters.xyz    * (Rot.y * HalfSize);

    Out.ClipPosition = mul(GlobalParams.ViewProjectionMatrix, float4(WorldPos, 1.0));
    Out.LocalUv      = Corner;
    Out.ColourLinear = Slot.ColourLinear;
    Out.SeedScalar   = Slot.SeedScalar;
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAGMENT ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("fragment")]
float4 ParticleSpriteFragmentEntry(SpriteVertexOutput Input) : SV_Target0
{
    float Mask = EvaluateSpriteCutoutMask(
        RigParams.SpriteCutoutCode,
        RigParams.BokehApertureCode,
        Input.LocalUv,
        Input.SeedScalar);

    float Alpha = Mask * Input.ColourLinear.a;
    if (Alpha <= 0.002)
    {
        discard;
    }

    if (RigParams.OpticalBlendCode == 1u)
    {
        // Premultiplied alpha
        return float4(Input.ColourLinear.rgb * Alpha, Alpha);
    }
    // Additive luminance output
    return float4(Input.ColourLinear.rgb * Alpha, Alpha);
}
