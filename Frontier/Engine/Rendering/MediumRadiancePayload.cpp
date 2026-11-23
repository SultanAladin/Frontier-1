/*==========================================================================================================================================
                                                MEDIUMRADIANCEPAYLOAD.CPP
==========================================================================================================================================*/
// 🧩 Encode and validate bounded cloud and height-fog shader inputs.

#include "MediumRadiancePayload.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTERNAL CONVERSION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

float ConvertRadianceFloat(double Quantity)
{
    if (!std::isfinite(Quantity) || std::abs(Quantity) > std::numeric_limits<float>::max())
        throw std::runtime_error("Medium parameter is not representable as float32");
    return static_cast<float>(Quantity);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   CLOUD INPUTS
//------------------------------------------------------------------------------------------------------------------------

std::array<float, CloudRadianceFloatCount> CompileCloudRadiancePayload(
    const CloudRadianceSpecification& Specification)
{
    const CloudParameters& Medium = Specification.Medium;
    std::array<float, CloudRadianceFloatCount> Payload{
        ConvertRadianceFloat(Medium.BaseHeightM), ConvertRadianceFloat(Medium.TopHeightM),
        ConvertRadianceFloat(Medium.Coverage), ConvertRadianceFloat(Medium.DensityScale),
        ConvertRadianceFloat(Medium.ExtinctionPerM), ConvertRadianceFloat(Medium.PhaseG),
        ConvertRadianceFloat(Medium.WindTimeS), ConvertRadianceFloat(Medium.UniformDensityOverride),
        ConvertRadianceFloat(Medium.WindXMps), ConvertRadianceFloat(Medium.WindYMps),
        ConvertRadianceFloat(Specification.MaximumDistanceM), float(Specification.ViewSamples),
        ConvertRadianceFloat(Specification.SolarDirection.X), ConvertRadianceFloat(Specification.SolarDirection.Y),
        ConvertRadianceFloat(Specification.SolarDirection.Z), float(Specification.SolarShadowSamples),
        ConvertRadianceFloat(Specification.SolarRadiance.R), ConvertRadianceFloat(Specification.SolarRadiance.G),
        ConvertRadianceFloat(Specification.SolarRadiance.B), ConvertRadianceFloat(Specification.ObserverHeightM),
        0, 0, 0, 0, ConvertRadianceFloat(Medium.AmbientRadiance.R), ConvertRadianceFloat(Medium.AmbientRadiance.G),
        ConvertRadianceFloat(Medium.AmbientRadiance.B), 0};
    static_assert(sizeof(float) == sizeof(uint32_t));
    const uint32_t LowBits = uint32_t(Medium.Seed);
    const uint32_t HighBits = uint32_t(Medium.Seed >> 32);
    std::memcpy(&Payload[20], &LowBits, sizeof(LowBits));
    std::memcpy(&Payload[21], &HighBits, sizeof(HighBits));
    ValidateCloudRadiancePayload(Payload.data(), Payload.size());
    return Payload;
}

void ValidateCloudRadiancePayload(const float* Payload, std::size_t FloatCount)
{
    if (Payload == nullptr || FloatCount < CloudRadianceFloatCount)
        throw std::runtime_error("Cloud input span is truncated");
    for (std::size_t Index = 0; Index < CloudRadianceFloatCount; ++Index)
        if (Index != 20 && Index != 21 && !std::isfinite(Payload[Index]))
            throw std::runtime_error("Cloud input contains a nonfinite parameter");
    if (!(Payload[1] > Payload[0] && Payload[2] >= 0 && Payload[2] <= 1 &&
          Payload[3] >= 0 && Payload[3] <= 16 && Payload[4] >= 0 && Payload[4] <= .1 &&
          std::abs(Payload[5]) < .99 && std::abs(Payload[6]) <= 1000000 && Payload[7] >= -1 &&
          Payload[7] <= 16 && std::abs(Payload[8]) <= 100 && std::abs(Payload[9]) <= 100 &&
          Payload[10] >= 0 && Payload[10] <= 100000 && Payload[11] >= 0 && Payload[11] <= 128 &&
          std::floor(Payload[11]) == Payload[11] && Payload[15] >= 0 && Payload[15] <= 8 &&
          std::floor(Payload[15]) == Payload[15]))
        throw std::runtime_error("Cloud parameters exceed the bounded shader domain");
    const double DirectionLength = Payload[12] * Payload[12] + Payload[13] * Payload[13] + Payload[14] * Payload[14];
    if (std::abs(DirectionLength - 1) >= .001)
        throw std::runtime_error("Cloud solar direction is not unit length");
    for (std::size_t Index : {16u, 17u, 18u, 24u, 25u, 26u})
        if (Payload[Index] < 0)
            throw std::runtime_error("Cloud incident radiance is negative");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FOG INPUTS
//------------------------------------------------------------------------------------------------------------------------

std::array<float, FogRadianceFloatCount> CompileFogRadiancePayload(
    const FogRadianceSpecification& Specification)
{
    const FogParameters& Medium = Specification.Medium;
    const std::array<float, FogRadianceFloatCount> Payload{
        ConvertRadianceFloat(Medium.DensityAtBase), ConvertRadianceFloat(Medium.BaseHeightM),
        ConvertRadianceFloat(Medium.FalloffHeightM), ConvertRadianceFloat(Specification.ScatteringAlbedo),
        ConvertRadianceFloat(Specification.PhaseAsymmetry), ConvertRadianceFloat(Specification.MaximumViewDistanceM),
        float(Specification.ViewSamples), float(Specification.CloudShadowSamples),
        ConvertRadianceFloat(Specification.AmbientRadiance.R), ConvertRadianceFloat(Specification.AmbientRadiance.G),
        ConvertRadianceFloat(Specification.AmbientRadiance.B), Specification.GeometryShadowsEnabled ? 1.0f : 0.0f,
        Specification.CloudShadowsEnabled ? 1.0f : 0.0f, Specification.AnalyticLightsEnabled ? 1.0f : 0.0f,
        ConvertRadianceFloat(Specification.MaximumLightDistanceM), float(Specification.FacetSamples)};
    ValidateFogRadiancePayload(Payload.data(), Payload.size());
    return Payload;
}

void ValidateFogRadiancePayload(const float* Payload, std::size_t FloatCount)
{
    if (Payload == nullptr || FloatCount < FogRadianceFloatCount)
        throw std::runtime_error("Fog input span is truncated");
    for (std::size_t Index = 0; Index < FogRadianceFloatCount; ++Index)
        if (!std::isfinite(Payload[Index]))
            throw std::runtime_error("Fog input contains a nonfinite parameter");
    if (!(Payload[0] >= 0 && Payload[0] <= 1 && std::abs(Payload[1]) <= 10000 &&
          Payload[2] >= 1 && Payload[2] <= 10000 && Payload[3] >= 0 && Payload[3] <= 1 &&
          std::abs(Payload[4]) < .95 && Payload[5] > 0 && Payload[5] <= 6000 &&
          Payload[6] >= 1 && Payload[6] <= 128 && std::floor(Payload[6]) == Payload[6] &&
          Payload[7] >= 1 && Payload[7] <= 128 && std::floor(Payload[7]) == Payload[7] &&
          Payload[8] >= 0 && Payload[9] >= 0 && Payload[10] >= 0 &&
          (Payload[11] == 0 || Payload[11] == 1) && (Payload[12] == 0 || Payload[12] == 1) &&
          (Payload[13] == 0 || Payload[13] == 1) && Payload[14] > 0 && Payload[14] <= 100000 &&
          Payload[15] >= 0 && Payload[15] <= 64 && std::floor(Payload[15]) == Payload[15]))
        throw std::runtime_error("Fog parameters exceed the bounded shader domain");
}

} // namespace Frontier
