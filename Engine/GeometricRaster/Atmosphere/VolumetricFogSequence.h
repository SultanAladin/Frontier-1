/*============================================================================================================================================
                                                          VOLUMETRICFOGSEQUENCE.H
============================================================================================================================================*/
// 🧩 Atmospheric Height Fog, Spectral Aerial Perspective, and Wind-Driven Multi-Shape LocalFogVolume raymarching

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericWindSolver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct HeightFogConfiguration
{
    bool                   Enabled             = false;                  // [-]   - Master exponential height fog toggle
    float                  Density             = 0.020f;                 // [1/m] - Datum extinction at BaseAltitudeMeters [0..0.2]
    float                  FalloffHeightMeters = 400.0f;                 // [m]   - Exponential e-folding scale height [10..3000]
    float                  BaseAltitudeMeters  = 0.0f;                   // [m]   - Reference floor elevation for datum density
    float                  SunScatterIntensity = 0.60f;                  // [x]   - Forward Henyey-Greenstein solar in-scatter [0..2]
    Mathematics::LinearRGB HeightColour        { 0.84f, 0.90f, 0.96f };  // [lin] - Scattering albedo / tint
    bool                   FollowWind          = false;                  // [-]   - Wind-driven boundary-layer density undulation
    WindCouplingSource      WindSource          = WindCouplingSource::HeightFogWind;
};

struct AerialFogConfiguration
{
    bool              Enabled             = false; // [-] - Master distance-gated aerial perspective toggle
    float             DensityMultiplier   = 1.00f; // [x] - Spectral extinction scale [0..4]
    float             StartDistanceMeters = 50.0f; // [m] - Minimum distance before aerial perspective begins [0..2000]
    float             MieBlend            = 0.40f; // [-] - Warm forward Mie aerosol scatter blend [0..1]
    float             ScaleHeightMeters   = 1800.0f;
    bool              FollowWind          = false;
    WindCouplingSource WindSource          = WindCouplingSource::AerialFogWind;
};

struct LocalFogVolume
{
    bool                   Enabled            = false;
    bool                   FollowWind         = true;
    WindCouplingSource      WindSource         = WindCouplingSource::LocalFogWind;
    AtmosphericVolumeShape Shape              = AtmosphericVolumeShape::Box;
    uint32_t               PrismSides         = 6u;                          // [cnt] - Sides for Prism [3..16]
    Mathematics::Axis3D    VolumeCenterMeters { 0.0f, 0.0f, 1.5f };          // [m]   - World-space volume center
    Mathematics::Scalar3D  HalfExtentMeters   { 8.0f, 8.0f, 3.0f };          // [m]   - Volume half-extents
    float                  YawDegrees         = 0.0f;                        // [deg] - Horizontal rotation around +Z
    float                  EdgeSoftness       = 0.35f;                       // [0..1]- Boundary feathering ratio
    float                  Density            = 1.00f;                       // [x]   - Extinction multiplier [0..4]
    float                  Coverage           = 0.65f;                       // [0..1]- FBM noise coverage threshold
    float                  FeatureScaleMeters = 18.0f;                       // [m]   - 3D FBM eddy feature scale [1..600]
    float                  AnisotropyG        = 0.55f;                       // [-.9..9] - Henyey-Greenstein phase g
    Mathematics::LinearRGB FogTint            { 0.86f, 0.91f, 0.96f };       // [lin] - Local fog scattering tint
};

struct AtmosphericFogConfiguration
{
    HeightFogConfiguration      HeightFog   {};
    AerialFogConfiguration      AerialFog   {};
    std::vector<LocalFogVolume> LocalFogs   {};
    struct GodRayConfiguration
    {
        bool                   Enabled            = false;  // [-]      - Volumetric crepuscular sun shafts through clouds & fog
        float                  Intensity          = 0.85f;  // [x]      - Shaft radiance multiplier [0..4]
        float                  AnisotropyG        = 0.78f;  // [0..0.95]- Forward Mie phase concentration around sun
        float                  Decay              = 0.92f;  // [0..1]   - Distance attenuation control
        float                  Density            = 0.55f;  // [x]      - Aerosol & fog-coupled scattering density [0..3]
        float                  MaxDistanceMeters  = 220.0f; // [m]      - Maximum raymarch shaft distance [10..2000]
        uint32_t               SampleSteps        = 24u;    // [cnt]    - IGN-jittered raymarch samples [8..48]
        bool                   InteractWithClouds = true;   // [-]      - Cloud-deck & LocalCloud shadow modulation
        bool                   InteractWithFog    = true;   // [-]      - Couples shaft medium density to HeightFog & LocalFogs
        Mathematics::LinearRGB ShaftTint          { 1.00f, 0.94f, 0.84f }; // [lin] - Crepuscular shaft color tint
    }                           GodRays     {};
};

