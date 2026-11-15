/*============================================================================================================================================
                                                           FIELDLATTICECASCADE.CPP
============================================================================================================================================*/
// 🧩 4-tier cascaded 3D GPU/CPU velocity & vorticity lattice (2m, 16m, 256m, 100km weather bounds) with SSBO packing

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                               LIFECYCLE & CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

FieldLatticeCascade::FieldLatticeCascade(uint32_t TierResolution) noexcept
{
    ReconfigureResolution(TierResolution);
}

void FieldLatticeCascade::ReconfigureResolution(uint32_t TierResolution) noexcept
{
    const uint32_t ClampedRes = std::clamp(TierResolution, 4u, 64u);
    constexpr std::array<float, FieldCascadeTierCount> HalfExtents = {{
        16.0f,     // Tier 0: 32m box (~2m voxel resolution at 16^3)
        128.0f,    // Tier 1: 256m box (~16m voxel resolution)
        2048.0f,   // Tier 2: 4.096km box (~256m voxel resolution)
        50000.0f,  // Tier 3: 100km weather envelope
    }};

    const std::size_t VoxelCount = static_cast<std::size_t>(ClampedRes) * ClampedRes * ClampedRes;
    for (uint32_t Tier = 0u; Tier < FieldCascadeTierCount; ++Tier)
    {
        TierDescriptors[Tier].TierIndex             = Tier;
        TierDescriptors[Tier].GridResolution        = ClampedRes;
        TierDescriptors[Tier].WorldHalfExtentMeters = HalfExtents[Tier];
        TierVoxels[Tier].assign(VoxelCount, FieldLatticeVoxelSample{});
    }
}

