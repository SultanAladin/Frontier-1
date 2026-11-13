/*============================================================================================================================================
                                                           BOUNDSCONSTRAINT.H
============================================================================================================================================*/
// 🧩 Spatial 3D AABB volume clamp and 2D UDIM planar canvas center constraint

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"

#include <algorithm>

namespace Frontier::Components::Constraints
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mathematics::Axis3D ClampSpatialPositionToBounds(
    Mathematics::Axis3D               Candidate,
    const BoundsConstraintParameters& Bounds) noexcept
{
    if (!Bounds.EnforceSpatialBounds)
    {
        return Candidate;
    }
    return {
        std::clamp(Candidate.X, Bounds.SpatialMinimum.X, Bounds.SpatialMaximum.X),
        std::clamp(Candidate.Y, Bounds.SpatialMinimum.Y, Bounds.SpatialMaximum.Y),
        std::clamp(Candidate.Z, Bounds.SpatialMinimum.Z, Bounds.SpatialMaximum.Z),
    };
}

[[nodiscard]] inline Mathematics::UVAxes ClampPlanarCenterToBounds(
    Mathematics::UVAxes               Candidate,
    const BoundsConstraintParameters& Bounds) noexcept
{
    if (!Bounds.EnforcePlanarBounds)
    {
        return Candidate;
    }
    return {
        std::clamp(Candidate.U, Bounds.PlanarMinimum.U, Bounds.PlanarMaximum.U),
        std::clamp(Candidate.V, Bounds.PlanarMinimum.V, Bounds.PlanarMaximum.V),
    };
}

} // namespace Frontier::Components::Constraints
