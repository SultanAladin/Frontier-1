/*==========================================================================================================================================
                                                 MEDIUMRADIANCEPAYLOAD.H
==========================================================================================================================================*/
// 🧩 Typed cloud and fog inputs for the shared LightInfluence compute shader.

#pragma once

#include "CloudField.h"
#include "FogMedium.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLOUD SHADER INPUTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 SolarDirection must already be unit length. The observer height supplies the cube's reference altitude;
//    it is not a spatially varying atmosphere. Seed bits are stored losslessly in two float32 slots.
struct CloudRadianceSpecification
{
    CloudParameters Medium{};
    SpatialVector   SolarDirection{0, 0, 1};
    SpectrumRgb     SolarRadiance{};
    double          ObserverHeightM = 0.0;
    double          MaximumDistanceM = 40000.0;
    uint32_t        ViewSamples = 64;
    uint32_t        SolarShadowSamples = 6;
};

inline constexpr std::size_t CloudRadianceFloatCount = 28;
[[nodiscard]] std::array<float, CloudRadianceFloatCount> CompileCloudRadiancePayload(
    const CloudRadianceSpecification& Specification);

// 📝 The two seed slots may have arbitrary float bit patterns, including NaNs; all other slots must be finite.
void ValidateCloudRadiancePayload(const float* Payload, std::size_t FloatCount);

//------------------------------------------------------------------------------------------------------------------------
//                                                   FOG SHADER INPUTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The shader uses the medium density/height, not FogParameters::Colour. AmbientRadiance is its separate
//    isotropic incident term. Boolean switches select geometry shadows, cloud shadows and analytic lights.
struct FogRadianceSpecification
{
    FogParameters Medium{};
    double        ScatteringAlbedo = 0.9;
    double        PhaseAsymmetry = 0.65;
    double        MaximumViewDistanceM = 2500.0;
    uint32_t      ViewSamples = 32;
    uint32_t      CloudShadowSamples = 32;
    SpectrumRgb   AmbientRadiance{0.01, 0.012, 0.015};
    bool          GeometryShadowsEnabled = true;
    bool          CloudShadowsEnabled = true;
    bool          AnalyticLightsEnabled = true;
    double        MaximumLightDistanceM = 40000.0;
    uint32_t      FacetSamples = 0;
};

inline constexpr std::size_t FogRadianceFloatCount = 16;
[[nodiscard]] std::array<float, FogRadianceFloatCount> CompileFogRadiancePayload(
    const FogRadianceSpecification& Specification);
void ValidateFogRadiancePayload(const float* Payload, std::size_t FloatCount);

} // namespace Frontier
