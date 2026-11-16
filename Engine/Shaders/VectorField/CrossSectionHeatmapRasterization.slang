/*============================================================================================================================================
                                                    CROSSSECTIONHEATMAPRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Editor-only orthogonal XY/XZ/YZ cross-section scalar heatmap & analytical isolines rendered over Lit geometry

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/VectorField/VectorFieldRenderingShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

[[vk::push_constant]]
ConstantBuffer<DrawObjectPushConstants> DrawPush;

[[vk::binding(0, 0)]]
ConstantBuffer<ViewportOverlayUniforms> Overlay;

[[vk::binding(1, 0)]]
StructuredBuffer<ForceSourceShaderEntry> SourceBuffer;

[[vk::binding(2, 0)]]
StructuredBuffer<float4> WaypointBuffer;

struct HeatmapVaryings
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float2 PlaneUv      : TEXCOORD1;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
HeatmapVaryings VertexMain(uint VertexId : SV_VertexID)
{
    static const float2 QuadCorners[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0),
    };

    float2 Corner   = QuadCorners[VertexId % 6u];
    float  HalfSpan = max(10.0, Overlay.CrossSectionPlaneParams.z);
    float  OffsetM  = Overlay.CrossSectionPlaneParams.y;
    uint   Axis     = DrawPush.SubMode; // 0 = XY (+Z normal), 1 = XZ (+Y normal), 2 = YZ (+X normal)

    float3 WorldPos;
    if (Axis == 1u)
    {
        WorldPos = float3(Corner.x * HalfSpan, OffsetM, Corner.y * HalfSpan + HalfSpan * 0.35);
    }
    else if (Axis == 2u)
    {
        WorldPos = float3(OffsetM, Corner.x * HalfSpan, Corner.y * HalfSpan + HalfSpan * 0.35);
    }
    else
    {
        WorldPos = float3(Corner.x * HalfSpan, Corner.y * HalfSpan, OffsetM);
    }

    HeatmapVaryings Out;
    Out.ClipPosition = VfProjectWorldToClip(Overlay, WorldPos);
    Out.WorldPos     = WorldPos;
    Out.PlaneUv      = Corner * 0.5 + 0.5;
    return Out;
}

[shader("fragment")]
OverlayFragmentOutput FragmentMain(HeatmapVaryings In)
{
    OverlayFragmentOutput Out;

    uint   SourceCount = Overlay.ActiveSourceCounts.x;
    uint   Channel     = uint(Overlay.CrossSectionPlaneParams.w);
    float3 ZeroVel     = float3(0.0, 0.0, 0.0);
    float3 V           = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, SourceCount, In.WorldPos, ZeroVel);

    float ScalarMetric = length(V);
    if (Channel == 2u) // Curl |∇×F|
    {
        float H = 0.25;
        float3 Vx = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, SourceCount, In.WorldPos + float3(H, 0, 0), ZeroVel);
        float3 Vy = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, SourceCount, In.WorldPos + float3(0, H, 0), ZeroVel);
        float3 Vz = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, SourceCount, In.WorldPos + float3(0, 0, H), ZeroVel);
        float3 Curl = float3((Vy.z - V.z) - (Vz.y - V.y), (Vz.x - V.x) - (Vx.z - V.z), (Vx.y - V.y) - (Vy.x - V.x)) / H;
        ScalarMetric = length(Curl) * 18.0;
    }
    else if (Channel == 3u) // Divergence ∇·F
    {
        float H = 0.25;
        float3 Vx = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, SourceCount, In.WorldPos + float3(H, 0, 0), ZeroVel);
        float3 Vy = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, SourceCount, In.WorldPos + float3(0, H, 0), ZeroVel);
        float3 Vz = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, SourceCount, In.WorldPos + float3(0, 0, H), ZeroVel);
        ScalarMetric = abs(((Vx.x - V.x) + (Vy.y - V.y) + (Vz.z - V.z)) / H) * 24.0;
    }

    float Norm = saturate(ScalarMetric / 28.0);
    float3 Ramp = VfRampSpeedColor(Norm);

    // Crisp isoline contour rings every 0.14 normalized steps
    float ContourPhase = frac(Norm * 7.0);
    float ContourLine  = 1.0 - smoothstep(0.0, 0.08, min(ContourPhase, 1.0 - ContourPhase));
    float3 FinalRgb    = lerp(Ramp, float3(0.92, 0.98, 1.0), ContourLine * 0.42);

    float EdgeBorder = min(min(In.PlaneUv.x, 1.0 - In.PlaneUv.x), min(In.PlaneUv.y, 1.0 - In.PlaneUv.y));
    float FrameMask  = smoothstep(0.0, 0.015, EdgeBorder);
    float Alpha      = lerp(0.32, 0.12 + Norm * 0.48, FrameMask) * smoothstep(0.01, 0.09, Norm + ContourLine * 0.08);

    // Preserve visible thin border outline around active slice plane
    if (EdgeBorder < 0.008)
    {
        FinalRgb = float3(0.41, 0.86, 1.0);
        Alpha    = 0.55;
    }

    Out.Color  = float4(FinalRgb, saturate(Alpha));
    Out.PickId = uint4(PickCategoryNone, 0u, 0u, 0u);
    return Out;
}
