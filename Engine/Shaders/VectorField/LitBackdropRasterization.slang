/*============================================================================================================================================
                                                        LITBACKDROPRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Editor Lit PBR architectural blockout & terrain surface rasterizer demonstrating Force Viz clarity over Lit shading

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

struct LitVertexInput
{
    float3 PositionMeters : POSITION;
    float3 NormalVector   : NORMAL;
    float4 BaseAlbedo     : COLOR0;
};

struct LitVaryings
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : TEXCOORD1;
    float4 Albedo       : COLOR0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
LitVaryings VertexMain(LitVertexInput Input)
{
    LitVaryings Out;
    Out.ClipPosition = VfProjectWorldToClip(Overlay, Input.PositionMeters);
    Out.WorldPos     = Input.PositionMeters;
    Out.WorldNormal  = normalize(Input.NormalVector);
    Out.Albedo       = Input.BaseAlbedo;
    return Out;
}

[shader("fragment")]
OverlayFragmentOutput FragmentMain(LitVaryings In)
{
    OverlayFragmentOutput Out;

    float3 N = normalize(In.WorldNormal);
    float3 L = normalize(float3(0.58, -0.42, 0.70)); // +Z Up directional sun
    float3 V = normalize(Overlay.CameraPositionTime.xyz - In.WorldPos);
    float3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float Spec  = pow(NdotH, 36.0) * 0.28;

    // Subtle analytical ground grid on ground-facing terrain (+Z Up)
    float3 BaseRgb = In.Albedo.rgb;
    if (abs(N.z) > 0.85 && abs(In.WorldPos.z) < 0.25)
    {
        float2 GridCoord = In.WorldPos.xy / 10.0;
        float2 CellDist  = abs(frac(GridCoord - 0.5) - 0.5) / max(fwidth(GridCoord), float2(0.001, 0.001));
        float  LineMask  = 1.0 - min(min(CellDist.x, CellDist.y), 1.0);
        BaseRgb = lerp(BaseRgb, float3(0.22, 0.24, 0.27), LineMask * 0.45);
    }

    float3 SkyAmbient = lerp(float3(0.08, 0.09, 0.11), float3(0.22, 0.27, 0.34), saturate(N.z * 0.5 + 0.5));
    float3 SunDirect  = float3(1.00, 0.95, 0.88) * (NdotL * 0.88 + Spec);
    float3 Shaded     = BaseRgb * (SkyAmbient + SunDirect);

    // Distance atmospheric horizon blending
    float CamDist = length(Overlay.CameraPositionTime.xyz - In.WorldPos);
    float FogAmt  = smoothstep(110.0, 260.0, CamDist);
    float3 FinalRgb = lerp(Shaded, float3(0.075, 0.086, 0.105), FogAmt);

    Out.Color  = float4(pow(saturate(FinalRgb), 1.0 / 2.2), In.Albedo.a);
    Out.PickId = uint4(DrawPush.PickCategory, DrawPush.PickPrimaryId, DrawPush.PickSecondaryId, 0u);
    return Out;
}
