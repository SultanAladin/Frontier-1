/*============================================================================================================================================
                                                      TETRAHEDRALCAGEEMBEDDER.H
============================================================================================================================================*/
// 🧩 Phase D04 Exact 3D tetrahedral barycentric cage embedder & Nanson cofactor normal skinning (3D FEM -> shared high-poly surface)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.4

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 TETRAHEDRAL CAGE EMBEDDER
//------------------------------------------------------------------------------------------------------------------------

class TetrahedralCageEmbedder
{
public:
    /// Bakes exact 3D barycentric coordinates using `body.TetInverseRestDm` for every vertex in `restSurface`.
    /// Uses fast sector-bucket search when embedding onto a `FemTireSolid` and falls back to full scan otherwise.
    [[nodiscard]] static CageEmbeddedSurface EmbedIntoFemSolid(
        const SimulationSpace::Deformable::DeformableBody& body,
        const HighPolySurfaceRest& restSurface);

    /// Evaluates live deformed world positions, Nanson cofactor normals (cof(F) * n_rest), and interpolated 3D strain on CPU.
    /// Produces bit-identical math to DeformableCageSkinning.slang (Mode 0) for headless CTest & VAT baking.
    static void EvaluateLiveSurface(
        const SimulationSpace::Deformable::DeformableBody& body,
        const CageEmbeddedSurface& embedded,
        DeformedSurfaceSample& outSample);
};

} // namespace Frontier::GeometrySpace::Deformation
