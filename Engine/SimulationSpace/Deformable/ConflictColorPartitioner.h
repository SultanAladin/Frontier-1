/*============================================================================================================================================
                                                      CONFLICTCOLORPARTITIONER.H
============================================================================================================================================*/
// 🧩 Phase D01 Zero-atomic greedy graph-coloring partitioner for parallel GPU XPBD edges & 3D Neo-Hookean tetrahedra
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

#include <cstdint>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                               CONFLICT COLOR PARTITIONER
//------------------------------------------------------------------------------------------------------------------------

class ConflictColorPartitioner
{
public:
    /// Partitions all 1D distance edges, 1D bend edges, and 3D tetrahedra inside `body`
    /// into vertex-disjoint color batches for lock-free GPU compute dispatches.
    static void PartitionAllConstraints(DeformableBody& body);

    /// Partitions a flat index array (stride = 2 for edges, 4 for tetrahedra) into zero-conflict color batches.
    [[nodiscard]] static ConflictColorBatches PartitionTuples(
        uint32_t pointCount,
        const uint32_t* tupleIndices,
        uint32_t tupleCount,
        uint32_t tupleStride);

    /// Diagnostic verifier: confirms no two tuples inside the same color batch share any mass point index.
    [[nodiscard]] static bool VerifyZeroVertexConflicts(
        uint32_t pointCount,
        const uint32_t* tupleIndices,
        uint32_t tupleStride,
        const ConflictColorBatches& batches);
};

} // namespace Frontier::SimulationSpace::Deformable
