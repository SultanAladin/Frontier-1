/*============================================================================================================================================
                                                        VECTORARROWRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Editor-only GPU instanced 3D directional arrows colored by speed LUT & culled smoothly beneath quiet threshold

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/VectorField/VectorFieldRenderingShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

[[vk::push_constant]]
ConstantBuffer<RenderInstancePushConstants> SurfacePush;

[[vk::binding(0, 0)]]
ConstantBuffer<ViewportOverlayUniforms> Overlay;

[[vk::binding(1, 0)]]
StructuredBuffer<ForceSourceShaderEntry> SourceBuffer;

[[vk::binding(2, 0)]]
StructuredBuffer<float4> WaypointBuffer;

struct ArrowVaryings
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float4 ArrowColor   : COLOR0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
ArrowVaryings VertexMain(
    float3 ArrowUnitPos    : POSITION, // Authored along +X [0..1] shaft + cone head
    float3 ArrowUnitNormal : NORMAL,
    uint   InstanceId      : SV_InstanceID)
{
    uint   GridDim  = 11u;
    uint   Ix       = InstanceId % GridDim;
    uint   Iy       = (InstanceId / GridDim) % GridDim;
    uint   Iz       = (InstanceId / (GridDim * GridDim)) % 3u;
    float  HalfSpan = max(12.0, Overlay.CrossSectionPlaneParams.z * 0.88);
    float  Fx       = (float(Ix) / float(GridDim - 1u)) * 2.0 - 1.0;
    float  Fy       = (float(Iy) / float(GridDim - 1u)) * 2.0 - 1.0;
    float  Fz       = (float(Iz) - 1.0) * 8.0 + Overlay.CrossSectionPlaneParams.y;

    float3 AnchorPos = float3(Fx * HalfSpan, Fy * HalfSpan, Fz);
    float3 Vel       = VfEvaluateCombinedVelocity(SourceBuffer, WaypointBuffer, Overlay.ActiveSourceCounts.x, AnchorPos, float3(0.0, 0.0, 0.0));
    float  Speed     = length(Vel);

    ArrowVaryings Out;
    if (Speed < 0.35)
    {
        Out.ClipPosition = float4(0.0, 0.0, -10.0, 1.0);
        Out.WorldNormal  = float3(0.0, 0.0, 1.0);
        Out.ArrowColor   = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    float3 Fwd  = Vel / Speed;
    float3 Ref  = (abs(Fwd.z) < 0.92) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 Left = normalize(cross(Ref, Fwd));
    float3 Up   = cross(Fwd, Left);

    float  ArrowLen = lerp(2.2, 6.4, saturate(Speed / 26.0));
    float  ArrowRad = lerp(0.55, 1.05, saturate(Speed / 26.0));
    float3 Scaled   = float3(ArrowUnitPos.x * ArrowLen, ArrowUnitPos.y * ArrowRad, ArrowUnitPos.z * ArrowRad);
    float3 WorldPos = AnchorPos + Fwd * Scaled.x + Left * Scaled.y + Up * Scaled.z;

    Out.ClipPosition = VfProjectWorldToClip(Overlay, WorldPos);
    Out.WorldNormal  = normalize(Fwd * ArrowUnitNormal.x + Left * ArrowUnitNormal.y + Up * ArrowUnitNormal.z);
    float3 RampColor = VfRampSpeedColor(saturate(Speed / 26.0));
    Out.ArrowColor   = float4(RampColor * (0.9 + 0.35 * Overlay.CameraUpEmissive.w), 0.94);
    return Out;
}

[shader("fragment")]
OverlayFragmentOutput FragmentMain(ArrowVaryings In)
{
    OverlayFragmentOutput Out;
    if (In.ArrowColor.a <= 0.01)
    {
        discard;
    }

    float3 N     = normalize(In.WorldNormal);
    float3 Sun   = normalize(float3(0.45, -0.35, 0.82));
    float  Shade = 0.58 + 0.42 * saturate(dot(N, Sun));

    Out.Color  = float4(In.ArrowColor.rgb * Shade, In.ArrowColor.a);
    Out.PickId = uint4(PickCategoryNone, 0u, 0u, 0u);
    return Out;
}
