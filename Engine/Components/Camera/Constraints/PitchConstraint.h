/*============================================================================================================================================
                                                            PITCHCONSTRAINT.H
============================================================================================================================================*/
// 🧩 Symmetric pole-guard elevation constraint preventing right-handed camera basis gimbal flip at +/- 90 degrees

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>

namespace Frontier::Components::Constraints
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr float ClampElevationPitchRadians(
    float CandidatePitchRadians,
    float MaxAbsPitchRadians = 1.5184364f) noexcept // +/- 87.0 deg default
{
    const float SafeLimit = std::clamp(MaxAbsPitchRadians, 0.10f, 1.553343f);
    return std::clamp(CandidatePitchRadians, -SafeLimit, SafeLimit);
}

} // namespace Frontier::Components::Constraints
