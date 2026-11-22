/*====================================================================================================================================
                                                    PRECISIONSPECIFICATION.H
====================================================================================================================================*/
// 🧩 Session precision and tolerant-modelling rules (exact by default, local precision per edge and vertex)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Tolerant modelling (Jackson 1995, Parasolid): geometry is exact to the session precision unless an edge or vertex
//    carries a local precision. A tolerant edge is a tube of radius εEdge around its curve, a tolerant vertex a sphere of
//    radius εVertex. Two entities touch when their distance is within the sum of their precisions.
inline constexpr double SessionPrecision    = 1.0e-8;                                        // [len] - Exact-model linear resolution
inline constexpr double AngularPrecision    = 1.0e-11;                                       // [rad] - Exact-model angular resolution
inline constexpr double ExactPrecision      = std::numeric_limits<double>::quiet_NaN();      // [len] - Marker: no local precision
inline constexpr double ModelSizeBoundary   = 1.0e3;                                         // [len] - Half-extent of the modelling box

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the effective linear precision of an entity: its local precision if it carries one, else the session precision.
[[nodiscard]] inline double ResolvePrecision(double LocalPrecision) noexcept
{
    return std::isnan(LocalPrecision) ? SessionPrecision : std::max(LocalPrecision, SessionPrecision);
}

// 📝 Exact entities are recorded with NaN so that "tolerant" is a property, not a magic magnitude.
[[nodiscard]] inline bool TolerantCondition(double LocalPrecision) noexcept
{
    return !std::isnan(LocalPrecision);
}

} // namespace Frontier::GeometrySpace::Parametric
