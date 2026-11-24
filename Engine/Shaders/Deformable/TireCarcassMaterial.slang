//============================================================================================================================================
//                                                     TIRECARCASSMATERIAL.SLANG
//============================================================================================================================================
// Phase D06 Sculpted Sport Tire Fragment Shader: zone-aware PBR rubber shading + micro-siping + 3D strain heatmap overlay.

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<VertexMotionAtlasUniforms> gVat;

struct MaterialCameraUniforms
{
    float4x4 ViewProj;
    float4   CameraPosition;
    float4   KeyLightDir;
};
[[vk::binding(4, 0)]] ConstantBuffer<MaterialCameraUniforms> gMaterialCam;

struct TireSurfaceIn
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : TEXCOORD1;
    float2 TexCoord     : TEXCOORD2;
    float  ZoneId       : TEXCOORD3;
    float  Strain       : TEXCOORD4;
};

float3 StrainHeatmapColor(float strain)
{
    float t = clamp(strain * 1.4, 0.0, 1.0);
    float3 cool = float3(0.11, 0.56, 0.92);
    float3 mid  = float3(0.95, 0.72, 0.14);
    float3 hot  = float3(0.95, 0.17, 0.14);
    return (t < 0.5) ? lerp(cool, mid, t * 2.0) : lerp(mid, hot, (t - 0.5) * 2.0);
}

[shader("fragment")]
float4 FsTireCarcassMaterial(TireSurfaceIn i) : SV_Target
{
    float3 N = normalize(i.WorldNormal);
    float3 V = normalize(gMaterialCam.CameraPosition.xyz - i.WorldPos);
    float3 L = normalize(gMaterialCam.KeyLightDir.xyz);
    float3 H = normalize(L + V);

    // Zone-specific vulcanized rubber base albedo & roughness
    // Zone 0 = smooth tread groove, 1 = raised chevron lug, 2 = smooth sidewall, 3 = sidewall scuff pad
    float3 albedo = float3(0.085, 0.090, 0.100);
    float  rough  = 0.72;
    if (i.ZoneId > 0.5 && i.ZoneId < 1.5)
    {
        albedo = float3(0.068, 0.072, 0.080);
        rough  = 0.62;
    }
    else if (i.ZoneId > 1.5 && i.ZoneId < 2.5)
    {
        albedo = float3(0.095, 0.100, 0.112);
        rough  = 0.54;
    }
    else if (i.ZoneId > 2.5)
    {
        albedo = float3(0.125, 0.130, 0.142);
        rough  = 0.48;
    }

    if (gVat.BlendFlags.z > 0.5)
    {
        albedo = lerp(albedo, StrainHeatmapColor(i.Strain), 0.78);
    }

    float ndl = max(dot(N, L), 0.0);
    float ndh = max(dot(N, H), 0.0);
    float ndv = max(dot(N, V), 0.0);

    float specExp = lerp(18.0, 96.0, 1.0 - rough);
    float spec = pow(ndh, specExp) * lerp(0.06, 0.24, 1.0 - rough);
    float fresnel = pow(1.0 - ndv, 4.0) * 0.16;
    float hemi = 0.5 + 0.5 * N.y;

    float3 ambient = albedo * lerp(float3(0.16, 0.17, 0.20), float3(0.32, 0.36, 0.42), hemi);
    float3 lit = ambient + albedo * ndl * 1.35 + float3(spec + fresnel);

    return float4(lit, 1.0);
}
