/*============================================================================================================================================
                                                    COMPONENTCHANNELINSPECTION.SLANG
============================================================================================================================================*/
// 🧩 Compiled geometry shader supporting OpenPbrDirect (Zero GI) plus all 7 Component Editor inspection channels & MRT PickID

#include "Engine/Shaders/ComponentEditor/ComponentEditorShared.slang"

[[vk::push_constant]]
ConstantBuffer<ComponentSurfacePushConstants> DrawPush;

[[vk::binding(0, 0)]]
ConstantBuffer<ComponentViewportUniforms> Viewport;

struct GeometryVertexInput
{
    float3 PositionMeters : POSITION;
    float3 NormalVector   : NORMAL;
    float2 ParameterUv    : TEXCOORD0;
    float3 AuxColor       : COLOR0;
};

[shader("vertex")]
ComponentVertexOutput VertexMain(GeometryVertexInput Input)
{
    float4 World4 = mul(DrawPush.ModelMatrix, float4(Input.PositionMeters, 1.0));
    float3x3 M3   = (float3x3)DrawPush.ModelMatrix;

    ComponentVertexOutput Out;
    Out.ClipPosition = mul(Viewport.ViewProjection, World4);
    Out.WorldPos     = World4.xyz;
    Out.WorldNormal  = CeNormalizeSafe(mul(M3, Input.NormalVector), float3(0.0, 1.0, 0.0));
    Out.LocalPos     = Input.PositionMeters;
    Out.LocalNormal  = CeNormalizeSafe(Input.NormalVector, float3(0.0, 1.0, 0.0));
    Out.ParameterUv  = Input.ParameterUv;
    Out.VertexColor  = Input.AuxColor;
    return Out;
}

float3 CeHslToRgb(float H, float S, float L)
{
    float3 K = frac(float3(H, H, H) + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0;
    float3 P = saturate(abs(K) - 1.0);
    return L + S * (P - 0.5) * (1.0 - abs(2.0 * L - 1.0));
}

[shader("fragment")]
ComponentFragmentOutput FragmentMain(ComponentVertexOutput In)
{
    float3 N = CeNormalizeSafe(In.WorldNormal, float3(0.0, 1.0, 0.0));
    float3 V = CeNormalizeSafe(Viewport.CameraPositionTime.xyz - In.WorldPos, float3(0.0, 0.0, 1.0));

    uint   Channel = DrawPush.PickPacked.w;
    float3 Display = float3(0.0, 0.0, 0.0);
    float  Alpha   = saturate(DrawPush.ChannelModifiers.y);

    if (Channel == 0u) // Lit (OpenPbrDirect, Zero GI)
    {
        Display = CeEvaluateOpenPbrDirect(
            Viewport,
            In.WorldPos,
            N,
            V,
            DrawPush.AlbedoRoughness.rgb,
            DrawPush.AlbedoRoughness.w,
            DrawPush.EmissiveMetalness.w,
            DrawPush.EmissiveMetalness.rgb);
    }
    else if (Channel == 1u) // Unlit
    {
        Display = CeLinearToSrgb(saturate(DrawPush.AlbedoRoughness.rgb + DrawPush.EmissiveMetalness.rgb));
    }
    else if (Channel == 2u) // Density heat ramp (carried in VertexColor)
    {
        float Lambert = 0.68 + 0.32 * max(dot(N, float3(0.4, 0.8, 0.45)), 0.0);
        Display = saturate(In.VertexColor * Lambert);
    }
    else if (Channel == 3u) // Normals
    {
        Display = saturate(N * 0.5 + 0.5);
    }
    else if (Channel == 4u) // UV Checker (8x8 grid with missing-UV #7a2d5a indicator)
    {
        if (DrawPush.ChannelModifiers.z > 0.5)
        {
            Display = float3(0.478, 0.176, 0.353); // #7a2d5a missing UV indicator
        }
        else
        {
            float2 UvFrac = frac(In.ParameterUv);
            float2 Cell   = floor(UvFrac * 8.0);
            float  Parity = fmod(Cell.x + Cell.y, 2.0);
            float  Hue    = frac((Cell.x + Cell.y * 8.0) / 64.0 * 0.72 + 0.08);
            float3 Tile   = CeHslToRgb(Hue, 0.45, Parity > 0.5 ? 0.68 : 0.28);
            float2 Inner  = abs(frac(UvFrac * 8.0) - 0.5);
            float  Border = smoothstep(0.44, 0.49, max(Inner.x, Inner.y));
            float3 Ink    = lerp(Tile, float3(0.08, 0.09, 0.11), Border * 0.65);
            float  Shade  = 0.72 + 0.28 * max(dot(N, V), 0.0);
            Display = Ink * Shade;
        }
    }
    else if (Channel == 5u) // LOD clusters (golden-angle hue in VertexColor)
    {
        float Lambert = 0.56 + 0.44 * max(dot(N, normalize(float3(0.45, 0.8, 0.4))), 0.0);
        Display = saturate(In.VertexColor * Lambert);
    }
    else // Channel == 6u: Collision surface ghost
    {
        Display = CeEvaluateOpenPbrDirect(
            Viewport,
            In.WorldPos,
            N,
            V,
            DrawPush.AlbedoRoughness.rgb,
            DrawPush.AlbedoRoughness.w,
            DrawPush.EmissiveMetalness.w,
            float3(0.0, 0.0, 0.0));
        Alpha = min(Alpha, 0.35);
    }

    Display = CeBlendSelectionHalo(
        Display,
        N,
        V,
        Viewport.CameraPositionTime.w,
        DrawPush.ChannelModifiers.x,
        Viewport.HaloTintParams.rgb);

    ComponentFragmentOutput Out;
    Out.ColorRGBA = float4(Display, Alpha);
    Out.PickIDs   = uint4(DrawPush.PickPacked.x, DrawPush.PickPacked.y, DrawPush.PickPacked.z, Channel);
    return Out;
}
