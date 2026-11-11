/*============================================================================================================================================
                                                       SURFACESHADING.CPP
============================================================================================================================================*/
// 🧩 Deterministic CPU reference for directional percentage-closer filtering and fast surface radiance

#include "Engine/GeometricRaster/SurfaceShading.h"

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIGHTING CALCULATIONS
//------------------------------------------------------------------------------------------------------------------------

float SampleDirectionalShadowVisibility(
    float                              LightU,
    float                              LightV,
    float                              ReceiverDepth,
    const DirectionalShadowDescription& Shadow) noexcept
{
    if (LightU < 0.0f || LightU > 1.0f || LightV < 0.0f || LightV > 1.0f || ReceiverDepth > 1.0f)
    {
        return 1.0f;
    }
    if (Shadow.DepthTexels.empty() || Shadow.WidthPx == 0u || Shadow.HeightPx == 0u ||
        Shadow.DepthTexels.size() < static_cast<std::size_t>(Shadow.WidthPx) * Shadow.HeightPx)
    {
        return 1.0f;
    }

    const float SampleX = LightU * static_cast<float>(Shadow.WidthPx - 1u);
    const float SampleY = LightV * static_cast<float>(Shadow.HeightPx - 1u);
    const int32_t CenterX = static_cast<int32_t>(SampleX + 0.5f);
    const int32_t CenterY = static_cast<int32_t>(SampleY + 0.5f);
    const int32_t Radius = static_cast<int32_t>(std::min(Shadow.FilterRadiusPx, 8u));
    uint32_t VisibleSamples = 0u;
    uint32_t TotalSamples = 0u;

    for (int32_t OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
    {
        for (int32_t OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
        {
            const int32_t TexelX = std::clamp(CenterX + OffsetX, 0, static_cast<int32_t>(Shadow.WidthPx - 1u));
            const int32_t TexelY = std::clamp(CenterY + OffsetY, 0, static_cast<int32_t>(Shadow.HeightPx - 1u));
            const std::size_t TexelIndex = static_cast<std::size_t>(TexelY) * Shadow.WidthPx + static_cast<std::size_t>(TexelX);
            const float NearestDepth = Shadow.DepthTexels[TexelIndex];
            VisibleSamples += ReceiverDepth - Shadow.DepthBias <= NearestDepth ? 1u : 0u;
            ++TotalSamples;
        }
    }
    return static_cast<float>(VisibleSamples) / static_cast<float>(TotalSamples);
}

LinearColor EvaluateFastSurfaceLighting(
    DirectionVector                   SurfaceNormal,
    DirectionVector                   ViewDirection,
    const DirectionalLightDescription& Light,
    const EonSpecularDescription&      Specular,
    LinearColor                       MatcapRadiance,
    float                             ShadowVisibility) noexcept
{
    const DirectionVector Normal = NormalizeDirection(SurfaceNormal);
    const float DiffuseCosine = std::max(0.0f, DotDirection(Normal, NormalizeDirection(Light.DirectionFromSurface)));
    const float Visibility = std::clamp(ShadowVisibility, 0.0f, 1.0f);
    const float DirectDiffuse = DiffuseCosine * Visibility;
    const float FillDiffuse = std::max(0.0f, Light.AmbientIntensity);
    const float EonEnergy = EvaluateEonSpecular(Normal, ViewDirection, Light.DirectionFromSurface, Specular.Shininess);
    const float SpecularEnergy = EonEnergy * std::max(0.0f, Specular.Intensity) * Visibility;

    const LinearColor Diffuse = ModulateRadiance(MatcapRadiance, ScaleRadiance(Light.Radiance, DirectDiffuse + FillDiffuse));
    const LinearColor Highlights = ModulateRadiance(Specular.Reflectance, ScaleRadiance(Light.Radiance, SpecularEnergy));
    return AddRadiance(Diffuse, Highlights);
}

} // namespace Frontier::GeometricRaster