void FieldLatticeCascade::AssignFocusOrigin(const SpatialVector3& FocusMeters) noexcept
{
    for (FieldLatticeTierDescriptor& Desc : TierDescriptors)
    {
        Desc.FocusOriginMeters = FocusMeters;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             SSBO PACKING & CPU MIRROR
//------------------------------------------------------------------------------------------------------------------------

void FieldLatticeCascade::PackGpuSourceBuffers(
    const ForceFieldAccumulator&         Accumulator,
    double                               TimeSeconds,
    std::vector<ForceSourceShaderEntry>& OutShaderSources,
    std::vector<SpatialVector3>&         OutPackedWaypoints) const
{
    OutShaderSources.clear();
    OutPackedWaypoints.clear();

    const std::span<const ForceSourceRecordEntry> Sources = Accumulator.QuerySources();
    bool HasSolo = false;
    for (const ForceSourceRecordEntry& Entry : Sources)
    {
        if (Entry.Enabled && Entry.Solo)
        {
            HasSolo = true;
            break;
        }
    }

    for (const ForceSourceRecordEntry& Entry : Sources)
    {
        if (!Entry.Enabled || (HasSolo && !Entry.Solo))
        {
            continue;
        }
        if (OutShaderSources.size() >= MaxActiveForceSources)
        {
            break;
        }

        const uint32_t StartIdx = static_cast<uint32_t>(OutPackedWaypoints.size());
        const uint32_t WpCount  = std::min<uint32_t>(Entry.Motion.WaypointCount, MaxForceMotionWaypoints);
        for (uint32_t K = 0u; K < WpCount; ++K)
        {
            OutPackedWaypoints.push_back(Entry.Motion.Waypoints[K]);
        }

        OutShaderSources.push_back(PackForceSourceForShader(Entry, TimeSeconds, StartIdx));
    }
}

SpatialVector3 FieldLatticeCascade::ComputeVoxelWorldCoordinate(
    uint32_t TierIndex,
    uint32_t Ix,
    uint32_t Iy,
    uint32_t Iz) const noexcept
{
    const FieldLatticeTierDescriptor& Desc = TierDescriptors[std::min(TierIndex, FieldCascadeTierCount - 1u)];
    const float Div = static_cast<float>(std::max(1u, Desc.GridResolution - 1u));
    const float Nx  = (static_cast<float>(Ix) / Div) * 2.0f - 1.0f;
    const float Ny  = (static_cast<float>(Iy) / Div) * 2.0f - 1.0f;
    const float Nz  = (static_cast<float>(Iz) / Div) * 2.0f - 1.0f;
    return {
        Desc.FocusOriginMeters.X + Nx * Desc.WorldHalfExtentMeters,
        Desc.FocusOriginMeters.Y + Ny * Desc.WorldHalfExtentMeters,
        Desc.FocusOriginMeters.Z + Nz * Desc.WorldHalfExtentMeters,
        0.0f,
    };
}

void FieldLatticeCascade::EvaluateTierOnCpu(
    uint32_t                     TierIndex,
    const ForceFieldAccumulator& Accumulator,
    double                       TimeSeconds)
{
    if (TierIndex >= FieldCascadeTierCount)
    {
        return;
    }

    const FieldLatticeTierDescriptor& Desc = TierDescriptors[TierIndex];
    const uint32_t Res = Desc.GridResolution;
    std::vector<FieldLatticeVoxelSample>& Voxels = TierVoxels[TierIndex];
    Voxels.resize(static_cast<std::size_t>(Res) * Res * Res);

    for (uint32_t Iz = 0u; Iz < Res; ++Iz)
    {
        for (uint32_t Iy = 0u; Iy < Res; ++Iy)
        {
            for (uint32_t Ix = 0u; Ix < Res; ++Ix)
            {
                const SpatialVector3 Pos = ComputeVoxelWorldCoordinate(TierIndex, Ix, Iy, Iz);
                const SpatialForceSample Sample = Accumulator.QueryForceFieldSample(Pos, {}, TimeSeconds);
                const SpatialVector3 Combined = Sample.AdvectionVelocity + Sample.LinearAcceleration;

                // Lightweight finite difference curl estimate along local grid step
                constexpr float StepM = 0.25f;
                const SpatialForceSample DxP = Accumulator.QueryForceFieldSample({ Pos.X + StepM, Pos.Y, Pos.Z, 0.0f }, {}, TimeSeconds);
                const SpatialForceSample DyP = Accumulator.QueryForceFieldSample({ Pos.X, Pos.Y + StepM, Pos.Z, 0.0f }, {}, TimeSeconds);
                const SpatialForceSample DzP = Accumulator.QueryForceFieldSample({ Pos.X, Pos.Y, Pos.Z + StepM, 0.0f }, {}, TimeSeconds);

                const SpatialVector3 VxP = DxP.AdvectionVelocity + DxP.LinearAcceleration;
                const SpatialVector3 VyP = DyP.AdvectionVelocity + DyP.LinearAcceleration;
                const SpatialVector3 VzP = DzP.AdvectionVelocity + DzP.LinearAcceleration;

                const float InvStep = 1.0f / StepM;
                const float CurlX   = ((VyP.Z - Combined.Z) - (VzP.Y - Combined.Y)) * InvStep;
                const float CurlY   = ((VzP.X - Combined.X) - (VxP.Z - Combined.Z)) * InvStep;
                const float CurlZ   = ((VxP.Y - Combined.Y) - (VyP.X - Combined.X)) * InvStep;

                const std::size_t LinearIdx =
                    (static_cast<std::size_t>(Iz) * Res + Iy) * Res + Ix;
                Voxels[LinearIdx] = {
                    Combined.X,
                    Combined.Y,
                    Combined.Z,
                    std::sqrt(CurlX * CurlX + CurlY * CurlY + CurlZ * CurlZ),
                };
            }
        }
    }
}

FieldLatticeTierDescriptor FieldLatticeCascade::QueryTierDescriptor(uint32_t TierIndex) const noexcept
{
    return TierDescriptors[std::min(TierIndex, FieldCascadeTierCount - 1u)];
}

const std::vector<FieldLatticeVoxelSample>& FieldLatticeCascade::QueryTierVoxels(uint32_t TierIndex) const noexcept
{
    return TierVoxels[std::min(TierIndex, FieldCascadeTierCount - 1u)];
}

FieldLatticeVoxelSample FieldLatticeCascade::SampleTierTrilinear(
    uint32_t              TierIndex,
    const SpatialVector3& WorldPoint) const noexcept
{
    if (TierIndex >= FieldCascadeTierCount)
    {
        return {};
    }
    const FieldLatticeTierDescriptor& Desc = TierDescriptors[TierIndex];
    const uint32_t Res = Desc.GridResolution;
    const auto& Voxels = TierVoxels[TierIndex];
    if (Res < 2u || Voxels.size() < static_cast<std::size_t>(Res) * Res * Res)
    {
        return {};
    }

    const float Half = std::max(1.0e-3f, Desc.WorldHalfExtentMeters);
    const float Fx   = std::clamp(((WorldPoint.X - Desc.FocusOriginMeters.X) / Half) * 0.5f + 0.5f, 0.0f, 1.0f) * static_cast<float>(Res - 1u);
    const float Fy   = std::clamp(((WorldPoint.Y - Desc.FocusOriginMeters.Y) / Half) * 0.5f + 0.5f, 0.0f, 1.0f) * static_cast<float>(Res - 1u);
    const float Fz   = std::clamp(((WorldPoint.Z - Desc.FocusOriginMeters.Z) / Half) * 0.5f + 0.5f, 0.0f, 1.0f) * static_cast<float>(Res - 1u);

    const uint32_t X0 = std::min(Res - 2u, static_cast<uint32_t>(std::floor(Fx)));
    const uint32_t Y0 = std::min(Res - 2u, static_cast<uint32_t>(std::floor(Fy)));
    const uint32_t Z0 = std::min(Res - 2u, static_cast<uint32_t>(std::floor(Fz)));
    const float Tx = Fx - static_cast<float>(X0);
    const float Ty = Fy - static_cast<float>(Y0);
    const float Tz = Fz - static_cast<float>(Z0);

    auto Load = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> const FieldLatticeVoxelSample&
    {
        return Voxels[(static_cast<std::size_t>(Z) * Res + Y) * Res + X];
    };

    auto Mix = [](const FieldLatticeVoxelSample& A, const FieldLatticeVoxelSample& B, float T) noexcept -> FieldLatticeVoxelSample
    {
        return {
            A.VelocityX  + (B.VelocityX  - A.VelocityX)  * T,
            A.VelocityY  + (B.VelocityY  - A.VelocityY)  * T,
            A.VelocityZ  + (B.VelocityZ  - A.VelocityZ)  * T,
            A.CurlScalar + (B.CurlScalar - A.CurlScalar) * T,
        };
    };

    const FieldLatticeVoxelSample C00 = Mix(Load(X0, Y0, Z0), Load(X0 + 1u, Y0, Z0), Tx);
    const FieldLatticeVoxelSample C10 = Mix(Load(X0, Y0 + 1u, Z0), Load(X0 + 1u, Y0 + 1u, Z0), Tx);
    const FieldLatticeVoxelSample C01 = Mix(Load(X0, Y0, Z0 + 1u), Load(X0 + 1u, Y0, Z0 + 1u), Tx);
    const FieldLatticeVoxelSample C11 = Mix(Load(X0, Y0 + 1u, Z0 + 1u), Load(X0 + 1u, Y0 + 1u, Z0 + 1u), Tx);
    return Mix(Mix(C00, C10, Ty), Mix(C01, C11, Ty), Tz);
}

} // namespace Frontier::SimulationSpace::ForceFields
