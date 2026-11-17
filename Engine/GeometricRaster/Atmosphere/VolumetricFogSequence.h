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
    WindBindingSource      WindSource          = WindBindingSource::HeightFogWind;
};

struct AerialFogConfiguration
{
    bool              Enabled             = false; // [-] - Master distance-gated aerial perspective toggle
    float             DensityMultiplier   = 1.00f; // [x] - Spectral extinction scale [0..4]
    float             StartDistanceMeters = 50.0f; // [m] - Minimum distance before aerial perspective begins [0..2000]
    float             MieBlend            = 0.40f; // [-] - Warm forward Mie aerosol scatter blend [0..1]
    float             ScaleHeightMeters   = 1800.0f;
    bool              FollowWind          = false;
    WindBindingSource WindSource          = WindBindingSource::AerialFogWind;
};

struct LocalFogVolume
{
    bool                   Enabled            = false;
    bool                   FollowWind         = true;
    WindBindingSource      WindSource         = WindBindingSource::LocalFogWind;
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
};

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

    // Evaluates combined Height Fog + Aerial Perspective + Wind-driven LocalFogVolumes over [0, MaxDistanceMeters]
    [[nodiscard]] static FogEvaluationResult EvaluateVolumetricFog(
        const Mathematics::Axis3D&                               RayOriginWorld,
        const Mathematics::Axis3D&                               RayDirection,
        float                                                    MaxDistanceMeters,
        const Mathematics::Axis3D&                               SunDirection,
        const Mathematics::LinearRGB&                            SolarIrradiance,
        const Mathematics::LinearRGB&                            SkyRadiance,
        const AtmosphericFogConfiguration&                       FogConfig,
        const AtmosphericWindParameters&                         GlobalWind,
        float                                                    DeckWindSpeedMps,
        float                                                    DeckWindBearingDeg,
        float                                                    SimulationSeconds,
        const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForceFields = nullptr,
        const SimulationSpace::ForceFields::FieldLatticeCascade*   OptionalLattice     = nullptr,
        float                                                    PixelJitter01       = -1.0f) noexcept;
};

} // namespace Frontier::GeometricRaster
