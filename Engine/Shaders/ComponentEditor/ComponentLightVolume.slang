/*============================================================================================================================================
                                                        COMPONENTLIGHTVOLUME.SLANG
============================================================================================================================================*/
// 🧩 Additive SpotBeam & Camera frustum volumetric beam shader attenuating smoothly along +X beam axis

#include "Engine/Shaders/ComponentEditor/ComponentEditorShared.slang"

[[vk::push_constant]]
ConstantBuffer<ComponentSurfacePushConstants> DrawPush;

[[vk::binding(0, 0)]]
ConstantBuffer<ComponentViewportUniforms> Viewport;

struct BeamVertexInput
{
    float3 PositionMeters : POSITION;
    float3 NormalVector   : NORMAL;
    float2 ParameterUv    : TEXCOORD0;
    float3 AuxColor       : COLOR0;
};

[shader("vertex")]
ComponentVertexOutput VertexMain(BeamVertexInput Input)
{
    float4 World4 = mul(DrawPush.ModelMatrix, float4(Input.PositionMeters, 1.0));
    float3x3 M3   = (float3x3)DrawPush.ModelMatrix;

    ComponentVertexOutput Out;
    Out.ClipPosition = mul(Viewport.ViewProjection, World4);
    Out.WorldPos     = World4.xyz;
    Out.WorldNormal  = CeNormalizeSafe(mul(M3, Input.NormalVector), float3(0.0, 1.0, 0.0));
    Out.LocalPos     = Input.PositionMeters;
    Out.LocalNormal  = Input.NormalVector;
    Out.ParameterUv  = Input.ParameterUv;
    Out.VertexColor  = Input.AuxColor;
    return Out;
}

[shader("fragment")]
ComponentFragmentOutput FragmentMain(ComponentVertexOutput In)
{
    float  BeamLength = max(DrawPush.ChannelModifiers.w, 0.1);
    float  Strength   = max(DrawPush.ChannelModifiers.y, 0.05);
    float  Along      = clamp(In.LocalPos.x / BeamLength, 0.0, 1.0);
    float3 N          = CeNormalizeSafe(In.WorldNormal, float3(0.0, 1.0, 0.0));
    float3 V          = CeNormalizeSafe(Viewport.CameraPositionTime.xyz - In.WorldPos, float3(0.0, 0.0, 1.0));
    float  Core       = pow(abs(dot(N, V)), 1.4);
    float  Falloff    = pow(1.0 - Along, 1.6) * smoothstep(0.0, 0.06, Along);
    float  Shimmer    = 0.92 + 0.08 * sin(Along * 40.0 - Viewport.CameraPositionTime.w * 3.0);

    float  Alpha      = Strength * Falloff * (0.25 + 0.75 * Core) * Shimmer;

    ComponentFragmentOutput Out;
    Out.ColorRGBA = float4(DrawPush.AlbedoRoughness.rgb * Alpha, clamp(Alpha, 0.0, 1.0));
    Out.PickIDs   = uint4(0u, 0u, 0u, 0u);
    return Out;
}
