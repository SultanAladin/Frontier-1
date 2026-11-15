/*============================================================================================================================================
                                                           DRAWERDETENTGESTURE.H
============================================================================================================================================*/
// 🧩 Unified 3-detent (Closed / Half / Full) touch-like drag settle physics & elastic clamping shared by pull-down/pull-up drawers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DrawerDetentStop : uint8_t
{
    Closed = 0u, // [-] - Fully retracted (0%)
    Half   = 1u, // [-] - Half-open split detent (~48%–50%)
    Full   = 2u, // [-] - Fully expanded detent (94%–100%)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Direct 1:1 follow within [Minimum, Maximum] with subtle rubber-band resistance beyond the stops.
[[nodiscard]] inline double EvaluateElasticClamp(
    double Position,
    double Minimum,
    double Maximum,
    double Elasticity = 0.05) noexcept
{
    if (Position < Minimum)
    {
        return Minimum - (Minimum - Position) * Elasticity;
    }
    if (Position > Maximum)
    {
        return Maximum + (Position - Maximum) * Elasticity;
    }
    return Position;
}

// 📝 Unified 3-detent release resolver combining normalized opening progress [0..1] with flick velocity (+ = opening, - = closing).
[[nodiscard]] inline DrawerDetentStop ResolveDrawerDetentOnRelease(
    float            NormalizedProgress,
    double           OpeningVelocityPxPerSec,
    float            DensityScale,
    DrawerDetentStop StartDetent) noexcept
{
    const float  Progress       = std::clamp(NormalizedProgress, 0.0f, 1.0f);
    const double SafeScale      = std::max(0.5, static_cast<double>(DensityScale));
    const double FastFlickRate  = 380.0 * SafeScale;
    const double HardCloseRate  = 780.0 * SafeScale;

    if (OpeningVelocityPxPerSec > FastFlickRate)
    {
        return DrawerDetentStop::Full;
    }
    if (OpeningVelocityPxPerSec < -FastFlickRate)
    {
        if (StartDetent == DrawerDetentStop::Full &&
            Progress > 0.58f &&
            OpeningVelocityPxPerSec > -HardCloseRate)
        {
            return DrawerDetentStop::Half;
        }
        return DrawerDetentStop::Closed;
    }

    if (Progress < 0.18f)
    {
        return DrawerDetentStop::Closed;
    }
    if (Progress < 0.58f)
    {
        return DrawerDetentStop::Half;
    }
    return DrawerDetentStop::Full;
}

} // namespace Frontier
