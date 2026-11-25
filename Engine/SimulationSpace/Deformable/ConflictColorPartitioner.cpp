/*============================================================================================================================================
                                                     CONFLICTCOLORPARTITIONER.CPP
============================================================================================================================================*/
// 🧩 Phase D01 Zero-atomic greedy graph-coloring partitioner for parallel GPU XPBD edges & 3D Neo-Hookean tetrahedra

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/ConflictColorPartitioner.h"

#include <vector>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                               CONFLICT COLOR PARTITIONER
//------------------------------------------------------------------------------------------------------------------------

ConflictColorBatches ConflictColorPartitioner::PartitionTuples(
    uint32_t pointCount,
    const uint32_t* tupleIndices,
    uint32_t tupleCount,
    uint32_t tupleStride)
{
    ConflictColorBatches out {};
    if (tupleCount == 0u || tupleStride == 0u || pointCount == 0u)
    {
        out.BatchOffsets.push_back(0u);
        return out;
    }

    // Track assigned color bitsets per mass point using expandable 64-bit word slices (supports >64 colors if ever needed).
    constexpr uint32_t kWordsPerPoint = 4u; // Up to 256 distinct colors
    std::vector<uint64_t> usedMasks(static_cast<size_t>(pointCount) * kWordsPerPoint, 0ull);
    std::vector<uint32_t> assignedColor(tupleCount, 0u);
    uint32_t maxColor = 0u;

    for (uint32_t item = 0u; item < tupleCount; ++item)
    {
        const uint32_t* verts = tupleIndices + static_cast<size_t>(item) * tupleStride;
        uint32_t chosenColor = 0u;
        bool found = false;

        for (uint32_t word = 0u; word < kWordsPerPoint && !found; ++word)
        {
            uint64_t blocked = 0ull;
            for (uint32_t k = 0u; k < tupleStride; ++k)
            {
                const uint32_t pt = verts[k];
                if (pt < pointCount)
                {
                    blocked |= usedMasks[static_cast<size_t>(pt) * kWordsPerPoint + word];
                }
            }
            const uint64_t freeBits = ~blocked;
            if (freeBits != 0ull)
            {
                uint32_t bit = 0u;
                while (((freeBits >> bit) & 1ull) == 0ull)
                {
                    ++bit;
                }
                chosenColor = word * 64u + bit;
                found = true;
            }
        }

        if (!found)
        {
            chosenColor = maxColor + 1u;
        }

        assignedColor[item] = chosenColor;
        if (chosenColor > maxColor)
        {
            maxColor = chosenColor;
        }

        const uint32_t word = chosenColor / 64u;
        const uint32_t bit  = chosenColor % 64u;
        if (word < kWordsPerPoint)
        {
            const uint64_t mask = 1ull << bit;
            for (uint32_t k = 0u; k < tupleStride; ++k)
            {
                const uint32_t pt = verts[k];
                if (pt < pointCount)
                {
                    usedMasks[static_cast<size_t>(pt) * kWordsPerPoint + word] |= mask;
                }
            }
        }
    }

    const uint32_t colorCount = maxColor + 1u;
    out.ColorCount = colorCount;
    out.BatchOffsets.assign(static_cast<size_t>(colorCount) + 1u, 0u);
    for (uint32_t item = 0u; item < tupleCount; ++item)
    {
        ++out.BatchOffsets[assignedColor[item] + 1u];
    }
    for (uint32_t c = 0u; c < colorCount; ++c)
    {
        out.BatchOffsets[c + 1u] += out.BatchOffsets[c];
    }

    std::vector<uint32_t> writeCursor = out.BatchOffsets;
    out.OrderedOrdinals.assign(tupleCount, 0u);
    for (uint32_t item = 0u; item < tupleCount; ++item)
    {
        const uint32_t c = assignedColor[item];
        out.OrderedOrdinals[writeCursor[c]++] = item;
    }

    return out;
}

void ConflictColorPartitioner::PartitionAllConstraints(DeformableBody& body)
{
    if (!body.DistanceConstraints.empty())
    {
        std::vector<uint32_t> distIdx(body.DistanceConstraints.size() * 2u);
        for (size_t i = 0u; i < body.DistanceConstraints.size(); ++i)
        {
            distIdx[i * 2u + 0u] = body.DistanceConstraints[i].PointA;
            distIdx[i * 2u + 1u] = body.DistanceConstraints[i].PointB;
        }
        body.DistanceBatches = PartitionTuples(
            body.PointCount, distIdx.data(), static_cast<uint32_t>(body.DistanceConstraints.size()), 2u);
    }
    else
    {
        body.DistanceBatches = {};
        body.DistanceBatches.BatchOffsets.push_back(0u);
    }

    if (!body.BendConstraints.empty())
    {
        std::vector<uint32_t> bendIdx(body.BendConstraints.size() * 2u);
        for (size_t i = 0u; i < body.BendConstraints.size(); ++i)
        {
            bendIdx[i * 2u + 0u] = body.BendConstraints[i].PointA;
            bendIdx[i * 2u + 1u] = body.BendConstraints[i].PointB;
        }
        body.BendBatches = PartitionTuples(
            body.PointCount, bendIdx.data(), static_cast<uint32_t>(body.BendConstraints.size()), 2u);
    }
    else
    {
        body.BendBatches = {};
        body.BendBatches.BatchOffsets.push_back(0u);
    }

    if (body.TetCount > 0u)
    {
        body.TetBatches = PartitionTuples(body.PointCount, body.TetIndices.data(), body.TetCount, 4u);
    }
    else
    {
        body.TetBatches = {};
        body.TetBatches.BatchOffsets.push_back(0u);
    }
}

bool ConflictColorPartitioner::VerifyZeroVertexConflicts(
    uint32_t pointCount,
    const uint32_t* tupleIndices,
    uint32_t tupleStride,
    const ConflictColorBatches& batches)
{
    if (batches.ColorCount == 0u)
    {
        return batches.OrderedOrdinals.empty();
    }
    if (batches.BatchOffsets.size() != static_cast<size_t>(batches.ColorCount) + 1u)
    {
        return false;
    }

    std::vector<uint32_t> stamp(pointCount, 0u);
    for (uint32_t c = 0u; c < batches.ColorCount; ++c)
    {
        const uint32_t epoch = c + 1u;
        const uint32_t begin = batches.BatchOffsets[c];
        const uint32_t end   = batches.BatchOffsets[c + 1u];
        for (uint32_t k = begin; k < end; ++k)
        {
            const uint32_t item = batches.OrderedOrdinals[k];
            const uint32_t* verts = tupleIndices + static_cast<size_t>(item) * tupleStride;
            for (uint32_t v = 0u; v < tupleStride; ++v)
            {
                const uint32_t pt = verts[v];
                if (pt >= pointCount || stamp[pt] == epoch)
                {
                    return false;
                }
                stamp[pt] = epoch;
            }
        }
    }
    return true;
}

} // namespace Frontier::SimulationSpace::Deformable
