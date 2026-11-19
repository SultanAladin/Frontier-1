/*============================================================================================================================================
                                                        STREAMLINERASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Editor-only volumetric 3D GPU RK4 streamline ribbons with Cyan/Speed/Custom/Altitude modes, emissive boost & phase pulses

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

struct StreamlineVaryings
{
    float4 ClipPosition : SV_Position;
    float3 ColorRgb     : COLOR0;
    float2 RibbonUv     : TEXCOORD0; // x = 0..1 along line, y = -1..+1 across ribbon half-width
    float  NormalizedT  : TEXCOORD1;
    float  LocalSpeed   : TEXCOORD2;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                SEEDING & RK4 INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

float3 ComputeVolumetricStreamlineSeed(uint LineIndex, uint TotalLines, uint SourceCount)
{
    if (SourceCount == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }

    uint SrcIdx = LineIndex % SourceCount;
    uint Local  = LineIndex / SourceCount;
    ForceSourceShaderEntry Src = SourceBuffer[SrcIdx];

    float R  = max(1.0, Src.CentreRadiusPacked.w * 0.88);
    float H1 = VfHashFloat01(Local * 747796405u + 2891336453u + SrcIdx * 97u);
    float H2 = VfHashFloat01(Local * 277803737u + 1597334677u + SrcIdx * 53u);
    float H3 = VfHashFloat01(Local * 982451653u + 53129771u   + SrcIdx * 31u);

    float Theta  = H1 * VfTau;
    float CosPhi = H2 * 2.0 - 1.0;
    float SinPhi = sqrt(max(0.0, 1.0 - CosPhi * CosPhi));
    float Rad    = R * pow(0.18 + 0.82 * H3, 0.58);

    float3 Offset = float3(Rad * SinPhi * cos(Theta), Rad * SinPhi * sin(Theta), Rad * CosPhi);
    // Concentrate slightly around slice plane while filling full 3D volume
    Offset.z *= 0.82;
    return Src.CentreRadiusPacked.xyz + Offset;
}

float3 EvaluateFieldVelocityAt(float3 Pos)
{
    return VfEvaluateCombinedVelocity(
        SourceBuffer,
        WaypointBuffer,
        Overlay.ActiveSourceCounts.x,
        Pos,
        float3(0.0, 0.0, 0.0));
}

void IntegrateRk4Step(inout float3 Pos, float StepMeters, out float OutSpeed)
{
    float3 K1 = EvaluateFieldVelocityAt(Pos);
    float  S1 = length(K1);
    if (S1 < 0.15)
    {
        OutSpeed = S1;
        return;
    }
    float3 N1 = K1 / S1;
    float3 K2 = EvaluateFieldVelocityAt(Pos + N1 * (StepMeters * 0.5));
    float3 N2 = VfNormalizeSafe(K2, N1);
    float3 K3 = EvaluateFieldVelocityAt(Pos + N2 * (StepMeters * 0.5));
    float3 N3 = VfNormalizeSafe(K3, N2);
    float3 K4 = EvaluateFieldVelocityAt(Pos + N3 * StepMeters);
    float3 N4 = VfNormalizeSafe(K4, N3);

    Pos      = Pos + normalize(N1 + 2.0 * N2 + 2.0 * N3 + N4) * StepMeters;
    OutSpeed = S1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
StreamlineVaryings VertexMain(
    uint VertexId   : SV_VertexID,
    uint InstanceId : SV_InstanceID)
{
    uint Segments    = clamp(uint(Overlay.StreamVisualParams.z), 8u, 72u);
    uint LineCount   = max(1u, uint(Overlay.StreamVisualParams.w));
    uint SrcCount    = Overlay.ActiveSourceCounts.x;

    uint SegIdx      = min(Segments, VertexId / 2u);
    float SideSign   = ((VertexId & 1u) == 0u) ? -1.0 : 1.0;

    float3 Pos       = ComputeVolumetricStreamlineSeed(InstanceId, LineCount, SrcCount);
    float  StepM     = 1.15;
    float  Speed     = length(EvaluateFieldVelocityAt(Pos));

    for (uint S = 0u; S < SegIdx; ++S)
    {
        float StepSpd = 0.0;
        IntegrateRk4Step(Pos, StepM, StepSpd);
        Speed = StepSpd;
    }

    float3 Vel       = EvaluateFieldVelocityAt(Pos);
    float  CurSpeed  = length(Vel);
    float3 Tangent   = (CurSpeed > 0.15) ? (Vel / CurSpeed) : float3(1.0, 0.0, 0.0);
    float3 ViewDir   = normalize(Overlay.CameraPositionTime.xyz - Pos);
    float3 SideAxis  = VfNormalizeSafe(cross(ViewDir, Tangent), Overlay.CameraRightThickness.xyz);

    float  U         = float(SegIdx) / float(Segments);
    float  Taper     = sin(U * VfPi);
    float  HalfWidth = (0.14 + 0.10 * saturate(CurSpeed / 24.0)) *
                       max(0.15, Overlay.CameraRightThickness.w) *
                       (0.35 + 0.65 * Taper);

    if (CurSpeed < 0.20)
    {
        HalfWidth = 0.0;
    }

    float3 WorldVert = Pos + SideAxis * (SideSign * HalfWidth);

    uint ColorMode = uint(Overlay.StreamVisualParams.x);
    float3 BaseRgb;
    if (ColorMode == 1u) // CyanSingle (#68DCFF)
    {
        BaseRgb = float3(0.408, 0.863, 1.0);
    }
    else if (ColorMode == 2u) // CustomSingle
    {
        BaseRgb = Overlay.CustomStreamColor.rgb;
    }
    else if (ColorMode == 3u) // Elevation
    {
        BaseRgb = VfElevationColor(Pos.z);
    }
    else // SpeedRamp
    {
        BaseRgb = VfRampSpeedColor(saturate(CurSpeed / 26.0));
    }

    StreamlineVaryings Out;
    Out.ClipPosition = VfProjectWorldToClip(Overlay, WorldVert);
    Out.ColorRgb     = BaseRgb;
    Out.RibbonUv     = float2(U, SideSign);
    Out.NormalizedT  = U;
    Out.LocalSpeed   = CurSpeed;
    return Out;
}

[shader("fragment")]
OverlayFragmentOutput FragmentMain(StreamlineVaryings In)
{
    OverlayFragmentOutput Out;
    if (In.LocalSpeed < 0.15)
    {
        discard;
    }

    float CrossFade = 1.0 - pow(abs(In.RibbonUv.y), 1.8);
    float EndFade   = smoothstep(0.0, 0.12, In.NormalizedT) * smoothstep(1.0, 0.88, In.NormalizedT);

    // Animated traveling phase pulse spark (saturated hue — zero white washout)
    float Phase     = frac(In.NormalizedT * 1.8 - Overlay.CameraPositionTime.w * (0.45 + 0.02 * In.LocalSpeed));
    float PulseGlow = exp(-pow((Phase - 0.5) / 0.11, 2.0)) * 0.55;

    float Emissive  = max(0.35, Overlay.CameraUpEmissive.w);
    float3 ShadeRgb = In.ColorRgb * Emissive * (0.88 + PulseGlow);

    float Alpha     = clamp(Overlay.StreamVisualParams.y, 0.05, 1.0) * CrossFade * EndFade * (0.78 + 0.22 * PulseGlow);
    if (Alpha <= 0.01)
    {
        discard;
    }

    Out.Color  = float4(ShadeRgb, saturate(Alpha));
    Out.PickId = uint4(PickCategoryNone, 0u, 0u, 0u);
    return Out;
}
