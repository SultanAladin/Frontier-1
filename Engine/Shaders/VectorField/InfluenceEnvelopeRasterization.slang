/*============================================================================================================================================
                                                     INFLUENCEENVELOPERASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Editor-only shaded semi-transparent ForceModel reach envelopes & RadialBurst pulse shells (zero wireframes)

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

struct EnvelopeVaryings
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : TEXCOORD1;
    uint   SourceToken  : TEXCOORD2;
    uint   SelectedFlag : TEXCOORD3;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
EnvelopeVaryings VertexMain(
    float3 UnitPos    : POSITION,
    float3 UnitNormal : NORMAL)
{
    uint SrcIdx = DrawPush.ObjectIndex;
    ForceSourceShaderEntry Src = SourceBuffer[SrcIdx];

    float3 Anchor   = Src.CentreRadiusPacked.xyz;
    float  Radius   = max(0.25, Src.CentreRadiusPacked.w);
    float3 Forward  = Src.DirectionStrength.xyz;
    float3 Left     = Src.TangentLeftFalloff.xyz;
    float3 Up       = Src.UpCoreRadius.xyz;

    float3 LocalScaled;
    uint   Env = Src.IdsPacked.w;
    if (Env == EnvelopeOrientedBox || Env == EnvelopePlanarBox)
    {
        LocalScaled = UnitPos * Src.HalfExtentsTurbScale.xyz;
    }
    else if (Env == EnvelopeColumn)
    {
        LocalScaled = float3(UnitPos.x * Radius, UnitPos.y * Radius, UnitPos.z * Src.HalfExtentsTurbScale.z);
    }
    else
    {
        LocalScaled = UnitPos * Radius;
    }

    float3 WorldPos    = Anchor + Forward * LocalScaled.x + Left * LocalScaled.y + Up * LocalScaled.z;
    float3 WorldNormal = VfNormalizeSafe(Forward * UnitNormal.x + Left * UnitNormal.y + Up * UnitNormal.z, float3(0.0, 0.0, 1.0));

    EnvelopeVaryings Out;
    Out.ClipPosition = VfProjectWorldToClip(Overlay, WorldPos);
    Out.WorldPos     = WorldPos;
    Out.WorldNormal  = WorldNormal;
    Out.SourceToken  = DrawPush.PickPrimaryId;
    Out.SelectedFlag = (DrawPush.PickPrimaryId == Overlay.ActiveSourceCounts.y) ? 1u : 0u;
    return Out;
}

[shader("fragment")]
OverlayFragmentOutput FragmentMain(EnvelopeVaryings In)
{
    OverlayFragmentOutput Out;

    float3 N = normalize(In.WorldNormal);
    float3 V = normalize(Overlay.CameraPositionTime.xyz - In.WorldPos);
    float  Fresnel = pow(1.0 - saturate(abs(dot(N, V))), 2.2);

    float3 Tint = (In.SelectedFlag != 0u)
                      ? float3(0.95, 0.74, 0.38)
                      : float3(0.32, 0.78, 0.96);

    // Subtle interior fill + crisp translucent Fresnel shell visible over Lit terrain
    float Alpha = lerp(0.055, 0.34, Fresnel);
    if (In.SelectedFlag != 0u)
    {
        Alpha = lerp(0.085, 0.46, Fresnel);
    }

    Out.Color  = float4(Tint * (0.85 + 0.55 * Fresnel), Alpha);
    Out.PickId = uint4(PickCategoryForceCircle, In.SourceToken, 0u, 0u);
    return Out;
}
