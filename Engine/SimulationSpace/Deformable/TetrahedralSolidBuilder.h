/*============================================================================================================================================
                                                       TETRAHEDRALSOLIDBUILDER.H
============================================================================================================================================*/
// 🧩 Phase D01 Conformal 3D hexahedron-to-5-tetrahedron solid builder, Dm^-1 & rest volume V0 precomputation
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.1

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TETRAHEDRAL BUILDER
//------------------------------------------------------------------------------------------------------------------------

class TetrahedralSolidBuilder
{
public:
    /// Appends one positively oriented 3D tetrahedron (automatically swaps i2/i3 if signed det(Dm) < 0).
    /// Returns true if non-degenerate (|det(Dm)| > 1e-10).
    static bool AppendTetrahedron(
        DeformableBody& body,
        uint32_t i0,
        uint32_t i1,
        uint32_t i2,
        uint32_t i3);

    /// Conformally decomposes an 8-corner hexahedral rubber cell into 5 positively oriented tetrahedra.
    /// Alternating `parityBit` across adjacent cells guarantees matching diagonal faces with zero T-junctions.
    /// Returns number of valid tetrahedra appended (expected 5).
    static uint32_t AppendHexCellAsFiveTets(
        DeformableBody& body,
        const std::array<uint32_t, 8>& hexCorners,
        uint32_t parityBit);

    /// Recomputes TetInverseRestDm (Q = Dm^-1), TetRestVolume (V0), and initializes identity Nanson cofactors.
    /// Returns number of degenerate (near-zero volume) tetrahedra encountered (expected 0).
    static uint32_t PrecomputeRestMatrices(DeformableBody& body);
};

} // namespace Frontier::SimulationSpace::Deformable
