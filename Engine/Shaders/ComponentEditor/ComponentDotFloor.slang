/*============================================================================================================================================
                                                          COMPONENTDOTFLOOR.SLANG
============================================================================================================================================*/
// 🧩 Infinite metric (1.0 m) & quarter-meter (0.25 m) ground dot floor fading around CameraRig focus point with X/Z axis tinting

#include "Engine/Shaders/ComponentEditor/ComponentEditorShared.slang"

[[vk::push_constant]]
ConstantBuffer<ComponentSurfacePushConstants> SurfacePush;

[[vk::binding(0, 0)]]
ConstantBuffer<ComponentViewportUniforms> Viewport;

struct FloorVertexInput
{
    float3 PositionMeters : POSITION;
    float3 NormalVector   : NORMAL;
    float2 ParameterUv    : TEXCOORD0;
    float3 AuxColor       : COLOR0;
};

[shader("vertex")]
ComponentVertexOutput VertexMain(FloorVertexInput Input)
{
    float4 World4 = mul(SurfacePush.WorldTransform, float4(Input.PositionMeters, 1.0));

    ComponentVertexOutput Out;
    Out.ClipPosition = mul(Viewport.ViewProjection, World4);
    Out.WorldPos     = World4.xyz;
    Out.WorldNormal  = float3(0.0, 1.0, 0.0);
    Out.LocalPos     = Input.PositionMeters;
    Out.LocalNormal  = float3(0.0, 1.0, 0.0);
    Out.ParameterUv  = Input.ParameterUv;
    Out.VertexColor  = Input.AuxColor;
    return Out;
}

float CeEvaluateFloorDots(float2 P, float Spacing, float Radius, float PixelSpan)
{
    float2 Grid = (frac(P / Spacing + 0.5) - 0.5) * Spacing;
    return 1.0 - smoothstep(Radius - PixelSpan, Radius + PixelSpan, length(Grid));
}

[shader("fragment")]
ComponentFragmentOutput FragmentMain(ComponentVertexOutput In)
{
    float2 P         = In.WorldPos.xz;
    float  PixelSpan = max(fwidth(P.x), fwidth(P.y));
    float  Major     = CeEvaluateFloorDots(P, 1.0,  max(0.018, PixelSpan * 1.1), PixelSpan) * (1.0 - smoothstep(0.06, 0.30, PixelSpan));
    float  Minor     = CeEvaluateFloorDots(P, 0.25, max(0.007, PixelSpan * 0.8), PixelSpan) * (1.0 - smoothstep(0.012, 0.045, PixelSpan));

    float2 Cell = floor(P + 0.5);
    float3 DotColor = float3(0.42, 0.44, 0.50);
    if (abs(Cell.y) < 0.5)
    {
        DotColor = float3(0.85, 0.36, 0.33); // +X axis red
    }
    if (abs(Cell.x) < 0.5)
    {
        DotColor = float3(0.33, 0.52, 0.88); // +Z axis blue
    }

    float FocusDist = distance(In.WorldPos.xz, Viewport.FocusPointRadius.xz);
    float FadeRad   = max(Viewport.FocusPointRadius.w, 6.0);
    float Fade      = 1.0 - smoothstep(FadeRad * 0.25, FadeRad, FocusDist);
    float Pool      = (1.0 - smoothstep(0.0, FadeRad * 0.6, FocusDist)) * 0.05;
    float DotAlpha  = max(Major * 0.8, Minor * 0.4) * Fade;
    float3 FinalRgb = lerp(float3(0.09, 0.09, 0.11), DotColor, step(0.001, DotAlpha));

    ComponentFragmentOutput Out;
    Out.ColorRGBA = float4(FinalRgb, clamp(DotAlpha + Pool, 0.0, 1.0));
    Out.PickIDs   = uint4(0u, 0u, 0u, 0u);
    return Out;
}