using GodRayConfiguration = AtmosphericFogConfiguration::GodRayConfiguration;

struct FogEvaluationResult
{
    Mathematics::LinearRGB InScatterHdr  { 0.0f, 0.0f, 0.0f }; // [cd/m2] - Accumulated volumetric fog radiance
    float                  Transmittance = 1.0f;               // [0..1]  - Remaining background visibility
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VOLUMETRIC FOG SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class VolumetricFogSequence
{
public:
    static constexpr uint32_t MaxGpuLocalFogVolumes = 8u;

    // Analytical exponential height-fog optical depth integral over [0, SegmentMeters]
    [[nodiscard]] static float EvaluateHeightFogOpticalDepth(
        float                         OriginZ,
        float                         DirZ,
        float                         SegmentMeters,
        const HeightFogConfiguration& Config) noexcept;

    // Visibility distance where transmittance drops to 2% (-ln(0.02)/sigma)
    [[nodiscard]] static float ComputeTwoPercentVisibilityRangeMeters(
        float                         AltitudeMeters,
        const HeightFogConfiguration& HeightFog) noexcept;

    // Evaluates local wind-advected fog extinction density at a 3D world point (HeightFog + LocalFogs)
    [[nodiscard]] static float EvaluateFogMediumDensityAtPoint(
        const Mathematics::Axis3D&         WorldPoint,
        const AtmosphericFogConfiguration& FogConfig,
        const AtmosphericWindParameters&   GlobalWind,
        float                              DeckWindSpeedMps,
        float                              DeckWindBearingDeg,
        float                              SimulationSeconds) noexcept;

    // Evaluates combined Height Fog + Aerial Perspective + Wind-driven LocalFogVolumes over [0, MaxDistanceMeters]
    [[nodiscard]] static FogEvaluationResult EvaluateVolumetricFog(
        const Mathematics::Axis3D&         RayOriginWorld,
        const Mathematics::Axis3D&         RayDirection,
        float                              MaxDistanceMeters,
        const Mathematics::Axis3D&         SunDirection,
        const Mathematics::LinearRGB&      SolarIrradiance,
        const Mathematics::LinearRGB&      SkyRadiance,
        const AtmosphericFogConfiguration& FogConfig,
        const AtmosphericWindParameters&   GlobalWind,
        float                              DeckWindSpeedMps,
        float                              DeckWindBearingDeg,
        float                              SimulationSeconds,
        float                              PixelJitter01 = -1.0f) noexcept;

    // Evaluates volumetric crepuscular God Rays interacting with Scene Occlusion, Clouds, and Fog
    template <typename SceneVisFn, typename CloudVisFn>
    [[nodiscard]] static Mathematics::LinearRGB EvaluateVolumetricGodRays(
        const Mathematics::Axis3D&         RayOriginWorld,
        const Mathematics::Axis3D&         RayDirection,
        float                              MaxDistanceMeters,
        const Mathematics::Axis3D&         SunDirection,
        const Mathematics::LinearRGB&      SolarIrradiance,
        const AtmosphericFogConfiguration& FogConfig,
        const AtmosphericWindParameters&   GlobalWind,
        float                              DeckWindSpeedMps,
        float                              DeckWindBearingDeg,
        float                              SimulationSeconds,
        SceneVisFn&&                       EvaluateSceneSolarVisibility,
        CloudVisFn&&                       EvaluateCloudSolarVisibility,
        float                              PixelJitter01 = -1.0f) noexcept;
};

template <typename SceneVisFn, typename CloudVisFn>
Mathematics::LinearRGB VolumetricFogSequence::EvaluateVolumetricGodRays(
    const Mathematics::Axis3D&         RayOriginWorld,
    const Mathematics::Axis3D&         RayDirection,
    float                              MaxDistanceMeters,
    const Mathematics::Axis3D&         SunDirection,
    const Mathematics::LinearRGB&      SolarIrradiance,
    const AtmosphericFogConfiguration& FogConfig,
    const AtmosphericWindParameters&   GlobalWind,
    float                              DeckWindSpeedMps,
    float                              DeckWindBearingDeg,
    float                              SimulationSeconds,
    SceneVisFn&&                       EvaluateSceneSolarVisibility,
    CloudVisFn&&                       EvaluateCloudSolarVisibility,
    float                              PixelJitter01) noexcept
{
    const auto& GR = FogConfig.GodRays;
    if (!GR.Enabled || GR.Intensity <= 1.0e-4f || SunDirection.Z <= 0.005f || MaxDistanceMeters <= 0.25f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float RangeM   = std::min(std::max(0.25f, MaxDistanceMeters), std::max(10.0f, GR.MaxDistanceMeters));
    const uint32_t Steps = std::clamp(GR.SampleSteps, 8u, 48u);
    const float Dt       = (RangeM - 0.15f) / static_cast<float>(Steps);
    const auto FracGr = [](float V) noexcept -> float { return V - std::floor(V); };
    const float Ign      = (PixelJitter01 >= 0.0f)
        ? std::clamp(PixelJitter01, 0.0f, 1.0f)
        : FracGr(52.9829189f * FracGr(RayDirection.X * 193.7f + RayDirection.Y * 371.4f + RayDirection.Z * 547.8f));
    const float Jitter   = 0.5f + 0.72f * (Ign - 0.5f);

    const float CosTheta = Mathematics::EvaluateDot(RayDirection, SunDirection);
    const float G        = std::clamp(GR.AnisotropyG, 0.0f, 0.94f);
    const float G2       = G * G;
    const float Den      = std::max(1.0e-3f, 1.0f + G2 - 2.0f * G * CosTheta);
    const float MiePhase = (1.0f - G2) / (4.0f * Constants::PiFloat * std::pow(Den, 1.5f));
    const float Phase    = 0.85f * MiePhase + 0.15f * (1.0f / (4.0f * Constants::PiFloat));
    const float DecayScale = std::max(15.0f, GR.MaxDistanceMeters * (1.35f - 0.85f * std::clamp(GR.Decay, 0.0f, 1.0f)));

    Mathematics::LinearRGB Accum { 0.0f, 0.0f, 0.0f };
    float CamTrans = 1.0f;

    for (uint32_t S = 0u; S < Steps; ++S)
    {
        const float T = 0.15f + (static_cast<float>(S) + Jitter) * Dt;
        const Mathematics::Axis3D P = RayOriginWorld + RayDirection * T;

        const float SceneVis = std::clamp(EvaluateSceneSolarVisibility(P, SunDirection), 0.0f, 1.0f);
        if (SceneVis <= 1.0e-3f)
        {
            continue;
        }
        const float RawCloudVis = GR.InteractWithClouds
            ? std::clamp(EvaluateCloudSolarVisibility(P, SunDirection), 0.0f, 1.0f)
            : 1.0f;
        const float CloudVis = GR.InteractWithClouds
            ? std::pow(std::clamp((RawCloudVis - 0.12f) / 0.88f, 0.0f, 1.0f), 2.35f)
            : 1.0f;

        float MediumSigma = 0.0048f * std::max(0.0f, GR.Density);
        if (GR.InteractWithFog)
        {
            const float FogRho = EvaluateFogMediumDensityAtPoint(
                P, FogConfig, GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg,
                SimulationSeconds);
            MediumSigma += FogRho * 0.68f * std::max(0.1f, GR.Density);
        }

        const float DecayW  = std::exp(-T / DecayScale);
        const float StepWt  = CamTrans * SceneVis * CloudVis * MediumSigma * Phase * GR.Intensity * DecayW * Dt * 0.72f;
        Accum.R += SolarIrradiance.R * GR.ShaftTint.R * StepWt;
        Accum.G += SolarIrradiance.G * GR.ShaftTint.G * StepWt;
        Accum.B += SolarIrradiance.B * GR.ShaftTint.B * StepWt;
        CamTrans *= std::exp(-MediumSigma * Dt * 0.35f);
        if (CamTrans < 0.02f)
        {
            break;
        }
    }

    return Accum;
}

} // namespace Frontier::GeometricRaster
