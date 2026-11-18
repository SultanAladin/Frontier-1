/*============================================================================================================================================
                                                      COMPONENTCONSTRAINTVISUAL.SLANG
============================================================================================================================================*/
// 🧩 Layer-1 X-ray constraint hardware, Hinge/Ball limit range fans, Spin comet ribbons, and Dock pulsing iris shader

#include "Engine/Shaders/ComponentEditor/ComponentEditorShared.slang"

[[vk::push_constant]]
ConstantBuffer<ComponentSurfacePushConstants> DrawPush;

[[vk::binding(0, 0)]]
ConstantBuffer<ComponentViewportUniforms> Viewport;

struct ConstraintVertexInput
{
    float3 PositionMeters : POSITION;
    float3 NormalVector   : NORMAL;
    float2 ParameterUv    : TEXCOORD0;
    float3 AuxColor       : COLOR0;
};

[shader("vertex")]
ComponentVertexOutput VertexMain(ConstraintVertexInput Input)
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

// ChannelModifiers.z encodes ConstraintVisualSubMode:
// 0 = Lit hardware solid (graphite/steel/accent)
// 1 = Angular limit range fan (ParameterUv.x = angle share [0..1], ParameterUv.y = radius share [0..1], ChannelModifiers.w = normalized current)
// 2 = Spin comet ribbon (3 trailing arcs)
// 3 = Dock pulsing iris disc
[shader("fragment")]
ComponentFragmentOutput FragmentMain(ComponentVertexOutput In)
{
    float  SubMode = DrawPush.ChannelModifiers.z;
    float  TimeSec = Viewport.CameraPositionTime.w;
    float3 Accent  = DrawPush.AlbedoRoughness.rgb;
    float3 FinalRgb = Accent;
    float  Alpha   = saturate(DrawPush.ChannelModifiers.y);

    if (SubMode < 0.5) // Shaded constraint hardware solid
    {
        float3 N = CeNormalizeSafe(In.WorldNormal, float3(0.0, 1.0, 0.0));
        float3 V = CeNormalizeSafe(Viewport.CameraPositionTime.xyz - In.WorldPos, float3(0.0, 0.0, 1.0));
        FinalRgb = CeEvaluateOpenPbrDirect(
            Viewport,
            In.WorldPos,
            N,
            V,
            Accent,
            DrawPush.AlbedoRoughness.w,
            DrawPush.EmissiveMetalness.w,
            DrawPush.EmissiveMetalness.rgb);
    }
    else if (SubMode < 1.5) // Hinge / Ball angular range fan
    {
        float Radial = smoothstep(0.0, 0.30, In.ParameterUv.y) * (1.0 - smoothstep(0.80, 1.0, In.ParameterUv.y));
        float Edge   = max(1.0 - smoothstep(0.0, 0.035, In.ParameterUv.x),
                           1.0 - smoothstep(0.0, 0.035, 1.0 - In.ParameterUv.x));
        float CurNorm = DrawPush.ChannelModifiers.w;
        float CurGlow = (CurNorm >= 0.0) ? exp(-pow((In.ParameterUv.x - CurNorm) / 0.035, 2.0)) : 0.0;
        float Shimmer = 0.85 + 0.15 * sin(In.ParameterUv.y * 18.0 - TimeSec * 3.0);
        Alpha = clamp((0.07 + 0.22 * In.ParameterUv.y) * Radial * Shimmer + Edge * 0.75 * Radial + CurGlow * 0.90 * Radial, 0.0, 1.0);
        FinalRgb = Accent * Alpha;
    }
    else if (SubMode < 2.5) // Spin 3-comet ribbon
    {
        float Radial = smoothstep(0.0, 0.30, In.ParameterUv.y) * (1.0 - smoothstep(0.80, 1.0, In.ParameterUv.y));
        float Comet  = pow(frac(In.ParameterUv.x * 3.0), 4.0);
        Alpha = clamp(Comet * Radial * DrawPush.ChannelModifiers.y, 0.0, 1.0);
        FinalRgb = Accent * Alpha;
    }
    else // Dock pulsing iris disc
    {
        float2 P = In.ParameterUv * 2.0 - 1.0;
        float  R = length(P);
        if (R > 1.0)
        {
            discard;
        }
        float SpinSense = (DrawPush.ChannelModifiers.w > 0.5) ? 1.0 : -1.0;
        float Steady    = (DrawPush.ChannelModifiers.w > 0.5) ? 1.0 : 0.0;
        float Rings     = pow(0.5 + 0.5 * sin(R * 22.0 + TimeSec * 7.0 * SpinSense), 6.0);
        float Core   = exp(-R * R * 6.0);
        Alpha = clamp(lerp(Rings * (1.0 - R) * 0.9, Core * 0.9 + 0.15 * (1.0 - R), Steady), 0.0, 1.0);
        FinalRgb = Accent * Alpha;
    }

    ComponentFragmentOutput Out;
    Out.ColorRGBA = float4(FinalRgb, Alpha);
    Out.PickIDs   = uint4(DrawPush.PickPacked.x, DrawPush.PickPacked.y, DrawPush.PickPacked.z, 0u);
    return Out;
}
