/*============================================================================================================================================
                                                    CLEANEMISSIVEGICANDIDATES.H
============================================================================================================================================*/
// 🧩 Shared image-space error and dark-region leakage measurements for the GI candidate shootout

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/LinearColors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace Exhibits::CleanEmissiveGi
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ImageErrorSummary
{
    std::size_t ComparedPixelCount       = 0u;  // [count] - Pixels present in both images
    double      MeanAbsoluteError        = 0.0; // [-]     - Channel-mean absolute linear-HDR error
    double      MeanSquaredError         = 0.0; // [-]     - Channel-mean squared linear-HDR error
    double      PositiveBiasRatio        = 0.0; // [-]     - Positive excess relative to reference radiance energy
    double      DarkReferenceLeakageRatio = 0.0; // [-]     - Candidate excess over dark reference pixels / candidate dark-region energy
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ImageErrorSummary CompareLinearRadiance(
    std::span<const Frontier::Mathematics::LinearRGB> Candidate,
    std::span<const Frontier::Mathematics::LinearRGB> Reference,
    float                                             PositiveTolerance = 0.025f,
    float                                             DarkReferenceThreshold = 0.03f) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline ImageErrorSummary CompareLinearRadiance(
    std::span<const Frontier::Mathematics::LinearRGB> Candidate,
    std::span<const Frontier::Mathematics::LinearRGB> Reference,
    float                                             PositiveTolerance,
    float                                             DarkReferenceThreshold) noexcept
{
    ImageErrorSummary Summary {};
    Summary.ComparedPixelCount = std::min(Candidate.size(), Reference.size());
    if (Summary.ComparedPixelCount == 0u)
    {
        return Summary;
    }

    double AbsoluteErrorSum = 0.0;
    double SquaredErrorSum = 0.0;
    double PositiveExcessSum = 0.0;
    double ReferenceEnergy = 0.0;
    double DarkCandidateEnergy = 0.0;
    double DarkPositiveExcess = 0.0;

    for (std::size_t PixelOrdinal = 0u; PixelOrdinal < Summary.ComparedPixelCount; ++PixelOrdinal)
    {
        const auto& CandidatePixel = Candidate[PixelOrdinal];
        const auto& ReferencePixel = Reference[PixelOrdinal];
        const std::array<float, 3u> CandidateChannels {{ CandidatePixel.R, CandidatePixel.G, CandidatePixel.B }};
        const std::array<float, 3u> ReferenceChannels {{ ReferencePixel.R, ReferencePixel.G, ReferencePixel.B }};
        const double CandidateLuminance =
            0.2126 * CandidatePixel.R + 0.7152 * CandidatePixel.G + 0.0722 * CandidatePixel.B;
        const double ReferenceLuminance =
            0.2126 * ReferencePixel.R + 0.7152 * ReferencePixel.G + 0.0722 * ReferencePixel.B;
        const bool DarkReferencePixel = ReferenceLuminance <= static_cast<double>(DarkReferenceThreshold);
        if (DarkReferencePixel)
        {
            DarkCandidateEnergy += std::max(0.0, CandidateLuminance);
            DarkPositiveExcess += std::max(
                0.0,
                CandidateLuminance - ReferenceLuminance - static_cast<double>(PositiveTolerance));
        }

        for (std::size_t ChannelOrdinal = 0u; ChannelOrdinal < CandidateChannels.size(); ++ChannelOrdinal)
        {
            const double Difference = static_cast<double>(CandidateChannels[ChannelOrdinal]) -
                                      static_cast<double>(ReferenceChannels[ChannelOrdinal]);
            AbsoluteErrorSum += std::fabs(Difference);
            SquaredErrorSum += Difference * Difference;
            ReferenceEnergy += std::fabs(static_cast<double>(ReferenceChannels[ChannelOrdinal]));
            PositiveExcessSum += std::max(0.0, Difference - static_cast<double>(PositiveTolerance));
        }
    }

    const double ChannelCount = static_cast<double>(Summary.ComparedPixelCount * 3u);
    Summary.MeanAbsoluteError = AbsoluteErrorSum / ChannelCount;
    Summary.MeanSquaredError = SquaredErrorSum / ChannelCount;
    Summary.PositiveBiasRatio = PositiveExcessSum / std::max(1.0e-9, ReferenceEnergy);
    Summary.DarkReferenceLeakageRatio = DarkPositiveExcess / std::max(1.0e-9, DarkCandidateEnergy);
    return Summary;
}

} // namespace Exhibits::CleanEmissiveGi
