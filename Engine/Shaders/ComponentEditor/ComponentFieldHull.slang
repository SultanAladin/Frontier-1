/*============================================================================================================================================
                                                        COMPONENTFIELDSHELL.SLANG
============================================================================================================================================*/
// 🧩 Fresnel rim, soft box face borders, rising world-Y scan band, and analytical hull shell shader (no wireframe line cages)

#include "Engine/Shaders/ComponentEditor/ComponentEditorShared.slang"

[[vk::push_constant]]
ConstantBuffer<ComponentSurfacePushConstants> SurfacePush;

[[vk::binding(0, 0)]]
ConstantBuffer<ComponentViewportUniforms> Viewport;

struct HullVertexInput
{
    float3 PositionMeters : POSITION;
    float3 NormalVector   : NORMAL;
    float2 ParameterUv    : TEXCOORD0;
    float3 AuxColor       : COLOR0;
};

[shader("vertex")]
ComponentVertexOutput VertexMain(HullVertexInput Input)
{
    float4 World4 = mul(SurfacePush.WorldTransform, float4(Input.PositionMeters, 1.0));
    float3x3 M3   = (float3x3)SurfacePush.WorldTransform;

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

[shader("fragment")]
ComponentFragmentOutput FragmentMain(ComponentVertexOutput In, bool FrontFacing : SV_IsFrontFace)
{
    float3 N = CeNormalizeSafe(In.WorldNormal, float3(0.0, 1.0, 0.0));
    if (!FrontFacing)
    {
        N = -N;
    }
    float3 V      = CeNormalizeSafe(Viewport.CameraPositionTime.xyz - In.WorldPos, float3(0.0, 0.0, 1.0));
    float  Facing = abs(dot(N, V));
    float  Rim    = pow(1.0 - Facing, 2.2);

    // ChannelModifiers: x = SelectedHighlight, y = BaseAlpha, z = RimWeight, w = ScanWeight
    // EmissiveMetalness: r = BorderWeight, g = VoxelDotsPerMeter, b = PulseWeight
    float BaseW   = SurfacePush.ChannelModifiers.y;
    float RimW    = SurfacePush.ChannelModifiers.z;
    float ScanW   = SurfacePush.ChannelModifiers.w;
    float BorderW = SurfacePush.EmissiveMetalness.r;
    float DotsPM  = SurfacePush.EmissiveMetalness.g;
    float PulseW  = SurfacePush.EmissiveMetalness.b;
    float TimeSec = Viewport.CameraPositionTime.w;

    float Alpha = BaseW + RimW * Rim;

    if (BorderW > 0.0)
    {
        float2 EdgePair = min(In.ParameterUv, 1.0 - In.ParameterUv);
        float  EdgeDist = min(EdgePair.x, EdgePair.y);
        float  Gradient = 1.0 - smoothstep(0.0, 0.14, EdgeDist);
        Alpha += BorderW * Gradient * Gradient;
    }

    if (ScanW > 0.0)
    {
        float Band = pow(0.5 + 0.5 * sin(In.WorldPos.y * 14.0 - TimeSec * 2.4), 18.0);
        Alpha += ScanW * Band;
    }

    if (DotsPM > 0.0)
    {
        float3 AbsN = abs(In.LocalNormal);
        float2 FaceUv = (AbsN.x > AbsN.y && AbsN.x > AbsN.z)
            ? In.LocalPos.yz
            : ((AbsN.y > AbsN.z) ? In.LocalPos.xz : In.LocalPos.xy);
        float2 Cell = frac(FaceUv * DotsPM) - 0.5;
        Alpha += 0.55 * (1.0 - smoothstep(0.10, 0.22, length(Cell)));
    }

    if (PulseW > 0.0)
    {
        Alpha *= 1.0 - PulseW * 0.5 + PulseW * 0.5 * sin(TimeSec * 4.0);
    }

    float3 HullRgb = SurfacePush.AlbedoRoughness.rgb * (0.75 + 0.60 * Rim);

    ComponentFragmentOutput Out;
    Out.ColorRGBA = float4(HullRgb, clamp(Alpha, 0.0, 1.0));
    Out.PickIDs   = uint4(SurfacePush.PickPacked.x, SurfacePush.PickPacked.y, SurfacePush.PickPacked.z, 0u);
    return Out;
}
