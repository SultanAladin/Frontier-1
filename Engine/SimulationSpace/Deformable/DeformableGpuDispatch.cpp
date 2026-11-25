/*============================================================================================================================================
                                                     DEFORMABLEGPUDISPATCH.CPP
============================================================================================================================================*/
// 🧩 Phase D10 16B-aligned GPU SSBO buffer packer & lock-free conflict-colored Vulkan compute dispatch planner

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableGpuDispatch.h"

#include <algorithm>
#include <bit>

namespace Frontier::SimulationSpace::Deformable
{

namespace
{

[[nodiscard]] uint32_t CeilDiv64(uint32_t count) noexcept
{
    return (count + 63u) / 64u;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE GPU DISPATCH
//------------------------------------------------------------------------------------------------------------------------

void DeformableGpuDispatch::PackBodyForGpuInto(
    const DeformableBody& body,
    const DeformableSimulationConfiguration& configuration,
    DeformableGpuBufferPack& pack)
{
    const uint32_t N = body.PointCount;
    pack.PositionsInvMass.resize(N);
    pack.PrevPositionsStrain.resize(N);
    pack.VelocitiesFlags.resize(N);

    for (uint32_t i = 0u; i < N; ++i)
    {
        const size_t i3 = static_cast<size_t>(i) * 3u;
        const float invM = body.InverseMass[i];
        pack.PositionsInvMass[i] = {
            body.Positions[i3 + 0u],
            body.Positions[i3 + 1u],
            body.Positions[i3 + 2u],
            invM
        };
        pack.PrevPositionsStrain[i] = {
            body.PrevPositions[i3 + 0u],
            body.PrevPositions[i3 + 1u],
            body.PrevPositions[i3 + 2u],
            body.PointStrain[i]
        };
        const uint32_t flags = (invM <= 0.0f) ? 1u : 0u;
        pack.VelocitiesFlags[i] = {
            body.Velocities[i3 + 0u],
            body.Velocities[i3 + 1u],
            body.Velocities[i3 + 2u],
            std::bit_cast<float>(flags)
        };
    }

    const float stiffScale = std::max(0.01f, configuration.StretchStiffness / 1200.0f);
    pack.DistanceConstraints.resize(body.DistanceConstraints.size());
    for (size_t i = 0u; i < body.DistanceConstraints.size(); ++i)
    {
        const auto& c = body.DistanceConstraints[i];
        const float effStiff = std::max(1.0e-3f, c.Stiffness * stiffScale);
        pack.DistanceConstraints[i] = DistanceConstraintGpuRecord {
            c.PointA,
            c.PointB,
            c.RestLength,
            1.0f / effStiff,
            c.BreakRatio,
            c.Severed ? 1u : 0u
        };
    }
    pack.DistanceBatchOrder.resize(body.DistanceBatches.OrderedOrdinals.size());
    if (!body.DistanceBatches.OrderedOrdinals.empty())
    {
        std::copy(body.DistanceBatches.OrderedOrdinals.begin(),
                  body.DistanceBatches.OrderedOrdinals.end(),
                  pack.DistanceBatchOrder.begin());
    }

    const float bendScale = std::max(0.01f, configuration.BendStiffness);
    pack.BendConstraints.resize(body.BendConstraints.size());
    for (size_t i = 0u; i < body.BendConstraints.size(); ++i)
    {
        const auto& b = body.BendConstraints[i];
        const float effStiff = std::max(1.0e-3f, b.Stiffness * bendScale);
        pack.BendConstraints[i] = BendConstraintGpuRecord {
            b.PointA,
            b.PointA,
            b.PointB,
            b.RestLength,
            1.0f / effStiff,
            0u
        };
    }
    pack.BendBatchOrder.resize(body.BendBatches.OrderedOrdinals.size());
    if (!body.BendBatches.OrderedOrdinals.empty())
    {
        std::copy(body.BendBatches.OrderedOrdinals.begin(),
                  body.BendBatches.OrderedOrdinals.end(),
                  pack.BendBatchOrder.begin());
    }

    // Lame parameters from Young's modulus E [MPa] & Poisson ratio nu [-]
    const float E  = std::max(0.05f, configuration.FemYoungMpa) * 1.0e6f;
    const float nu = std::clamp(configuration.FemPoissonNu, 0.05f, 0.485f);
    const float mu = E / (2.0f * (1.0f + nu));
    const float la = (E * nu) / ((1.0f + nu) * (1.0f - 2.0f * nu));

    const uint32_t T = body.TetCount;
    pack.TetElements.resize(T);
    pack.TetCofactorCols.resize(static_cast<size_t>(T) * 3u);
    for (uint32_t t = 0u; t < T; ++t)
    {
        const size_t t4 = static_cast<size_t>(t) * 4u;
        const size_t t9 = static_cast<size_t>(t) * 9u;
        const float* dm = body.TetInverseRestDm.data() + t9;

        TetElementGpuRecord rec {};
        rec.Indices   = { body.TetIndices[t4 + 0u], body.TetIndices[t4 + 1u], body.TetIndices[t4 + 2u], body.TetIndices[t4 + 3u] };
        rec.DmInvRow0 = { dm[0], dm[1], dm[2], body.TetRestVolume[t] };
        rec.DmInvRow1 = { dm[3], dm[4], dm[5], mu };
        rec.DmInvRow2 = { dm[6], dm[7], dm[8], la };
        pack.TetElements[t] = rec;

        const float* cof = body.TetCofactorNormals.data() + t9;
        pack.TetCofactorCols[static_cast<size_t>(t) * 3u + 0u] = { cof[0], cof[1], cof[2], 0.0f };
        pack.TetCofactorCols[static_cast<size_t>(t) * 3u + 1u] = { cof[3], cof[4], cof[5], 0.0f };
        pack.TetCofactorCols[static_cast<size_t>(t) * 3u + 2u] = { cof[6], cof[7], cof[8], 0.0f };
    }
    pack.TetBatchOrder.resize(body.TetBatches.OrderedOrdinals.size());
    if (!body.TetBatches.OrderedOrdinals.empty())
    {
        std::copy(body.TetBatches.OrderedOrdinals.begin(),
                  body.TetBatches.OrderedOrdinals.end(),
                  pack.TetBatchOrder.begin());
    }

    pack.TotalUploadBytes =
        static_cast<uint64_t>(pack.PositionsInvMass.size()) * 16ull +
        static_cast<uint64_t>(pack.PrevPositionsStrain.size()) * 16ull +
        static_cast<uint64_t>(pack.VelocitiesFlags.size()) * 16ull +
        static_cast<uint64_t>(pack.DistanceConstraints.size()) * sizeof(DistanceConstraintGpuRecord) +
        static_cast<uint64_t>(pack.DistanceBatchOrder.size()) * sizeof(uint32_t) +
        static_cast<uint64_t>(pack.BendConstraints.size()) * sizeof(BendConstraintGpuRecord) +
        static_cast<uint64_t>(pack.BendBatchOrder.size()) * sizeof(uint32_t) +
        static_cast<uint64_t>(pack.TetElements.size()) * sizeof(TetElementGpuRecord) +
        static_cast<uint64_t>(pack.TetBatchOrder.size()) * sizeof(uint32_t) +
        static_cast<uint64_t>(pack.TetCofactorCols.size()) * 16ull;
}

DeformableGpuBufferPack DeformableGpuDispatch::PackBodyForGpu(
    const DeformableBody& body,
    const DeformableSimulationConfiguration& configuration)
{
    DeformableGpuBufferPack pack {};
    PackBodyForGpuInto(body, configuration, pack);
    return pack;
}

bool DeformableGpuDispatch::UnpackPositionsFromGpu(
    const DeformableGpuBufferPack& pack,
    DeformableBody& targetBody) noexcept
{
    if (pack.PositionsInvMass.size() != targetBody.PointCount ||
        pack.PrevPositionsStrain.size() != targetBody.PointCount ||
        pack.VelocitiesFlags.size() != targetBody.PointCount)
    {
        return false;
    }

    for (uint32_t i = 0u; i < targetBody.PointCount; ++i)
    {
        const size_t i3 = static_cast<size_t>(i) * 3u;
        targetBody.Positions[i3 + 0u]     = pack.PositionsInvMass[i][0];
        targetBody.Positions[i3 + 1u]     = pack.PositionsInvMass[i][1];
        targetBody.Positions[i3 + 2u]     = pack.PositionsInvMass[i][2];
        targetBody.PrevPositions[i3 + 0u] = pack.PrevPositionsStrain[i][0];
        targetBody.PrevPositions[i3 + 1u] = pack.PrevPositionsStrain[i][1];
        targetBody.PrevPositions[i3 + 2u] = pack.PrevPositionsStrain[i][2];
        targetBody.PointStrain[i]         = pack.PrevPositionsStrain[i][3];
        targetBody.Velocities[i3 + 0u]    = pack.VelocitiesFlags[i][0];
        targetBody.Velocities[i3 + 1u]    = pack.VelocitiesFlags[i][1];
        targetBody.Velocities[i3 + 2u]    = pack.VelocitiesFlags[i][2];
    }
    return true;
}

void DeformableGpuDispatch::BuildTickDispatchPlanInto(
    const DeformableBody& body,
    const DeformableSimulationConfiguration& configuration,
    DeformableSubstepDispatchPlan& plan,
    bool appendVatSliceBake,
    uint32_t highPolyVertexCount)
{
    plan.Substeps           = std::max(1u, configuration.Substeps);
    plan.TotalDispatchCount = 0u;
    plan.TotalBarrierCount  = 0u;
    plan.Steps.clear();

    if (body.PointCount == 0u)
    {
        return;
    }

    const auto pushStep = [&](DeformableComputePassOrdinal pass, uint32_t sub, uint32_t color, uint32_t start, uint32_t count)
    {
        if (count == 0u) return;
        DeformableComputeDispatchStep step {};
        step.Pass                  = pass;
        step.SubstepIndex          = sub;
        step.ColorBatchIndex       = color;
        step.BatchStartOffset      = start;
        step.WorkItemCount         = count;
        step.ThreadGroupsX         = CeilDiv64(count);
        step.RequiresMemoryBarrier = true;
        plan.Steps.push_back(step);
        ++plan.TotalDispatchCount;
        ++plan.TotalBarrierCount;
    };

    for (uint32_t sub = 0u; sub < plan.Substeps; ++sub)
    {
        pushStep(DeformableComputePassOrdinal::PredictIntegrate, sub, 0u, 0u, body.PointCount);

        for (uint32_t c = 0u; c < body.DistanceBatches.ColorCount; ++c)
        {
            const uint32_t start = body.DistanceBatches.BatchOffsets[c];
            const uint32_t end   = body.DistanceBatches.BatchOffsets[c + 1u];
            pushStep(DeformableComputePassOrdinal::DistanceBatch, sub, c, start, end - start);
        }

        for (uint32_t c = 0u; c < body.BendBatches.ColorCount; ++c)
        {
            const uint32_t start = body.BendBatches.BatchOffsets[c];
            const uint32_t end   = body.BendBatches.BatchOffsets[c + 1u];
            pushStep(DeformableComputePassOrdinal::BendBatch, sub, c, start, end - start);
        }

        for (uint32_t c = 0u; c < body.TetBatches.ColorCount; ++c)
        {
            const uint32_t start = body.TetBatches.BatchOffsets[c];
            const uint32_t end   = body.TetBatches.BatchOffsets[c + 1u];
            pushStep(DeformableComputePassOrdinal::NeoHookeanTet, sub, c, start, end - start);
        }

        pushStep(DeformableComputePassOrdinal::ContactUpdate, sub, 0u, 0u, body.PointCount);
    }

    if (appendVatSliceBake && highPolyVertexCount > 0u)
    {
        pushStep(DeformableComputePassOrdinal::VatSliceBake, plan.Substeps - 1u, 0u, 0u, highPolyVertexCount);
    }
}

DeformableSubstepDispatchPlan DeformableGpuDispatch::BuildTickDispatchPlan(
    const DeformableBody& body,
    const DeformableSimulationConfiguration& configuration,
    bool appendVatSliceBake,
    uint32_t highPolyVertexCount)
{
    DeformableSubstepDispatchPlan plan {};
    BuildTickDispatchPlanInto(body, configuration, plan, appendVatSliceBake, highPolyVertexCount);
    return plan;
}

} // namespace Frontier::SimulationSpace::Deformable
