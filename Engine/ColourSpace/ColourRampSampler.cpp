/*============================================================================================================================================
                                                          COLOURRAMPSAMPLER.CPP
============================================================================================================================================*/
// 🎨 Implementation of modular colour source sampling, 3D lattice noise (HTML bit-parity), perceptual Oklab blending & 64/256-entry LUTs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourRampSampler.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/SpectralDistribution.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ColourSpace
{

namespace
{

[[nodiscard]] float HashLatticeGrid3U(int32_t Ix, int32_t Iy, int32_t Iz) noexcept
{
    uint32_t H = static_cast<uint32_t>(Ix) * 374761393u
               + static_cast<uint32_t>(Iy) * 668265263u
               + static_cast<uint32_t>(Iz) * 2147483647u;
    H = (H ^ (H >> 13u)) * 1274126177u;
    return static_cast<float>(H ^ (H >> 16u)) / 4294967296.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENUM TOKEN CONVERSIONS
//------------------------------------------------------------------------------------------------------------------------

std::string_view QueryColourSourceCategoryToken(ColourSourceCategory Category) noexcept
{
    switch (Category)
    {
        case ColourSourceCategory::SolidPigment:      return "SolidPigment";
        case ColourSourceCategory::LinearFill:        return "LinearFill";
        case ColourSourceCategory::MultiStopGradient: return "MultiStopGradient";
        case ColourSourceCategory::QuantizedRamp:     return "QuantizedRamp";
        case ColourSourceCategory::BlackbodyThermal:  return "BlackbodyThermal";
        case ColourSourceCategory::DiscreteTintSet:   return "DiscreteTintSet";
        case ColourSourceCategory::SpatialNoiseFill:  return "SpatialNoiseFill";
        default:                                      return "LinearFill";
    }
}

ColourSourceCategory ParseColourSourceCategoryToken(std::string_view Token) noexcept
{
    if (Token == "SolidPigment")      return ColourSourceCategory::SolidPigment;
    if (Token == "LinearFill")        return ColourSourceCategory::LinearFill;
    if (Token == "MultiStopGradient") return ColourSourceCategory::MultiStopGradient;
    if (Token == "QuantizedRamp")     return ColourSourceCategory::QuantizedRamp;
    if (Token == "BlackbodyThermal")  return ColourSourceCategory::BlackbodyThermal;
    if (Token == "DiscreteTintSet")   return ColourSourceCategory::DiscreteTintSet;
    if (Token == "SpatialNoiseFill")  return ColourSourceCategory::SpatialNoiseFill;
    return ColourSourceCategory::LinearFill;
}

std::string_view QueryColourWrapToken(ColourWrapBehaviour Wrap) noexcept
{
    switch (Wrap)
    {
        case ColourWrapBehaviour::Repeat: return "repeat";
        case ColourWrapBehaviour::Mirror: return "mirror";
        default:                          return "clamp";
    }
}

ColourWrapBehaviour ParseColourWrapToken(std::string_view Token) noexcept
{
    if (Token == "repeat" || Token == "Repeat") return ColourWrapBehaviour::Repeat;
    if (Token == "mirror" || Token == "Mirror") return ColourWrapBehaviour::Mirror;
    return ColourWrapBehaviour::Clamp;
}

std::string_view QueryColourBlendCurveToken(ColourBlendCurve Curve) noexcept
{
    switch (Curve)
    {
        case ColourBlendCurve::Smooth:  return "smooth";
        case ColourBlendCurve::Stepped: return "stepped";
        default:                        return "linear";
    }
}

ColourBlendCurve ParseColourBlendCurveToken(std::string_view Token) noexcept
{
    if (Token == "smooth"  || Token == "Smooth")  return ColourBlendCurve::Smooth;
    if (Token == "stepped" || Token == "Stepped") return ColourBlendCurve::Stepped;
    return ColourBlendCurve::Linear;
}

std::string_view QueryInterpolationSpaceToken(ColourInterpolationSpace Space) noexcept
{
    switch (Space)
    {
        case ColourInterpolationSpace::Oklab:  return "Oklab";
        case ColourInterpolationSpace::AcesCg: return "ACEScg";
        default:                               return "LinearSrgb";
    }
}

ColourInterpolationSpace ParseInterpolationSpaceToken(std::string_view Token) noexcept
{
    if (Token == "Oklab" || Token == "oklab")   return ColourInterpolationSpace::Oklab;
    if (Token == "ACEScg" || Token == "AcesCg") return ColourInterpolationSpace::AcesCg;
    return ColourInterpolationSpace::LinearSrgb;
}

std::string_view QueryOpticalBlendToken(OpticalBlendEquation Equation) noexcept
{
    switch (Equation)
    {
        case OpticalBlendEquation::PremultipliedAlpha: return "alpha";
        case OpticalBlendEquation::Screen:             return "screen";
        case OpticalBlendEquation::Multiply:           return "multiply";
        default:                                       return "additive";
    }
}

OpticalBlendEquation ParseOpticalBlendToken(std::string_view Token) noexcept
{
    if (Token == "alpha" || Token == "PremultipliedAlpha") return OpticalBlendEquation::PremultipliedAlpha;
    if (Token == "screen" || Token == "Screen")            return OpticalBlendEquation::Screen;
    if (Token == "multiply" || Token == "Multiply")        return OpticalBlendEquation::Multiply;
    return OpticalBlendEquation::Additive;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           NOISE, HASH & COORDINATE WRAPPING
//------------------------------------------------------------------------------------------------------------------------

float SampleLatticeNoise3D(float X, float Y, float Z) noexcept
{
    const int32_t Ix = static_cast<int32_t>(std::floor(X));
    const int32_t Iy = static_cast<int32_t>(std::floor(Y));
    const int32_t Iz = static_cast<int32_t>(std::floor(Z));
    const float Fx = X - static_cast<float>(Ix);
    const float Fy = Y - static_cast<float>(Iy);
    const float Fz = Z - static_cast<float>(Iz);
    const float Ux = Fx * Fx * (3.0f - 2.0f * Fx);
    const float Uy = Fy * Fy * (3.0f - 2.0f * Fy);
    const float Uz = Fz * Fz * (3.0f - 2.0f * Fz);

    const float C000 = HashLatticeGrid3U(Ix,     Iy,     Iz);
    const float C100 = HashLatticeGrid3U(Ix + 1, Iy,     Iz);
    const float C010 = HashLatticeGrid3U(Ix,     Iy + 1, Iz);
    const float C110 = HashLatticeGrid3U(Ix + 1, Iy + 1, Iz);
    const float C001 = HashLatticeGrid3U(Ix,     Iy,     Iz + 1);
    const float C101 = HashLatticeGrid3U(Ix + 1, Iy,     Iz + 1);
    const float C011 = HashLatticeGrid3U(Ix,     Iy + 1, Iz + 1);
    const float C111 = HashLatticeGrid3U(Ix + 1, Iy + 1, Iz + 1);

    const float X00 = C000 + (C100 - C000) * Ux;
    const float X10 = C010 + (C110 - C010) * Ux;
    const float X01 = C001 + (C101 - C001) * Ux;
    const float X11 = C011 + (C111 - C011) * Ux;
    const float Y0  = X00 + (X10 - X00) * Uy;
    const float Y1  = X01 + (X11 - X01) * Uy;
    return Y0 + (Y1 - Y0) * Uz;
}

float HashDeterministicFloat(float Seed, uint32_t Salt) noexcept
{
    uint32_t X = static_cast<uint32_t>(std::floor(std::max(0.0f, Seed))) * 31u + Salt * 977u + 17u;
    X ^= X >> 16u;
    X *= 0x7feb352du;
    X ^= X >> 15u;
    X *= 0x846ca68bu;
    X ^= X >> 16u;
    return static_cast<float>(X & 0x00ffffffu) / 16777216.0f;
}

float ApplyCoordinateWrap(float Coordinate, ColourWrapBehaviour Wrap) noexcept
{
    if (Wrap == ColourWrapBehaviour::Repeat)
    {
        return Coordinate - std::floor(Coordinate);
    }
    if (Wrap == ColourWrapBehaviour::Mirror)
    {
        const float M2 = Coordinate - 2.0f * std::floor(Coordinate * 0.5f);
        return (M2 <= 1.0f) ? M2 : (2.0f - M2);
    }
    return std::clamp(Coordinate, 0.0f, 1.0f);
}

float NormalizeScalarToRange(float RawReading, const ColourRangeTransform& Range) noexcept
{
    const float InvSpan  = 1.0f / std::max(1.0e-4f, std::fabs(Range.RangeMax - Range.RangeMin));
    const float SignSpan = (Range.RangeMax >= Range.RangeMin) ? 1.0f : -1.0f;
    const float Scaled   = (RawReading - Range.RangeMin) * InvSpan * SignSpan;
    return (Range.WrapBehaviour == ColourWrapBehaviour::Clamp) ? std::clamp(Scaled, 0.0f, 1.0f) : Scaled;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           STOP BLENDING & SOURCE SAMPLING
//------------------------------------------------------------------------------------------------------------------------

LinearRgba BlendLinearRgba(LinearRgba First, LinearRgba Second, float Factor01, ColourInterpolationSpace Space) noexcept
{
    const float T     = std::clamp(Factor01, 0.0f, 1.0f);
    const float Alpha = First.Alpha + (Second.Alpha - First.Alpha) * T;

    if (Space == ColourInterpolationSpace::Oklab)
    {
        const OklabTriplet LabA = ConvertLinearSrgbToOklab(First);
        const OklabTriplet LabB = ConvertLinearSrgbToOklab(Second);
        const OklabTriplet Mix  = {
            LabA.Lightness + (LabB.Lightness - LabA.Lightness) * T,
            LabA.AxisA     + (LabB.AxisA     - LabA.AxisA)     * T,
            LabA.AxisB     + (LabB.AxisB     - LabA.AxisB)     * T,
        };
        return ConvertOklabToLinearSrgb(Mix, Alpha);
    }

    if (Space == ColourInterpolationSpace::AcesCg)
    {
        const LinearRgba AcesA = ConvertPrimaries(First,  ChromaticityPrimaries::LinearSrgbRec709, ChromaticityPrimaries::AcesCg);
        const LinearRgba AcesB = ConvertPrimaries(Second, ChromaticityPrimaries::LinearSrgbRec709, ChromaticityPrimaries::AcesCg);
        const LinearRgba Mix   = {
            AcesA.Red   + (AcesB.Red   - AcesA.Red)   * T,
            AcesA.Green + (AcesB.Green - AcesA.Green) * T,
            AcesA.Blue  + (AcesB.Blue  - AcesA.Blue)  * T,
            Alpha,
        };
        return ConvertPrimaries(Mix, ChromaticityPrimaries::AcesCg, ChromaticityPrimaries::LinearSrgbRec709);
    }

    return {
        First.Red   + (Second.Red   - First.Red)   * T,
        First.Green + (Second.Green - First.Green) * T,
        First.Blue  + (Second.Blue  - First.Blue)  * T,
        Alpha,
    };
}

std::vector<ColourStop> ExtractOrderedStops(const ColourSourceSpec& Source)
{
    std::vector<ColourStop> Stops {};
    if (const auto* Solid = std::get_if<SolidPigmentSource>(&Source))
    {
        Stops.push_back({ 0.0f, Solid->LinearTint });
        Stops.push_back({ 1.0f, Solid->LinearTint });
    }
    else if (const auto* Fill = std::get_if<LinearFillSource>(&Source))
    {
        Stops.push_back({ 0.0f, Fill->PrimaryLinear });
        if (Fill->MidpointEnabled)
        {
            Stops.push_back({ 0.5f, Fill->MidpointLinear });
        }
        Stops.push_back({ 1.0f, Fill->SecondaryLinear });
    }
    else if (const auto* Gradient = std::get_if<MultiStopGradientSource>(&Source))
    {
        Stops = Gradient->Stops;
        if (Stops.empty() && !Gradient->LinkedPresetIdentifier.empty())
        {
            if (const auto* BuiltIn = FindBuiltInColourRampPreset(Gradient->LinkedPresetIdentifier))
            {
                Stops = BuiltIn->Stops;
            }
        }
    }
    else if (const auto* Quant = std::get_if<QuantizedRampSource>(&Source))
    {
        Stops = Quant->BandStops;
    }
    else if (const auto* Thermal = std::get_if<BlackbodyThermalSource>(&Source))
    {
        constexpr uint32_t SampleCount = 8u;
        Stops.reserve(SampleCount);
        for (uint32_t I = 0u; I < SampleCount; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(SampleCount - 1u);
            const float K = Thermal->HotKelvin + (Thermal->ColdKelvin - Thermal->HotKelvin) * T;
            const float A = 1.0f + (Thermal->TailOpacity - 1.0f) * T;
            LinearRgba C  = ConvertKelvinToLinearSrgb(K, A, TransferCurveCategory::PureGamma22);
            C.Red   *= Thermal->IntensityGain;
            C.Green *= Thermal->IntensityGain;
            C.Blue  *= Thermal->IntensityGain;
            Stops.push_back({ T, C });
        }
    }
    else if (const auto* Discrete = std::get_if<DiscreteTintSetSource>(&Source))
    {
        const uint32_t Count = std::clamp(Discrete->ActiveCount, 1u, static_cast<uint32_t>(Discrete->Entries.size()));
        Stops.reserve(Count);
        for (uint32_t I = 0u; I < Count; ++I)
        {
            const float T = (Count > 1u) ? static_cast<float>(I) / static_cast<float>(Count - 1u) : 0.0f;
            Stops.push_back({ T, Discrete->Entries[I] });
        }
    }
    else if (const auto* Noise = std::get_if<SpatialNoiseFillSource>(&Source))
    {
        Stops = Noise->Stops;
    }

    if (Stops.empty())
    {
        Stops.push_back({ 0.0f, { 1.0f, 1.0f, 1.0f, 1.0f } });
        Stops.push_back({ 1.0f, { 1.0f, 1.0f, 1.0f, 0.0f } });
    }
    std::stable_sort(Stops.begin(), Stops.end(), [](const ColourStop& A, const ColourStop& B) noexcept {
        return A.Position01 < B.Position01;
    });
    return Stops;
}

LinearRgba SampleColourStopsAt(
    std::span<const ColourStop> Stops,
    float                       RawCoordinate,
    const ColourRangeTransform& Range) noexcept
{
    if (Stops.empty()) return { 1.0f, 1.0f, 1.0f, 1.0f };
    if (Stops.size() == 1u) return Stops.front().LinearTint;

    const float Repeat = std::max(0.05f, Range.RepeatCount);
    const float U      = RawCoordinate * Repeat + Range.PhaseOffset;
    const float T      = ApplyCoordinateWrap(U, Range.WrapBehaviour);

    if (T <= Stops.front().Position01) return Stops.front().LinearTint;
    if (T >= Stops.back().Position01)  return Stops.back().LinearTint;

    std::size_t Lo = 0u;
    std::size_t Hi = Stops.size() - 1u;
    while (Lo + 1u < Hi)
    {
        const std::size_t Mid = (Lo + Hi) >> 1u;
        if (Stops[Mid].Position01 <= T) Lo = Mid;
        else                            Hi = Mid;
    }

    const ColourStop& A = Stops[Lo];
    const ColourStop& B = Stops[Lo + 1u];
    float Frac = std::clamp((T - A.Position01) / std::max(1.0e-5f, B.Position01 - A.Position01), 0.0f, 1.0f);
    if (Range.BlendCurve == ColourBlendCurve::Smooth)
    {
        Frac = Frac * Frac * (3.0f - 2.0f * Frac);
    }
    else if (Range.BlendCurve == ColourBlendCurve::Stepped)
    {
        Frac = 0.0f;
    }

    return BlendLinearRgba(A.LinearTint, B.LinearTint, Frac, Range.InterpolationSpace);
}

LinearRgba SampleColourSource(const ColourSourceProfile& Profile, const SpatialColourSampleInput& Input) noexcept
{
    if (const auto* Solid = std::get_if<SolidPigmentSource>(&Profile.Source))
    {
        return Solid->LinearTint;
    }

    if (const auto* Discrete = std::get_if<DiscreteTintSetSource>(&Profile.Source))
    {
        const uint32_t Active = std::clamp(Discrete->ActiveCount, 1u, static_cast<uint32_t>(Discrete->Entries.size()));
        return Discrete->Entries[Input.DiscreteOrdinal % Active];
    }

    const std::vector<ColourStop> Stops = ExtractOrderedStops(Profile.Source);
    ColourRangeTransform EffectiveRange = Profile.Range;
    if (std::holds_alternative<QuantizedRampSource>(Profile.Source))
    {
        EffectiveRange.BlendCurve = ColourBlendCurve::Stepped;
    }

    float Coordinate = Input.NormalizedCoordinate;
    if (const auto* NoiseFill = std::get_if<SpatialNoiseFillSource>(&Profile.Source))
    {
        const float Freq = std::max(0.25f, NoiseFill->NoiseFrequency);
        Coordinate = SampleLatticeNoise3D(
            Input.PositionX * Freq,
            Input.PositionY * Freq,
            Input.PositionZ * Freq + Input.ElapsedSeconds * NoiseFill->EvolutionRate);
    }

    return SampleColourStopsAt(Stops, Coordinate, EffectiveRange);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               GPU LUT PACKING & SAMPLING
//------------------------------------------------------------------------------------------------------------------------

ColourRampLut64 PackColourRampLut64(const ColourSourceProfile& Profile) noexcept
{
    ColourRampLut64 Lut {};
    const std::vector<ColourStop> Stops = ExtractOrderedStops(Profile.Source);
    ColourRangeTransform LutRange       = Profile.Range;
    if (std::holds_alternative<QuantizedRampSource>(Profile.Source))
    {
        LutRange.BlendCurve = ColourBlendCurve::Stepped;
    }

    for (uint32_t Index = 0u; Index < ColourRampLut64Count; ++Index)
    {
        const float T       = static_cast<float>(Index) / static_cast<float>(ColourRampLut64Count - 1u);
        Lut.Samples[Index]  = SampleColourStopsAt(Stops, T, LutRange);
    }
    return Lut;
}

ColourRampLut256 PackColourRampLut256(const ColourSourceProfile& Profile) noexcept
{
    ColourRampLut256 Lut {};
    const std::vector<ColourStop> Stops = ExtractOrderedStops(Profile.Source);
    ColourRangeTransform LutRange       = Profile.Range;
    if (std::holds_alternative<QuantizedRampSource>(Profile.Source))
    {
        LutRange.BlendCurve = ColourBlendCurve::Stepped;
    }

    for (uint32_t Index = 0u; Index < ColourRampLut256Count; ++Index)
    {
        const float T      = static_cast<float>(Index) / static_cast<float>(ColourRampLut256Count - 1u);
        Lut.Samples[Index] = SampleColourStopsAt(Stops, T, LutRange);
    }
    return Lut;
}

LinearRgba SamplePackedLut64(const ColourRampLut64& Lut, float Coordinate01, ColourWrapBehaviour Wrap) noexcept
{
    const float T        = ApplyCoordinateWrap(Coordinate01, Wrap);
    const float Scaled   = T * static_cast<float>(ColourRampLut64Count - 1u);
    const uint32_t Low   = std::min(static_cast<uint32_t>(std::floor(Scaled)), ColourRampLut64Count - 1u);
    const uint32_t High  = std::min(Low + 1u, ColourRampLut64Count - 1u);
    const float Fraction = Scaled - static_cast<float>(Low);
    return BlendLinearRgba(Lut.Samples[Low], Lut.Samples[High], Fraction, ColourInterpolationSpace::LinearSrgb);
}

LinearRgba SamplePackedLut256(const ColourRampLut256& Lut, float Coordinate01, ColourWrapBehaviour Wrap) noexcept
{
    const float T        = ApplyCoordinateWrap(Coordinate01, Wrap);
    const float Scaled   = T * static_cast<float>(ColourRampLut256Count - 1u);
    const uint32_t Low   = std::min(static_cast<uint32_t>(std::floor(Scaled)), ColourRampLut256Count - 1u);
    const uint32_t High  = std::min(Low + 1u, ColourRampLut256Count - 1u);
    const float Fraction = Scaled - static_cast<float>(Low);
    return BlendLinearRgba(Lut.Samples[Low], Lut.Samples[High], Fraction, ColourInterpolationSpace::LinearSrgb);
}

} // namespace Frontier::ColourSpace
