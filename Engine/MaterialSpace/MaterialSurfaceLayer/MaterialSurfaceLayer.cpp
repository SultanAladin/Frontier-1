/*============================================================================================================================================
                                                           MATERIALSURFACELAYER.CPP
============================================================================================================================================*/
// 🧩 Implementation of vertical optical layer composition (coverage, relief height blend, dielectric coat over, moisture film)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/MaterialSurfaceLayer/MaterialSurfaceLayer.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace
{

namespace
{

[[nodiscard]] Mathematics::LinearRGB LerpColor(Mathematics::LinearRGB A, Mathematics::LinearRGB B, float T) noexcept
{
    const float ClampedT = std::clamp(T, 0.0f, 1.0f);
    return {
        A.R + (B.R - A.R) * ClampedT,
        A.G + (B.G - A.G) * ClampedT,
        A.B + (B.B - A.B) * ClampedT,
    };
}

[[nodiscard]] float LerpScalar(float A, float B, float T) noexcept
{
    const float ClampedT = std::clamp(T, 0.0f, 1.0f);
    return A + (B - A) * ClampedT;
}

[[nodiscard]] float ComputeEffectiveBlendFactor(const MaterialSurfaceLayer& Layer) noexcept
{
    const float Opacity = std::clamp(Layer.CoverageOpacity, 0.0f, 1.0f);
    if (Layer.BlendKind != MaterialLayerBlendCategory::HeightThresholdBlend)
    {
        return Opacity;
    }

    const float Width     = std::max(0.02f, 1.0f - std::clamp(Layer.HeightContrastSharpness, 0.0f, 0.98f));
    const float Threshold = 1.0f - Opacity;
    const float RawT      = std::clamp((Layer.SampledMicroHeight - Threshold + 0.5f * Width) / Width, 0.0f, 1.0f);
    return RawT * RawT * (3.0f - 2.0f * RawT);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            STACK MANAGEMENT & FLATTENING
//------------------------------------------------------------------------------------------------------------------------

bool MaterialSurfaceStack::AppendLayer(const MaterialSurfaceLayer& NewLayer) noexcept
{
    if (ActiveLayerCount >= MaxMaterialSurfaceLayers)
    {
        return false;
    }
    Layers[ActiveLayerCount++] = NewLayer;
    return true;
}

SurfaceMaterial FlattenMaterialSurfaceStack(std::span<const MaterialSurfaceLayer> OrderedLayers)
{
    SurfaceMaterial Composite {};
    bool InitializedBase = false;

    for (const MaterialSurfaceLayer& Entry : OrderedLayers)
    {
        if (!Entry.LayerEnabled)
        {
            continue;
        }

        if (!InitializedBase)
        {
            Composite       = Entry.LayerMaterial;
            InitializedBase = true;
            SanitizeSurfaceMaterial(Composite);
            continue;
        }

        const float Alpha = ComputeEffectiveBlendFactor(Entry);
        if (Alpha <= 1.0e-5f)
        {
            continue;
        }

        const SurfaceMaterial& Top = Entry.LayerMaterial;

        if (Entry.BlendKind == MaterialLayerBlendCategory::OpticalClearcoatOver)
        {
            // 📝 Layering a dielectric varnish/lacquer coat over existing substrate widens underlying roughness (OpenPBR Eq. 44)
            Composite.Clearcoat.CoatWeight              = std::clamp(Composite.Clearcoat.CoatWeight + Alpha * Top.Clearcoat.CoatWeight, 0.0f, 1.0f);
            Composite.Clearcoat.CoatRoughness           = LerpScalar(Composite.Clearcoat.CoatRoughness, Top.Clearcoat.CoatRoughness, Alpha);
            Composite.Clearcoat.CoatIOR                 = LerpScalar(Composite.Clearcoat.CoatIOR, Top.Clearcoat.CoatIOR, Alpha);
            Composite.Clearcoat.CoatAbsorptionTint      = LerpColor(Composite.Clearcoat.CoatAbsorptionTint, Top.Clearcoat.CoatAbsorptionTint, Alpha);
            Composite.Clearcoat.CoatThicknessMillimeter += Alpha * Top.Clearcoat.CoatThicknessMillimeter;
        }
        else if (Entry.BlendKind == MaterialLayerBlendCategory::MoisturePorosityFilm)
        {
            Composite.PorosityLight.SurfaceWetnessFraction = std::clamp(
                Composite.PorosityLight.SurfaceWetnessFraction + Alpha * std::max(0.25f, Top.PorosityLight.SurfaceWetnessFraction),
                0.0f,
                1.0f);
            Composite.Specular.Roughness = LerpScalar(Composite.Specular.Roughness, 0.04f, 0.65f * Alpha);
        }
        else
        {
            Composite.MaterialName             = Top.MaterialName;
            Composite.Diffuse.BaseColor        = LerpColor(Composite.Diffuse.BaseColor, Top.Diffuse.BaseColor, Alpha);
            Composite.Diffuse.BaseWeight       = LerpScalar(Composite.Diffuse.BaseWeight, Top.Diffuse.BaseWeight, Alpha);
            Composite.Diffuse.DiffuseRoughness = LerpScalar(Composite.Diffuse.DiffuseRoughness, Top.Diffuse.DiffuseRoughness, Alpha);

            Composite.Specular.Metalness       = LerpScalar(Composite.Specular.Metalness, Top.Specular.Metalness, Alpha);
            Composite.Specular.SpecularWeight  = LerpScalar(Composite.Specular.SpecularWeight, Top.Specular.SpecularWeight, Alpha);
            Composite.Specular.Roughness       = LerpScalar(Composite.Specular.Roughness, Top.Specular.Roughness, Alpha);
            Composite.Specular.Anisotropy      = LerpScalar(Composite.Specular.Anisotropy, Top.Specular.Anisotropy, Alpha);
            Composite.Specular.DielectricIOR   = LerpScalar(Composite.Specular.DielectricIOR, Top.Specular.DielectricIOR, Alpha);
            Composite.Specular.EdgeTintF82     = LerpColor(Composite.Specular.EdgeTintF82, Top.Specular.EdgeTintF82, Alpha);

            Composite.Clearcoat.CoatWeight     = LerpScalar(Composite.Clearcoat.CoatWeight, Top.Clearcoat.CoatWeight, Alpha);
            Composite.Clearcoat.CoatRoughness  = LerpScalar(Composite.Clearcoat.CoatRoughness, Top.Clearcoat.CoatRoughness, Alpha);

            Composite.ThinFilm.ThinFilmWeight              = LerpScalar(Composite.ThinFilm.ThinFilmWeight, Top.ThinFilm.ThinFilmWeight, Alpha);
            Composite.ThinFilm.ThinFilmThicknessNanometers = LerpScalar(Composite.ThinFilm.ThinFilmThicknessNanometers, Top.ThinFilm.ThinFilmThicknessNanometers, Alpha);

            Composite.GlintFlakes.FlakeCoverage     = LerpScalar(Composite.GlintFlakes.FlakeCoverage, Top.GlintFlakes.FlakeCoverage, Alpha);
            Composite.GlintFlakes.FlakePrimaryTint  = LerpColor(Composite.GlintFlakes.FlakePrimaryTint, Top.GlintFlakes.FlakePrimaryTint, Alpha);

            Composite.Transmission.TransmissionWeight = LerpScalar(Composite.Transmission.TransmissionWeight, Top.Transmission.TransmissionWeight, Alpha);
            Composite.Subsurface.SubsurfaceWeight     = LerpScalar(Composite.Subsurface.SubsurfaceWeight, Top.Subsurface.SubsurfaceWeight, Alpha);
            Composite.Subsurface.SubsurfaceColor      = LerpColor(Composite.Subsurface.SubsurfaceColor, Top.Subsurface.SubsurfaceColor, Alpha);

            Composite.Sheen.SheenWeight    = LerpScalar(Composite.Sheen.SheenWeight, Top.Sheen.SheenWeight, Alpha);
            Composite.Sheen.SheenRoughness = LerpScalar(Composite.Sheen.SheenRoughness, Top.Sheen.SheenRoughness, Alpha);
            Composite.Sheen.SheenColor     = LerpColor(Composite.Sheen.SheenColor, Top.Sheen.SheenColor, Alpha);

            Composite.Fiber.FiberWeight = LerpScalar(Composite.Fiber.FiberWeight, Top.Fiber.FiberWeight, Alpha);

            Composite.PorosityLight.PorosityFraction           = LerpScalar(Composite.PorosityLight.PorosityFraction, Top.PorosityLight.PorosityFraction, Alpha);
            Composite.PorosityLight.SurfaceWetnessFraction     = LerpScalar(Composite.PorosityLight.SurfaceWetnessFraction, Top.PorosityLight.SurfaceWetnessFraction, Alpha);
            Composite.PorosityLight.EmissionLuminanceNits      = LerpScalar(Composite.PorosityLight.EmissionLuminanceNits, Top.PorosityLight.EmissionLuminanceNits, Alpha);
            Composite.PorosityLight.BlackbodyTemperatureKelvin = LerpScalar(Composite.PorosityLight.BlackbodyTemperatureKelvin, Top.PorosityLight.BlackbodyTemperatureKelvin, Alpha);
        }
    }

    SanitizeSurfaceMaterial(Composite);
    return Composite;
}

SurfaceMaterial FlattenMaterialSurfaceStack(const MaterialSurfaceStack& Stack)
{
    const std::size_t SafeCount = std::min<std::size_t>(Stack.ActiveLayerCount, MaxMaterialSurfaceLayers);
    return FlattenMaterialSurfaceStack(std::span<const MaterialSurfaceLayer>(Stack.Layers.data(), SafeCount));
}

} // namespace Frontier::MaterialSpace
