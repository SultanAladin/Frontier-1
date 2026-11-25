/*============================================================================================================================================
                                                        PNEUMATICTIRESOLVER.H
============================================================================================================================================*/
// 🧩 Phase D03 Co-rotating pneumatic XPBD & 3D Neo-Hookean FEM tire dynamics (Boyle bulge, outward centrifugal growth, rim keep-out)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.2

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PNEUMATIC TIRE SOLVER
//------------------------------------------------------------------------------------------------------------------------

class PneumaticTireSolver
{
public:
    /// Advances co-rotating transport, Boyle cavity volume ratio, axle suspension, outward centrifugal growth,
    /// and internal air pressure for all Pneumatic XPBD and 3D FEM tires in `body`.
    static void ApplySubstepTireForces(
        DeformableBody& body,
        float h,
        const DeformableSimulationConfiguration& configuration);

    /// Enforces alloy rim-barrel radial keep-out so low-PSI tread never penetrates the metallic rim.
    static void ProjectRimKeepOut(DeformableBody& body);

    /// Lifts every tire assembly vertically by `liftMeters` for drop-impact verification tests.
    static void TriggerDropLift(DeformableBody& body, float liftMeters = 1.25f);

    /// Computes enclosed air-cavity volume for either a pneumatic carcass or inner FEM rubber liner.
    [[nodiscard]] static float ComputeCavityVolume(
        const float* positions,
        uint32_t startPoint,
        uint32_t sectors,
        uint32_t profileCount,
        uint32_t layerStride,
        float axleX,
        float axleY,
        float axleZ,
        float rimRadius);
};

} // namespace Frontier::SimulationSpace::Deformable
