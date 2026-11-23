/*==========================================================================================================================================
                                                           BROWSERPREVIEW.SLANG
==========================================================================================================================================*/
// 🧩 Project Browser EON/GGX scene rasterization transcribed 1:1 from the reference WebGL2 renderer.
// 📝 Source: Experimental/Project browser/ReferenceRenderer.js SceneVertexSource + SceneFragmentSource.
// 📝 Surface defaults are compile-time constants: the reference uploads identical values every frame, converted here
//    once from sRGB with the same (.04045 / 12.92 / 1.055 ^ 2.4) transfer, so no uniform buffer is needed.

#include "BrowserProjection.slang"

[[vk::push_constant]] ConstantBuffer<BrowserPreviewProjection> Projection;
[[vk::binding(0, 0)]] Texture2D<float4> EonTexture;
[[vk::binding(1, 0)]] SamplerState EonSampler;

static const float BrowserPi = 3.14159265359;

// 📝 SceneSurfaceDefaults linear albedo + metallic, in reference order (housing, windings, shaft, seals, plate).
static const float4 BrowserAlbedoMetallic[7] =
{
    float4(0.242281122, 0.445201195, 0.366252596, 0.65),
    float4(0.693871761, 0.300543794, 0.130136477, 1.0),
    float4(0.558340390, 0.610495571, 0.644479682, 1.0),
    float4(0.0212190104, 0.0251868596, 0.0241576324, 0.0),
    float4(0.174647404, 0.212230757, 0.223227957, 0.85),
    float4(0, 0, 0, 0),
    float4(0, 0, 0, 0),
};
static const float2 BrowserSpecularRoughness[7] =
{
    float2(0.5, 0.3),
    float2(0.5, 0.24),
    float2(0.5, 0.18),
    float2(0.5, 0.7),
    float2(0.5, 0.4),
    float2(0, 1),
    float2(0, 1),
};

struct BrowserSceneInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] float3 Normal : NORMAL0;
    [[vk::location(2)]] float Surface : TEXCOORD0;
    [[vk::location(3)]] float3 Separation : TEXCOORD1;
};
struct BrowserScenePixel
{
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    nointerpolation int SurfaceIndex : TEXCOORD2;
};

[shader("vertex")]
BrowserScenePixel BrowserPreviewVertex(BrowserSceneInput Corner)
{
    BrowserScenePixel Pixel;
    Pixel.WorldPosition = Corner.Position + Corner.Separation * Projection.RasterOptions.x;
    Pixel.WorldNormal = Corner.Normal;
    Pixel.SurfaceIndex = int(Corner.Surface + 0.5);
    Pixel.Position = mul(Projection.ClipFromSurface, float4(Pixel.WorldPosition, 1));
    return Pixel;
}

float BrowserFifth(float X) { float X2 = X * X; return X2 * X2 * X; }
float3 BrowserFresnel(float3 F0, float Cosine) { return F0 + (1.0 - F0) * BrowserFifth(1.0 - Cosine); }
float2 BrowserEnvironmentIntegral(float NoV, float Roughness)
{
    float4 Fit = Roughness * float4(-1.0, -0.0275, -0.572, 0.022) + float4(1.0, 0.0425, 1.04, -0.04);
    float A = min(Fit.x * Fit.x, exp2(-9.28 * NoV)) * Fit.x + Fit.y;
    return float2(-1.04, 1.04) * A + Fit.zw;
}
float BrowserEonMissing(float Cosine, float Roughness)
{
    // 📝 Single-level lookup: explicit LOD 0 equals the reference implicit sample without divergent derivatives.
    return EonTexture.SampleLevel(EonSampler, float2((Cosine * 63.0 + 0.5) / 64.0, (Roughness * 31.0 + 0.5) / 32.0), 0)
               .r *
           0.25;
}
float3 BrowserDiffuseEon(float3 Albedo, float Roughness, float NoV, float NoL, float VoL)
{
    float Correlation = VoL - NoV * NoL;
    float Angular = Correlation > 0.0 ? Correlation / max(NoV, NoL) : Correlation;
    float A = 1.0 / (1.0 + (0.5 - 2.0 / (3.0 * BrowserPi)) * Roughness);
    float Average = A * (1.0 + (2.0 / 3.0 - 28.0 / (15.0 * BrowserPi)) * Roughness);
    float3 Multiple = Albedo * Albedo * Average / (1.0 - Albedo * max(0.0, 1.0 - Average));
    return (Albedo * A * (1.0 + Roughness * Angular) +
               Multiple * BrowserEonMissing(NoV, Roughness) * BrowserEonMissing(NoL, Roughness) / max(1e-5, 1.0 - Average)) /
           BrowserPi;
}
float3 BrowserEvaluateLight(float3 Normal, float3 View, float3 Light, float3 Energy, float3 Albedo, float3 F0,
    float Metallic, float Roughness, float3 Compensation)
{
    float NoL = max(dot(Normal, Light), 0.0);
    if (NoL <= 0.0) return float3(0.0);
    float NoV = max(dot(Normal, View), 0.0001);
    float3 HalfDirection = normalize(View + Light);
    float NoH = max(dot(Normal, HalfDirection), 0.0);
    float VoH = max(dot(View, HalfDirection), 0.0);
    float Alpha = Roughness * Roughness, Alpha2 = Alpha * Alpha;
    float Denominator = NoH * NoH * (Alpha2 - 1.0) + 1.0;
    float Distribution = Alpha2 / (BrowserPi * Denominator * Denominator);
    float Visibility = 0.5 /
                       max(NoL * sqrt(NoV * NoV * (1.0 - Alpha2) + Alpha2) + NoV * sqrt(NoL * NoL * (1.0 - Alpha2) + Alpha2),
                           0.00001);
    float3 F = BrowserFresnel(F0, VoH);
    float3 Diffuse =
        BrowserDiffuseEon(Albedo, Roughness, NoV, NoL, dot(View, Light)) * (1.0 - Metallic) * (1.0 - F);
    return (Diffuse + Distribution * Visibility * F * Compensation) * Energy * NoL;
}
float3 BrowserToSrgb(float3 Linear)
{
    return lerp(12.92 * Linear, 1.055 * pow(max(Linear, float3(0.0)), float3(1.0 / 2.4)) - 0.055,
        step(float3(0.0031308), Linear));
}

