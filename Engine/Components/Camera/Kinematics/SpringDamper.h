/*============================================================================================================================================
                                                             SPRINGDAMPER.H
============================================================================================================================================*/
// 🧩 Exact analytical critically-damped second-order spring-damper integration for FollowTarget camera pursuit

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components::Kinematics
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void AdvanceCriticallyDampedSpring(
    Mathematics::Axis3D& InOutPosition,
    Mathematics::Axis3D& InOutVelocity,
    Mathematics::Axis3D  TargetPosition,
    float                NaturalFrequencyRadPerSec,
    float                DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f)
    {
        return;
    }
    const float Omega = std::max(0.1f, NaturalFrequencyRadPerSec);
    const float Dt    = std::min(DeltaSeconds, 0.25f);
    const float ExpTerm = std::exp(-Omega * Dt);

    const Mathematics::Axis3D Displacement = InOutPosition - TargetPosition;
    const Mathematics::Axis3D TempTerm     = InOutVelocity + Displacement * Omega;

    InOutPosition = TargetPosition + (Displacement + TempTerm * Dt) * ExpTerm;
    InOutVelocity = (InOutVelocity - TempTerm * (Omega * Dt)) * ExpTerm;
}

} // namespace Frontier::Components::Kinematics
