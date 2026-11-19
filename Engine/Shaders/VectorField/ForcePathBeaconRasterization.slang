/*============================================================================================================================================
                                                      FORCEPATHBEACONRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Editor-only Lit-visible 3D motion paths (A->P->B curves/orbits), clickable Waypoint markers, Force circles & Anemometer cursor

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

struct BeaconVertexInput
{
    float3 PositionMeters : POSITION;
    float3 NormalVector   : NORMAL;
    float4 BeaconColor    : COLOR0;
};

struct BeaconVaryings
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float4 Color        : COLOR0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
BeaconVaryings VertexMain(BeaconVertexInput Input)
{
    BeaconVaryings Out;
    Out.ClipPosition = VfProjectWorldToClip(Overlay, Input.PositionMeters);
    Out.WorldNormal  = normalize(Input.NormalVector);
    Out.Color        = Input.BeaconColor;
    return Out;
}

[shader("fragment")]
OverlayFragmentOutput FragmentMain(BeaconVaryings In)
{
    OverlayFragmentOutput Out;

    float3 N     = normalize(In.WorldNormal);
    float3 Sun   = normalize(float3(0.42, -0.35, 0.84));
    float  Shade = 0.72 + 0.28 * saturate(dot(N, Sun));

    Out.Color  = float4(In.Color.rgb * Shade, In.Color.a);
    Out.PickId = uint4(SurfacePush.PickCategory, SurfacePush.PickPrimaryId, SurfacePush.PickSecondaryId, 0u);
    return Out;
}