[shader("fragment")]
float4 BrowserPreviewFragment(BrowserScenePixel Pixel) : SV_Target
{
    int Channel = int(Projection.EyeChannel.w + 0.5);
    bool GridEnabled = Projection.RasterOptions.y > 0.5;
    int SurfaceIndex = Pixel.SurfaceIndex;
    if (Channel == 5)
    {
        // Opaque black depth coverage with optional faint grid; no material lighting.
        if (SurfaceIndex == 6 || (SurfaceIndex == 5 && !GridEnabled)) discard;
        float GridInk = SurfaceIndex == 5 ? 0.055 * (1.0 - smoothstep(4.0, 9.0, length(Pixel.WorldPosition.xz))) : 0.0;
        return float4(float3(GridInk), 1.0);
    }
    if (SurfaceIndex == 5 && !GridEnabled) discard;
    if (SurfaceIndex >= 5)
    {
        float Fade = 1.0 - smoothstep(4.0, 9.0, length(Pixel.WorldPosition.xz));
        float Grey = SurfaceIndex == 5 ? 0.085 * Fade : 0.008;
        return float4(float3(Grey), 1.0);
    }
    float3 Albedo = BrowserAlbedoMetallic[SurfaceIndex].rgb;
    float Metallic = BrowserAlbedoMetallic[SurfaceIndex].a;
    float Specular = BrowserSpecularRoughness[SurfaceIndex].x;
    float Roughness = max(0.04, BrowserSpecularRoughness[SurfaceIndex].y);
    float3 F0 = lerp(float3(0.16 * Specular * Specular), Albedo, Metallic);
    if (Channel != 0)
    {
        float3 Display = Channel == 1 ? BrowserToSrgb(Albedo)
                         : Channel == 2 ? BrowserToSrgb(F0)
                                        : float3(Channel == 3 ? Metallic : Roughness);
        return float4(Display, 1.0);
    }
    float3 Normal = normalize(Pixel.WorldNormal), View = normalize(Projection.EyeChannel.xyz - Pixel.WorldPosition);
    if (dot(Normal, View) < 0.0) Normal = -Normal;
    float NoV = max(dot(Normal, View), 0.0001);
    float2 Dfg = BrowserEnvironmentIntegral(NoV, Roughness);
    float3 Compensation = 1.0 + F0 * (1.0 / max(Dfg.x + Dfg.y, 0.001) - 1.0);
    float3 Radiance = BrowserEvaluateLight(Normal, View, normalize(float3(2, 5, 3)), float3(3.6, 3.9, 4.2), Albedo,
        F0, Metallic, Roughness, Compensation);
    Radiance += BrowserEvaluateLight(Normal, View, normalize(float3(-3, 2, -4)), float3(2.5, 1.9, 1.3), Albedo, F0,
        Metallic, Roughness, Compensation);
    // Analytic studio fill, not image-based lighting or a physical environment convolution.
    float3 Reflection = reflect(-View, Normal);
    float Softbox =
        pow(max(dot(Reflection, normalize(float3(-0.5, 1.0, 1.0))), 0.0), lerp(100.0, 2.0, Roughness));
    float3 Studio =
        lerp(float3(0.06, 0.07, 0.08), float3(0.65, 0.72, 0.78), smoothstep(-0.2, 0.9, Reflection.y)) + float3(1.8) * Softbox;
    Radiance += Studio * (F0 * Dfg.x + Dfg.y) * Compensation;
    Radiance += Albedo * (1.0 - Metallic) * (1.0 - BrowserFresnel(F0, NoV)) * (0.08 + 0.2 * max(Normal.y, 0.0));
    // A single display transform. This standalone target is not the engine HDR radiance target.
    float3 Mapped = clamp((Radiance * (2.51 * Radiance + 0.03)) / (Radiance * (2.43 * Radiance + 0.59) + 0.14), 0.0, 1.0);
    return float4(BrowserToSrgb(Mapped), 1.0);
}
