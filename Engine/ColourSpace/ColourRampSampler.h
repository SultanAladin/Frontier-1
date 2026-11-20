/*============================================================================================================================================
                                                             COLOURRAMPSAMPLER.H
============================================================================================================================================*/
// 🧩 Arbitrary-stop colour ramp sampling (Linear/Smooth/Stepped, LinearSrgb/Oklab/ACEScg), 3D lattice noise & 64/256-entry GPU LUT packing

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourSourceTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpatialColourSampleInput
{
    float    NormalizedCoordinate = 0.0f; // [0-1] - Primary scalar coordinate
    float    PositionX            = 0.0f; // [m]   - Optional world X for SpatialNoiseFill
    float    PositionY            = 0.0f; // [m]   - Optional world Y for SpatialNoiseFill
    float    PositionZ            = 0.0f; // [m]   - Optional world Z for SpatialNoiseFill
    float    ElapsedSeconds       = 0.0f; // [s]   - Optional elapsed time for SpatialNoiseFill drift
    uint32_t DiscreteOrdinal      = 0u;   // [idx] - Optional slot index for DiscreteTintSet
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float SampleLatticeNoise3D(float X, float Y, float Z) noexcept;
[[nodiscard]] float HashDeterministicFloat(float Seed, uint32_t Salt) noexcept;

[[nodiscard]] float ApplyCoordinateWrap(float Coordinate, ColourWrapBehaviour Wrap) noexcept;
[[nodiscard]] float NormalizeScalarToRange(float RawReading, const ColourRangeTransform& Range) noexcept;

[[nodiscard]] LinearRgba BlendLinearRgba(
    LinearRgba               First,
    LinearRgba               Second,
    float                    Factor01,
    ColourInterpolationSpace Space = ColourInterpolationSpace::LinearSrgb) noexcept;

[[nodiscard]] std::vector<ColourStop> ExtractOrderedStops(const ColourSourceSpec& Source);

[[nodiscard]] LinearRgba SampleColourStopsAt(
    std::span<const ColourStop> Stops,
    float                       RawCoordinate,
    const ColourRangeTransform& Range) noexcept;

[[nodiscard]] LinearRgba SampleColourSource(
    const ColourSourceProfile&      Profile,
    const SpatialColourSampleInput& Input) noexcept;

[[nodiscard]] ColourRampLut64  PackColourRampLut64(const ColourSourceProfile& Profile) noexcept;
[[nodiscard]] ColourRampLut256 PackColourRampLut256(const ColourSourceProfile& Profile) noexcept;

[[nodiscard]] LinearRgba SamplePackedLut64(const ColourRampLut64& Lut, float Coordinate01, ColourWrapBehaviour Wrap = ColourWrapBehaviour::Clamp) noexcept;
[[nodiscard]] LinearRgba SamplePackedLut256(const ColourRampLut256& Lut, float Coordinate01, ColourWrapBehaviour Wrap = ColourWrapBehaviour::Clamp) noexcept;

} // namespace Frontier::ColourSpace
