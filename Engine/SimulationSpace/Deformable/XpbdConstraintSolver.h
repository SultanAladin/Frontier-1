/*============================================================================================================================================
                                                        XPBDCONSTRAINTSOLVER.H
============================================================================================================================================*/
// 🧩 Phase D02 Unified XPBD + 3D Stable Neo-Hookean FEM + Pneumatic Tire step integrator (exact CPU reference & parity solver)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformablePhysicsCoupling.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  XPBD CONSTRAINT SOLVER
//------------------------------------------------------------------------------------------------------------------------

class XpbdConstraintSolver
{
public:
    /// Advances `body` by one frame step `dtSeconds` split into `configuration.Substeps` XPBD/FEM substeps.
    static DeformableCouplingFeedback Step(
        DeformableBody& body,
        float dtSeconds,
        const DeformableSimulationConfiguration& configuration,
        float simulationTimeSec = 0.0f,
        float windPulse = 0.0f,
        const DeformableExternalCoupling* externalCoupling = nullptr);

    /// Interactive snip/tear utility: severs all distance constraints intersecting within `cutRadiusMeters` of segment (a, b).
    [[nodiscard]] static uint32_t CutConstraintsNearSegment(
        DeformableBody& body,
        const std::array<float, 3>& segA,
        const std::array<float, 3>& segB,
        float cutRadiusMeters = 0.14f);
};

} // namespace Frontier::SimulationSpace::Deformable
