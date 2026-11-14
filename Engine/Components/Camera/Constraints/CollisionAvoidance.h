/*============================================================================================================================================
                                                          COLLISIONAVOIDANCE.H
============================================================================================================================================*/
// 🧩 Analytic swept-sphere ray pullback from Pivot toward DesiredEye preventing camera occlusion inside scene obstacles

#pragma once

#include "Engine/Components/Camera/CameraConfiguration.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components::Constraints
{

[[nodiscard]] inline float ResolveUnoccludedOrbitDistance(
    Mathematics::Axis3D                 PivotPosition,
    Mathematics::Axis3D                 DesiredEyePosition,
    float                               MinAllowedDistance,
    const CollisionAvoidanceParameters& Avoidance) noexcept
{
    const Mathematics::Axis3D Segment = DesiredEyePosition - PivotPosition;
    const float DesiredDist           = Mathematics::EvaluateLength(Segment);
    if (!Avoidance.Enabled || Avoidance.Obstacles.empty() || DesiredDist <= 1.0e-4f)
    {
        return DesiredDist;
    }

    const Mathematics::Axis3D RayDir = Segment * (1.0f / DesiredDist);
    float SafeDist = DesiredDist;

    for (const CollisionObstacleSphere& Obstacle : Avoidance.Obstacles)
    {
        const float ExpandedRadius        = std::max(0.01f, Obstacle.Radius + Avoidance.ClearanceRadiusMeters);
        const Mathematics::Axis3D ToPivot = PivotPosition - Obstacle.Center;
        const float HalfB                 = Mathematics::EvaluateDot(ToPivot, RayDir);
        const float C                     = Mathematics::EvaluateDot(ToPivot, ToPivot) - ExpandedRadius * ExpandedRadius;
        const float Discrim               = HalfB * HalfB - C;
        if (Discrim < 0.0f)
        {
            continue;
        }
        const float RootNear = -HalfB - std::sqrt(Discrim);
        if (RootNear > 0.0f && RootNear < SafeDist)
        {
            SafeDist = std::max(MinAllowedDistance, RootNear);
        }
    }

    return SafeDist;
}

} // namespace Frontier::Components::Constraints
