/*============================================================================================================================================
                                                          PARTICLERIBBONRASTER.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 continuous camera-facing ribbon/strand vertex & fragment shaders consuming StrandVertexGpu history chains

import ParticleBuffers;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] StructuredBuffer<ParticleSlotGpu>        ParticleSlots;
[[vk::binding(1, 1)]] StructuredBuffer<StrandVertexGpu>        StrandVertices;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(3, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;

//------------------------------------------------------------------------------------------------------------------------
//                                                      RASTER STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RibbonVertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 RibbonUv     : TEXCOORD0;
    float4 ColourLinear : COLOR0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     VERTEX ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
RibbonVertexOutput ParticleRibbonVertexEntry(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    RibbonVertexOutput Out;
    uint SlotIdx = RigParams.SlotOffset + InstanceId;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];

    uint SegVerts = max(2u, RigParams.StrandSegments);
    uint RingIdx  = min(VertexId / 2u, SegVerts - 1u);
    float Side    = ((VertexId & 1u) == 0u) ? -1.0 : 1.0;

    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u || InstanceId >= RigParams.SlotCapacity)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.RibbonUv     = float2(0.0, 0.0);
        Out.ColourLinear = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    uint StrandStart = InstanceId * SegVerts;
    uint CurIdx      = StrandStart + RingIdx;
    uint PrvIdx      = StrandStart + (RingIdx > 0u ? (RingIdx - 1u) : 0u);
    uint NxtIdx      = StrandStart + min(RingIdx + 1u, SegVerts - 1u);

    StrandVertexGpu CurV = StrandVertices[CurIdx];
    float3 Tangent = StrandVertices[NxtIdx].PositionMeters - StrandVertices[PrvIdx].PositionMeters;
    float  TanLen  = length(Tangent);
    Tangent = (TanLen > 1e-5) ? (Tangent / TanLen) : float3(0.0, 1.0, 0.0);

    float3 ToCam = normalize(GlobalParams.CameraPositionMeters.xyz - CurV.PositionMeters);
    float3 Right = cross(Tangent, ToCam);
    float  RLen  = length(Right);
    Right = (RLen > 1e-5) ? (Right / RLen) : GlobalParams.CameraRightMeters.xyz;

    float Taper     = saturate(1.0 - CurV.NormalisedSpanU * 0.85);
    float HalfWidth = max(0.002, Slot.SizeMeters * 0.65 * Taper);
    float3 WorldPos = CurV.PositionMeters + Right * (Side * HalfWidth);

    Out.ClipPosition = mul(GlobalParams.ViewProjectionMatrix, float4(WorldPos, 1.0));
    Out.RibbonUv     = float2(CurV.NormalisedSpanU, Side * 0.5 + 0.5);
    Out.ColourLinear = float4(Slot.ColourLinear.rgb, Slot.ColourLinear.a * Taper);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAGMENT ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("fragment")]
float4 ParticleRibbonFragmentEntry(RibbonVertexOutput Input) : SV_Target0
{
    float EdgeDist = abs(Input.RibbonUv.y - 0.5) * 2.0;
    float SoftEdge = saturate(1.0 - EdgeDist * EdgeDist);
    float TailFade = saturate(1.0 - Input.RibbonUv.x);
    float Alpha    = SoftEdge * TailFade * Input.ColourLinear.a;
    if (Alpha <= 0.002)
    {
        discard;
    }
    return float4(Input.ColourLinear.rgb * Alpha, Alpha);
}
