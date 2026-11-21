/*============================================================================================================================================
                                                           CONSTRAINTSOLVER.CPP
============================================================================================================================================*/
// 🧩 Strain-bond rupture ratio evaluation utilities for two-body physics constraints

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/ConstraintSolver.h"

#include <algorithm>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

float ConstraintSolver::EvaluateBondStrainRatio(
    float AppliedForceN,
    float AppliedTorqueNm,
    float BreakForceN,
    float BreakTorqueNm) noexcept
{
    float Ratio = 0.0f;
    if (BreakForceN > 1.0e-5f)
    {
        Ratio = std::max(Ratio, AppliedForceN / BreakForceN);
    }
    if (BreakTorqueNm > 1.0e-5f)
    {
        Ratio = std::max(Ratio, AppliedTorqueNm / BreakTorqueNm);
    }
    return Ratio;
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
