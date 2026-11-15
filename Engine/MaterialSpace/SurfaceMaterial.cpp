/*============================================================================================================================================
                                                             SURFACEMATERIAL.CPP
============================================================================================================================================*/
// 🧩 Implementation of physical domain sanitization, invariant validation, and 10-lobe reference material presets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/SurfaceMaterial.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace
{

namespace
{

[[nodiscard]] Mathematics::LinearRGB ClampUnitColor(Mathematics::LinearRGB Value) noexcept
{
    return {
        std::clamp(std::isfinite(Value.R) ? Value.R : 0.0f, 0.0f, 1.0f),
        std::clamp(std::isfinite(Value.G) ? Value.G : 0.0f, 0.0f, 1.0f),
        std::clamp(std::isfinite(Value.B) ? Value.B : 0.0f, 0.0f, 1.0f),
    };
}

[[nodiscard]] float SanitizeScalar(float Value, float Lower, float Upper, float Fallback) noexcept
{
    if (!std::isfinite(Value))
    {
        return Fallback;
    }
    return std::clamp(Value, Lower, Upper);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            SANITIZATION & INVARIANT VALIDATION
//------------------------------------------------------------------------------------------------------------------------

void SanitizeSurfaceMaterial(SurfaceMaterial& InOutMaterial) noexcept
{
    if (InOutMaterial.MaterialName.empty())
    {
        InOutMaterial.MaterialName = "StandardOpenPbrSurface";
    }

    InOutMaterial.Diffuse.BaseColor        = ClampUnitColor(InOutMaterial.Diffuse.BaseColor);
    InOutMaterial.Diffuse.BaseWeight       = SanitizeScalar(InOutMaterial.Diffuse.BaseWeight, 0.0f, 1.0f, 1.0f);
    InOutMaterial.Diffuse.DiffuseRoughness = SanitizeScalar(InOutMaterial.Diffuse.DiffuseRoughness, 0.0f, 1.0f, 0.25f);

    InOutMaterial.Specular.Metalness       = SanitizeScalar(InOutMaterial.Specular.Metalness, 0.0f, 1.0f, 0.0f);
    InOutMaterial.Specular.SpecularWeight  = SanitizeScalar(InOutMaterial.Specular.SpecularWeight, 0.0f, 1.0f, 1.0f);
    InOutMaterial.Specular.Roughness       = SanitizeScalar(InOutMaterial.Specular.Roughness, 0.015f, 1.0f, 0.30f);
    InOutMaterial.Specular.Anisotropy      = SanitizeScalar(InOutMaterial.Specular.Anisotropy, -0.98f, 0.98f, 0.0f);
    InOutMaterial.Specular.RotationRadians = SanitizeScalar(InOutMaterial.Specular.RotationRadians, -Constants::TauFloat, Constants::TauFloat, 0.0f);
    InOutMaterial.Specular.DielectricIOR   = SanitizeScalar(InOutMaterial.Specular.DielectricIOR, 1.0f, 3.5f, 1.50f);
    InOutMaterial.Specular.EdgeTintF82     = ClampUnitColor(InOutMaterial.Specular.EdgeTintF82);

    InOutMaterial.Clearcoat.CoatWeight              = SanitizeScalar(InOutMaterial.Clearcoat.CoatWeight, 0.0f, 1.0f, 0.0f);
    InOutMaterial.Clearcoat.CoatRoughness           = SanitizeScalar(InOutMaterial.Clearcoat.CoatRoughness, 0.01f, 1.0f, 0.06f);
    InOutMaterial.Clearcoat.CoatAnisotropy          = SanitizeScalar(InOutMaterial.Clearcoat.CoatAnisotropy, -0.98f, 0.98f, 0.0f);
    InOutMaterial.Clearcoat.CoatIOR                 = SanitizeScalar(InOutMaterial.Clearcoat.CoatIOR, 1.0f, 3.0f, 1.52f);
    InOutMaterial.Clearcoat.CoatAbsorptionTint      = ClampUnitColor(InOutMaterial.Clearcoat.CoatAbsorptionTint);
    InOutMaterial.Clearcoat.CoatThicknessMillimeter = SanitizeScalar(InOutMaterial.Clearcoat.CoatThicknessMillimeter, 0.0f, 10.0f, 0.05f);

    InOutMaterial.ThinFilm.ThinFilmWeight              = SanitizeScalar(InOutMaterial.ThinFilm.ThinFilmWeight, 0.0f, 1.0f, 0.0f);
    InOutMaterial.ThinFilm.ThinFilmThicknessNanometers = SanitizeScalar(InOutMaterial.ThinFilm.ThinFilmThicknessNanometers, 0.0f, 2000.0f, 420.0f);
    InOutMaterial.ThinFilm.ThinFilmIOR                 = SanitizeScalar(InOutMaterial.ThinFilm.ThinFilmIOR, 1.0f, 3.5f, 1.90f);

    InOutMaterial.GlintFlakes.FlakeCoverage           = SanitizeScalar(InOutMaterial.GlintFlakes.FlakeCoverage, 0.0f, 1.0f, 0.0f);
    InOutMaterial.GlintFlakes.FlakeIntrinsicRoughness = SanitizeScalar(InOutMaterial.GlintFlakes.FlakeIntrinsicRoughness, 0.005f, 0.6f, 0.045f);
    InOutMaterial.GlintFlakes.FlakeOrientationSpread  = SanitizeScalar(InOutMaterial.GlintFlakes.FlakeOrientationSpread, 0.01f, 0.9f, 0.16f);
    InOutMaterial.GlintFlakes.FlakeLogDensityPerSqM   = SanitizeScalar(InOutMaterial.GlintFlakes.FlakeLogDensityPerSqM, 2.0f, 10.0f, 7.0f);
    InOutMaterial.GlintFlakes.FlakePrimaryTint        = ClampUnitColor(InOutMaterial.GlintFlakes.FlakePrimaryTint);
    InOutMaterial.GlintFlakes.FlakeHorizonTint        = ClampUnitColor(InOutMaterial.GlintFlakes.FlakeHorizonTint);
    InOutMaterial.GlintFlakes.FlakeBurialDepth        = SanitizeScalar(InOutMaterial.GlintFlakes.FlakeBurialDepth, 0.0f, 1.0f, 0.35f);

    InOutMaterial.Transmission.TransmissionWeight       = SanitizeScalar(InOutMaterial.Transmission.TransmissionWeight, 0.0f, 1.0f, 0.0f);
    InOutMaterial.Transmission.AbbeDispersionNumber     = SanitizeScalar(InOutMaterial.Transmission.AbbeDispersionNumber, 10.0f, 120.0f, 55.0f);
    InOutMaterial.Transmission.VolumeAnisotropyG        = SanitizeScalar(InOutMaterial.Transmission.VolumeAnisotropyG, -0.95f, 0.95f, 0.0f);
    InOutMaterial.Transmission.AttenuationColor         = ClampUnitColor(InOutMaterial.Transmission.AttenuationColor);
    InOutMaterial.Transmission.AttenuationDistanceMeter = SanitizeScalar(InOutMaterial.Transmission.AttenuationDistanceMeter, 1.0e-3f, 100.0f, 0.25f);

    InOutMaterial.Subsurface.SubsurfaceWeight           = SanitizeScalar(InOutMaterial.Subsurface.SubsurfaceWeight, 0.0f, 1.0f, 0.0f);
    InOutMaterial.Subsurface.SubsurfaceColor            = ClampUnitColor(InOutMaterial.Subsurface.SubsurfaceColor);
    InOutMaterial.Subsurface.MeanFreePathMillimeters.X  = SanitizeScalar(InOutMaterial.Subsurface.MeanFreePathMillimeters.X, 0.01f, 500.0f, 3.6f);
    InOutMaterial.Subsurface.MeanFreePathMillimeters.Y  = SanitizeScalar(InOutMaterial.Subsurface.MeanFreePathMillimeters.Y, 0.01f, 500.0f, 1.4f);
    InOutMaterial.Subsurface.MeanFreePathMillimeters.Z  = SanitizeScalar(InOutMaterial.Subsurface.MeanFreePathMillimeters.Z, 0.01f, 500.0f, 0.7f);
    InOutMaterial.Subsurface.PhaseAnisotropyG           = SanitizeScalar(InOutMaterial.Subsurface.PhaseAnisotropyG, -0.90f, 0.90f, 0.0f);

    InOutMaterial.Sheen.SheenWeight    = SanitizeScalar(InOutMaterial.Sheen.SheenWeight, 0.0f, 1.0f, 0.0f);
    InOutMaterial.Sheen.SheenRoughness = SanitizeScalar(InOutMaterial.Sheen.SheenRoughness, 0.05f, 1.0f, 0.45f);
    InOutMaterial.Sheen.SheenColor     = ClampUnitColor(InOutMaterial.Sheen.SheenColor);

    InOutMaterial.Fiber.FiberWeight                = SanitizeScalar(InOutMaterial.Fiber.FiberWeight, 0.0f, 1.0f, 0.0f);
    InOutMaterial.Fiber.LongitudinalRoughness      = SanitizeScalar(InOutMaterial.Fiber.LongitudinalRoughness, 0.02f, 1.0f, 0.22f);
    InOutMaterial.Fiber.AzimuthalRoughness         = SanitizeScalar(InOutMaterial.Fiber.AzimuthalRoughness, 0.02f, 1.0f, 0.28f);
    InOutMaterial.Fiber.CuticleTiltRadians         = SanitizeScalar(InOutMaterial.Fiber.CuticleTiltRadians, -0.35f, 0.35f, 0.035f);
    InOutMaterial.Fiber.MelaninConcentration       = SanitizeScalar(InOutMaterial.Fiber.MelaninConcentration, 0.0f, 1.0f, 0.45f);
    InOutMaterial.Fiber.PheomelaninRednessFraction = SanitizeScalar(InOutMaterial.Fiber.PheomelaninRednessFraction, 0.0f, 1.0f, 0.20f);

    InOutMaterial.PorosityLight.PorosityFraction           = SanitizeScalar(InOutMaterial.PorosityLight.PorosityFraction, 0.0f, 1.0f, 0.0f);
    InOutMaterial.PorosityLight.SurfaceWetnessFraction     = SanitizeScalar(InOutMaterial.PorosityLight.SurfaceWetnessFraction, 0.0f, 1.0f, 0.0f);
    InOutMaterial.PorosityLight.EmissionLuminanceNits      = SanitizeScalar(InOutMaterial.PorosityLight.EmissionLuminanceNits, 0.0f, 1.0e7f, 0.0f);
    InOutMaterial.PorosityLight.BlackbodyTemperatureKelvin = SanitizeScalar(InOutMaterial.PorosityLight.BlackbodyTemperatureKelvin, 0.0f, 20000.0f, 0.0f);
    InOutMaterial.PorosityLight.EmissionTint               = ClampUnitColor(InOutMaterial.PorosityLight.EmissionTint);
}

bool VerifySurfaceMaterialPhysicalBounds(const SurfaceMaterial& Material) noexcept
{
    const auto CheckUnitColor = [](Mathematics::LinearRGB Col) noexcept -> bool
    {
        return std::isfinite(Col.R) && Col.R >= 0.0f && Col.R <= 1.0f &&
               std::isfinite(Col.G) && Col.G >= 0.0f && Col.G <= 1.0f &&
               std::isfinite(Col.B) && Col.B >= 0.0f && Col.B <= 1.0f;
    };

    return CheckUnitColor(Material.Diffuse.BaseColor) &&
           CheckUnitColor(Material.Specular.EdgeTintF82) &&
           CheckUnitColor(Material.Clearcoat.CoatAbsorptionTint) &&
           CheckUnitColor(Material.GlintFlakes.FlakePrimaryTint) &&
           CheckUnitColor(Material.Sheen.SheenColor) &&
           Material.Specular.DielectricIOR >= 1.0f &&
           Material.Clearcoat.CoatIOR >= 1.0f &&
           Material.ThinFilm.ThinFilmIOR >= 1.0f &&
           Material.Specular.Roughness >= 0.01f && Material.Specular.Roughness <= 1.0f &&
           Material.Specular.Metalness >= 0.0f && Material.Specular.Metalness <= 1.0f &&
           Material.Transmission.TransmissionWeight >= 0.0f && Material.Transmission.TransmissionWeight <= 1.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           10-LOBE CANONICAL MATERIAL PRESETS
//------------------------------------------------------------------------------------------------------------------------

SurfaceMaterial ConstructMaterialPreset(MaterialPresetCategory Preset)
{
    SurfaceMaterial Mat {};

    switch (Preset)
    {
    case MaterialPresetCategory::RoughPorousConcrete:
        Mat.MaterialName             = "RoughPorousConcrete";
        Mat.Diffuse.BaseColor        = { 0.46f, 0.45f, 0.43f };
        Mat.Diffuse.BaseWeight       = 1.0f;
        Mat.Diffuse.DiffuseRoughness = 0.86f;
        Mat.Specular.Roughness       = 0.88f;
        Mat.Specular.SpecularWeight  = 0.35f;
        Mat.Specular.DielectricIOR   = 1.52f;
        Mat.PorosityLight.PorosityFraction = 0.42f;
        break;

    case MaterialPresetCategory::BrushedGoldAlloy:
        Mat.MaterialName             = "BrushedGoldAlloy";
        Mat.Diffuse.BaseColor        = { 1.00f, 0.766f, 0.336f };
        Mat.Specular.Metalness       = 1.0f;
        Mat.Specular.Roughness       = 0.22f;
        Mat.Specular.Anisotropy      = 0.68f;
        Mat.Specular.EdgeTintF82     = { 0.99f, 0.92f, 0.68f };
        break;

    case MaterialPresetCategory::ClearcoatedCarbonLacquer:
        Mat.MaterialName                      = "ClearcoatedCarbonLacquer";
        Mat.Diffuse.BaseColor                 = { 0.045f, 0.048f, 0.055f };
        Mat.Specular.Metalness                = 0.25f;
        Mat.Specular.Roughness                = 0.34f;
        Mat.Specular.Anisotropy               = 0.52f;
        Mat.Clearcoat.CoatWeight              = 1.0f;
        Mat.Clearcoat.CoatRoughness           = 0.04f;
        Mat.Clearcoat.CoatIOR                 = 1.56f;
        Mat.Clearcoat.CoatAbsorptionTint      = { 0.92f, 0.95f, 0.98f };
        Mat.Clearcoat.CoatThicknessMillimeter = 0.12f;
        break;

    case MaterialPresetCategory::HeatBluedTitaniumIridescent:
        Mat.MaterialName                        = "HeatBluedTitaniumIridescent";
        Mat.Diffuse.BaseColor                   = { 0.62f, 0.59f, 0.55f };
        Mat.Specular.Metalness                  = 1.0f;
        Mat.Specular.Roughness                  = 0.14f;
        Mat.Specular.EdgeTintF82                = { 0.88f, 0.90f, 0.94f };
        Mat.ThinFilm.ThinFilmWeight             = 1.0f;
        Mat.ThinFilm.ThinFilmThicknessNanometers= 490.0f;
        Mat.ThinFilm.ThinFilmIOR                = 2.35f;
        break;

    case MaterialPresetCategory::MetallicCarPaintGlinty:
        Mat.MaterialName                           = "MetallicCarPaintGlinty";
        Mat.Diffuse.BaseColor                      = { 0.035f, 0.14f, 0.42f };
        Mat.Diffuse.DiffuseRoughness               = 0.20f;
        Mat.Specular.Roughness                     = 0.28f;
        Mat.GlintFlakes.FlakeCoverage              = 0.62f;
        Mat.GlintFlakes.FlakeIntrinsicRoughness    = 0.038f;
        Mat.GlintFlakes.FlakeOrientationSpread     = 0.18f;
        Mat.GlintFlakes.FlakeLogDensityPerSqM      = 6.8f;
        Mat.GlintFlakes.FlakePrimaryTint           = { 0.72f, 0.86f, 1.00f };
        Mat.GlintFlakes.FlakeHorizonTint           = { 0.48f, 0.26f, 0.88f };
        Mat.GlintFlakes.FlakeBurialDepth           = 0.40f;
        Mat.Clearcoat.CoatWeight                   = 1.0f;
        Mat.Clearcoat.CoatRoughness                = 0.03f;
        Mat.Clearcoat.CoatIOR                      = 1.54f;
        Mat.Clearcoat.CoatAbsorptionTint           = { 0.86f, 0.92f, 0.99f };
        Mat.Clearcoat.CoatThicknessMillimeter      = 0.08f;
        break;

    case MaterialPresetCategory::AbbeCrownGlassDispersion:
        Mat.MaterialName                          = "AbbeCrownGlassDispersion";
        Mat.Diffuse.BaseColor                     = { 0.98f, 0.99f, 1.00f };
        Mat.Diffuse.BaseWeight                    = 0.0f;
        Mat.Specular.Roughness                    = 0.035f;
        Mat.Specular.DielectricIOR                = 1.52f;
        Mat.Transmission.TransmissionWeight       = 1.0f;
        Mat.Transmission.AbbeDispersionNumber     = 28.0f;
        Mat.Transmission.ThinWalledSlab           = false;
        Mat.Transmission.AttenuationColor         = { 0.88f, 0.97f, 0.95f };
        Mat.Transmission.AttenuationDistanceMeter = 0.18f;
        break;

    case MaterialPresetCategory::SubsurfaceJadeOrganic:
        Mat.MaterialName                          = "SubsurfaceJadeOrganic";
        Mat.Diffuse.BaseColor                     = { 0.18f, 0.58f, 0.38f };
        Mat.Specular.Roughness                    = 0.16f;
        Mat.Specular.DielectricIOR                = 1.61f;
        Mat.Subsurface.SubsurfaceWeight           = 0.88f;
        Mat.Subsurface.SubsurfaceColor            = { 0.24f, 0.72f, 0.48f };
        Mat.Subsurface.MeanFreePathMillimeters    = { 4.2f, 8.5f, 3.1f };
        Mat.Subsurface.PhaseAnisotropyG           = 0.25f;
        break;

    case MaterialPresetCategory::AlcantaraVelvetSheen:
        Mat.MaterialName             = "AlcantaraVelvetSheen";
        Mat.Diffuse.BaseColor        = { 0.26f, 0.07f, 0.12f };
        Mat.Diffuse.DiffuseRoughness = 0.72f;
        Mat.Specular.SpecularWeight  = 0.12f;
        Mat.Specular.Roughness       = 0.75f;
        Mat.Sheen.SheenWeight        = 0.92f;
        Mat.Sheen.SheenRoughness     = 0.36f;
        Mat.Sheen.SheenColor         = { 0.86f, 0.44f, 0.56f };
        break;

    case MaterialPresetCategory::KeratinStrandFiber:
        Mat.MaterialName                     = "KeratinStrandFiber";
        Mat.Diffuse.BaseColor                = { 0.96f, 0.86f, 0.72f };
        Mat.Specular.DielectricIOR           = 1.55f;
        Mat.Fiber.FiberWeight                = 1.0f;
        Mat.Fiber.LongitudinalRoughness      = 0.20f;
        Mat.Fiber.AzimuthalRoughness         = 0.28f;
        Mat.Fiber.CuticleTiltRadians         = 0.048f;
        Mat.Fiber.MelaninConcentration       = 0.16f;
        Mat.Fiber.PheomelaninRednessFraction = 0.58f;
        break;

    case MaterialPresetCategory::WetPorousGlowingBrakeRotor:
        Mat.MaterialName                               = "WetPorousGlowingBrakeRotor";
        Mat.Diffuse.BaseColor                          = { 0.22f, 0.20f, 0.19f };
        Mat.Diffuse.DiffuseRoughness                   = 0.65f;
        Mat.Specular.Metalness                         = 0.45f;
        Mat.Specular.Roughness                         = 0.38f;
        Mat.PorosityLight.PorosityFraction             = 0.55f;
        Mat.PorosityLight.SurfaceWetnessFraction       = 0.75f;
        Mat.PorosityLight.BlackbodyTemperatureKelvin   = 1350.0f;
        Mat.PorosityLight.EmissionLuminanceNits        = 950.0f;
        break;
    }

    SanitizeSurfaceMaterial(Mat);
    return Mat;
}

} // namespace Frontier::MaterialSpace
