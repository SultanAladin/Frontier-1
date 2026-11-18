/*============================================================================================================================================
                                                         WORLDVIEWPORTBLIT.SLANG
============================================================================================================================================*/
// 🧩 Fullscreen GPU viewport blit + ForceField overlay composite running in the Gizmo render pass (zero CPU readback)

struct BlitPushBlock
{
    uint ForceOverlayEnabled;
    uint Pad0;
    uint Pad1;
    uint Pad2;
};

[[vk::push_constant]] BlitPushBlock PushParams;
[[vk::binding(0, 0)]] Sampler2D     WorldSourceTexture;
[[vk::binding(1, 0)]] Sampler2D     ForceOverlayTexture;

struct BlitVSOutput
{
    float4 ClipPosition : SV_Position;
    float2 TexCoord     : TEXCOORD0;
};

struct BlitFSOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

[shader("vertex")]
BlitVSOutput VertexEntry(uint VertexID : SV_VulkanVertexID)
{
    BlitVSOutput Out;
    float2 Uv        = float2(float((VertexID << 1u) & 2u), float(VertexID & 2u));
    Out.ClipPosition = float4(Uv * 2.0 - 1.0, 0.0, 1.0);
    Out.TexCoord     = Uv;
    return Out;
}

[shader("fragment")]
BlitFSOutput FragmentEntry(BlitVSOutput In)
{
    float4 WorldCol = WorldSourceTexture.Sample(In.TexCoord);
    if (PushParams.ForceOverlayEnabled != 0u)
    {
        float4 ForceCol = ForceOverlayTexture.Sample(In.TexCoord);
        WorldCol.rgb = saturate(ForceCol.rgb + WorldCol.rgb * (1.0 - ForceCol.a));
    }
    BlitFSOutput Out;
    Out.ColorRGBA = float4(WorldCol.rgb, 1.0);
    Out.PickIDs   = uint4(0u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u);
    return Out;
}
