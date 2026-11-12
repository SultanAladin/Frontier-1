//============================================================================================================================================
//                                                       IMGUIOVERLAYRASTERIZATION.SLANG
//============================================================================================================================================
// Native Vulkan .slang rasterizer rendering ImDrawData batches (Typography, Panes, ControlCentre, ContentBrowser & GPU Viewports)

struct ImGuiVertexRecord
{
    float4 PositionAndUV; // xy = Screen Position (px), zw = Texture UV
    float4 UnpackedColor; // rgba = Unpacked [0..1] sRGB/linear vertex color
};

struct ImGuiPushBlock
{
    float4 ScaleAndTranslate; // xy = Scale (2/W, 2/H), zw = Translate (-1, -1)
};

[[vk::push_constant]] ImGuiPushBlock                       PushParams;
[[vk::binding(0, 0)]] StructuredBuffer<ImGuiVertexRecord>  ImGuiVertices;
[[vk::binding(1, 0)]] Sampler2D                            AtlasOrViewportTexture;

struct ImGuiVSOutput
{
    float4 ClipPosition : SV_Position;
    float2 TexCoord     : TEXCOORD0;
    float4 VertexColor  : TEXCOORD1;
};

[shader("vertex")]
ImGuiVSOutput VertexEntry(uint VertexID : SV_VertexID)
{
    ImGuiVertexRecord Rec = ImGuiVertices[VertexID];
    ImGuiVSOutput     Out;
    float2 ClipXY    = Rec.PositionAndUV.xy * PushParams.ScaleAndTranslate.xy + PushParams.ScaleAndTranslate.zw;
    Out.ClipPosition = float4(ClipXY.x, ClipXY.y, 0.0, 1.0);
    Out.TexCoord     = Rec.PositionAndUV.zw;
    Out.VertexColor  = Rec.UnpackedColor;
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(ImGuiVSOutput In) : SV_Target0
{
    float4 Sampled = AtlasOrViewportTexture.Sample(In.TexCoord);
    return In.VertexColor * Sampled;
}
