/*============================================================================================================================================
                                                        NEOHOOKEANTETSOLVER.H
============================================================================================================================================*/
// 🧩 Phase D02 Macklin & Müller (2021) Stable Neo-Hookean 3D Volumetric FEM Tetrahedron Solver + Nanson Cofactor Normal Matrix
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.1

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   NEO-HOOKEAN TET SOLVER
//------------------------------------------------------------------------------------------------------------------------

class NeoHookeanTetSolver
{
public:
    /// Solves one XPBD substep pass over all 3D volumetric tetrahedra in `body`.
    /// When `useConflictBatches` is true and `body.TetBatches` is populated, iterates in exact GPU color-batch order.
    static void ProjectAllTetrahedra(
        DeformableBody& body,
        float invH2,
        const DeformableSimulationConfiguration& configuration,
        bool useConflictBatches = true);

    /// Scatters per-tetrahedron 3D strain (deviatoric + volumetric) onto incident mass points for heatmap visualization.
    static void ScatterPointStrain(DeformableBody& body);

private:
    static void ProjectSingleTetrahedron(
        DeformableBody& body,
        uint32_t tetOrdinal,
        float invH2,
        float mu,
        float lambda,
        float gamma);
};

} // namespace Frontier::SimulationSpace::Deformable
