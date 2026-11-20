/*============================================================================================================================================
                                                          BAKEVIEWPORTPREVIEW.SLANG
============================================================================================================================================*/
// 🧩 Interactive 3D studio PBR + MatCap preview shader sampling live GPU-baked Tangent Normal, AO, Curvature & inspected channel overlays,
// plus optional translucent cage & High-Poly silhouette wireframe overlays.

#include "BakeShaderLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<GpuPreviewConfig>     PreviewConfig;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLowSurfaceVertex> SurfaceVertices;
[[vk::binding(2, 0)]] Texture2DArray<float4>               BakedChannels;
[[vk::binding(3, 0)]] SamplerState                         BakedSampler;

struct PreviewVertexOut
{
    float4 ClipPos       : SV_Position;
    float3 WorldPos      : TEXCOORD0;
    float3 SmoothNormal  : TEXCOORD1;
    float3 Tangent       : TEXCOORD2;
    float3 Bitangent     : TEXCOORD3;
    float2 UvCoord       : TEXCOORD4;
    float2 BaryXY        : TEXCOORD5;
};

[shader("vertex")]
PreviewVertexOut VertexEntry(uint VertexId : SV_VertexID)
{
    GpuLowSurfaceVertex V = SurfaceVertices[VertexId];
    float OverlayMode = PreviewConfig.ViewportSizeTime.w;
    float3 PosWorld = V.Position;
    if (OverlayMode > 1.5) // Cage wireframe push along smooth normal
    {
        PosWorld += normalize(V.SmoothNormal) * PreviewConfig.OverlayFlagsPacked.z;
    }

    PreviewVertexOut Out;
    Out.ClipPos      = mul(PreviewConfig.ViewProjTransform, float4(PosWorld, 1.0));
    Out.WorldPos     = PosWorld;
    Out.SmoothNormal = V.SmoothNormal;
    Out.Tangent      = V.Tangent;
    Out.Bitangent    = V.Bitangent * V.Handedness;
    Out.UvCoord      = float2(V.UvCoordU, V.UvCoordV);
    Out.BaryXY       = float2(V.BarycentricX, V.BarycentricY);
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(PreviewVertexOut In) : SV_Target0
{
    float OverlayMode = PreviewConfig.ViewportSizeTime.w;
    if (OverlayMode > 0.5)
    {
        // High-poly wireframe or translucent front/rear cage shell overlay using analytic barycentric edge filter
        float3 Bary = float3(In.BaryXY, max(0.0, 1.0 - In.BaryXY.x - In.BaryXY.y));
        float3 FW   = max(fwidth(Bary), float3(1.0e-5));
        float3 Edge = smoothstep(float3(0.0), FW * 1.45, Bary);
        float  Wire = 1.0 - min(Edge.x, min(Edge.y, Edge.z));
        if (Wire < 0.04)
        {
            discard;
        }
        return float4(PreviewConfig.OverlayColor.rgb, Wire * PreviewConfig.OverlayColor.a);
    }

    float3 N = normalize(In.SmoothNormal);
    float3 T = normalize(In.Tangent - N * dot(N, In.Tangent));
    float3 B = normalize(In.Bitangent - N * dot(N, In.Bitangent));

    // Sample row 0 = V=1 top convention
    float2 SampleUv = float2(In.UvCoord.x, 1.0 - In.UvCoord.y);

    bool HasBakedSurface = (PreviewConfig.OverlayFlagsPacked.x > 0.5);
    float3 BaseLitCol    = float3(0.56, 0.59, 0.65);
    float3 ShadingN      = N;
    float  AoFactor      = 1.0;

    if (HasBakedSurface)
    {
        float4 TangN  = BakedChannels.SampleLevel(BakedSampler, float3(SampleUv, 0.0), 0.0);
        float4 AoSamp = BakedChannels.SampleLevel(BakedSampler, float3(SampleUv, 5.0), 0.0);
        if (TangN.a > 0.25)
        {
            float3 TS = TangN.xyz * 2.0 - 1.0;
            ShadingN = normalize(T * TS.x + B * TS.y + N * TS.z);
        }
        if (AoSamp.a > 0.25)
        {
            AoFactor = pow(saturate(AoSamp.r), 1.35);
        }

        uint InspectTarget = uint(PreviewConfig.CameraEyeMode.w + 0.5);
        if (InspectTarget >= 1u && InspectTarget <= 17u)
        {
            uint LayerIdx = InspectTarget - 1u;
            float4 InspCol = BakedChannels.SampleLevel(BakedSampler, float3(SampleUv, float(LayerIdx)), 0.0);
            if (InspCol.a > 0.25)
            {
                // For diagnostic masks/normals/curvature, visualize cleanly while keeping crisp micro-lighting
                if (LayerIdx != 0u)
                {
                    BaseLitCol = InspCol.rgb;
                    if (LayerIdx == 13u || LayerIdx == 14u || LayerIdx == 15u || LayerIdx == 16u || LayerIdx == 1u || LayerIdx == 2u)
                    {
                        float RimLight = 0.85 + 0.15 * saturate(dot(ShadingN, normalize(PreviewConfig.CameraEyeMode.xyz - In.WorldPos)));
                        return float4(BaseLitCol * RimLight, 1.0);
                    }
                }
            }
        }
    }

    // Studio 3-point key / cool sky fill / warm rim PBR-approximated shading (+Z Up)
    float3 VDir = normalize(PreviewConfig.CameraEyeMode.xyz - In.WorldPos);
    if (dot(N, VDir) < 0.0)
    {
        ShadingN = -ShadingN;
    }
    float3 KeyL = normalize(float3(0.58, -0.52, 0.63));
    float3 FillL = normalize(float3(-0.62, 0.45, 0.34));
    float3 RimL = normalize(float3(-0.35, 0.78, 0.52));

    float NdotK = saturate(dot(ShadingN, KeyL));
    float NdotF = saturate(dot(ShadingN, FillL));
    float Fresnel = pow(1.0 - saturate(dot(ShadingN, VDir)), 4.0);

    float3 HKey = normalize(KeyL + VDir);
    float  Spec = pow(saturate(dot(ShadingN, HKey)), 44.0) * 0.42;

    float3 Hemi = lerp(float3(0.06, 0.07, 0.10), float3(0.22, 0.26, 0.34), ShadingN.z * 0.5 + 0.5);
    float3 Lit  = BaseLitCol * (Hemi * AoFactor + float3(0.98, 0.94, 0.88) * NdotK * AoFactor * 0.86 + float3(0.26, 0.35, 0.52) * NdotF * AoFactor * 0.42);
    Lit += float3(1.0, 0.97, 0.92) * Spec * AoFactor;
    Lit += float3(0.66, 0.78, 0.98) * Fresnel * saturate(dot(ShadingN, RimL) * 0.5 + 0.5) * 0.32;

    // Crisp studio ACES-like tone curve -> sRGB
    float3 Tone = Lit / (Lit + 0.42);
    Tone = pow(saturate(Tone), float3(1.0 / 2.2));
    return float4(Tone, 1.0);
}
