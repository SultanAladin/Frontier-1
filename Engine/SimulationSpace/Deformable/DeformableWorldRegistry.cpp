/*============================================================================================================================================
                                                   DEFORMABLEWORLDREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Phase D15 Multi-instance world deformable registry with world origins, distance sleep/LOD culling & shared ForceField/Jolt coupling

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableWorldRegistry.h"
#include "Engine/Codecs/Simulation/SoftBodyCodec.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                DEFORMABLE WORLD REGISTRY
//------------------------------------------------------------------------------------------------------------------------

void DeformableWorldRegistry::TranslateBodyBy(DeformableBody& body, const std::array<float, 3>& offset) noexcept
{
    if (offset[0] == 0.0f && offset[1] == 0.0f && offset[2] == 0.0f)
    {
        return;
    }

    for (uint32_t i = 0u; i < body.PointCount; ++i)
    {
        const size_t i3 = static_cast<size_t>(i) * 3u;
        body.Positions[i3 + 0u]     += offset[0];
        body.Positions[i3 + 1u]     += offset[1];
        body.Positions[i3 + 2u]     += offset[2];
        body.PrevPositions[i3 + 0u] += offset[0];
        body.PrevPositions[i3 + 1u] += offset[1];
        body.PrevPositions[i3 + 2u] += offset[2];
        body.RestPositions[i3 + 0u] += offset[0];
        body.RestPositions[i3 + 1u] += offset[1];
        body.RestPositions[i3 + 2u] += offset[2];
    }
    for (auto& posture : body.PostureConstraints)
    {
        posture.TargetX += offset[0];
        posture.TargetY += offset[1];
        posture.TargetZ += offset[2];
    }
    for (auto& tire : body.FemTires)
    {
        tire.AxleX += offset[0];
        tire.AxleY += offset[1];
        tire.AxleZ += offset[2];
    }
    for (auto& tire : body.PneumaticTires)
    {
        tire.AxleX += offset[0];
        tire.AxleY += offset[1];
        tire.AxleZ += offset[2];
    }
    for (auto& prop : body.StaticProps)
    {
        prop.PositionMeters[0] += offset[0];
        prop.PositionMeters[1] += offset[1];
        prop.PositionMeters[2] += offset[2];
    }
    body.Collider.CenterMeters[0] += offset[0];
    body.Collider.CenterMeters[1] += offset[1];
    body.Collider.CenterMeters[2] += offset[2];
    body.GroundY                  += offset[1];
}

void DeformableWorldRegistry::InitializeInstanceExecutor(DeformableWorldInstance& inst)
{
    if (!inst.GpuExecutor)
    {
        inst.GpuExecutor = std::make_unique<DeformableVulkanExecutor>();
    }
#if FRONTIER_DEFORMABLE_HAS_VULKAN
    if (Interchange_ != nullptr)
    {
        (void)inst.GpuExecutor->AttachVulkanInterchange(Interchange_);
    }
    (void)inst.GpuExecutor->InitializeForBody(inst.Body, inst.Configuration, CompiledShaderDirectory_);
#else
    (void)inst.GpuExecutor->InitializeForBody(inst.Body, inst.Configuration);
#endif
}

#if FRONTIER_DEFORMABLE_HAS_VULKAN
bool DeformableWorldRegistry::AttachVulkanInterchange(
    GeometricRaster::VulkanInterchange* interchange,
    const std::filesystem::path& compiledShaderDirectory) noexcept
{
    Interchange_ = interchange;
    if (!compiledShaderDirectory.empty())
    {
        CompiledShaderDirectory_ = compiledShaderDirectory;
    }
    bool allOk = (interchange != nullptr && interchange->Ready());
    for (auto& inst : Instances_)
    {
        if (!inst.GpuExecutor)
        {
            inst.GpuExecutor = std::make_unique<DeformableVulkanExecutor>();
        }
        const bool attached = inst.GpuExecutor->AttachVulkanInterchange(interchange);
        const bool inited   = inst.GpuExecutor->InitializeForBody(inst.Body, inst.Configuration, CompiledShaderDirectory_);
        allOk = allOk && attached && inited;
    }
    return allOk;
}
#endif

uint32_t DeformableWorldRegistry::SpawnPreset(
    DeformablePresetKind kind,
    const std::array<float, 3>& worldOriginMeters,
    std::string instanceName)
{
    DeformableWorldInstance inst {};
    inst.InstanceId        = NextInstanceId_++;
    inst.WorldOriginMeters = worldOriginMeters;
    DeformablePresetRegistry::BuildPreset(kind, inst.Body, inst.Configuration);
    TranslateBodyBy(inst.Body, worldOriginMeters);
    inst.InstanceName = instanceName.empty()
        ? (inst.Body.PresetId + "_" + std::to_string(inst.InstanceId))
        : std::move(instanceName);
    InitializeInstanceExecutor(inst);

    Instances_.push_back(std::move(inst));
    return Instances_.back().InstanceId;
}

uint32_t DeformableWorldRegistry::SpawnFromSoftBodyFile(
    const std::filesystem::path& softBodyPath,
    const std::array<float, 3>& worldOriginMeters,
    std::string instanceName,
    std::string* outError)
{
    Codecs::Simulation::SoftBodyDocumentModel doc {};
    if (!Codecs::Simulation::SoftBodyCodec::DecodeFromFile(softBodyPath, doc, outError))
    {
        return 0u;
    }

    DeformableWorldInstance inst {};
    inst.InstanceId        = NextInstanceId_++;
    inst.WorldOriginMeters = worldOriginMeters;

    if (doc.EmbedRestCageBinary && doc.EmbeddedRestBody.PointCount > 0u)
    {
        inst.Body          = std::move(doc.EmbeddedRestBody);
        inst.Configuration = doc.Configuration;
    }
    else
    {
        DeformablePresetRegistry::BuildPreset(doc.PresetKind, inst.Body, inst.Configuration);
        inst.Configuration = doc.Configuration;
    }

    TranslateBodyBy(inst.Body, worldOriginMeters);
    inst.InstanceName = instanceName.empty() ? doc.PresetId : std::move(instanceName);
    InitializeInstanceExecutor(inst);
    Instances_.push_back(std::move(inst));
    return Instances_.back().InstanceId;
}

bool DeformableWorldRegistry::RemoveInstance(uint32_t instanceId)
{
    const auto it = std::find_if(Instances_.begin(), Instances_.end(),
                                 [instanceId](const DeformableWorldInstance& inst) { return inst.InstanceId == instanceId; });
    if (it == Instances_.end())
    {
        return false;
    }
    Instances_.erase(it);
    return true;
}

void DeformableWorldRegistry::ClearAll() noexcept
{
    Instances_.clear();
}

DeformableWorldInstance* DeformableWorldRegistry::FindMutable(uint32_t instanceId) noexcept
{
    for (auto& inst : Instances_)
    {
        if (inst.InstanceId == instanceId) return &inst;
    }
    return nullptr;
}

const DeformableWorldInstance* DeformableWorldRegistry::Find(uint32_t instanceId) const noexcept
{
    for (const auto& inst : Instances_)
    {
        if (inst.InstanceId == instanceId) return &inst;
    }
    return nullptr;
}

DeformableWorldStepSummary DeformableWorldRegistry::StepWorld(
    float dtSeconds,
    float simTimeSeconds,
    const std::array<float, 3>& cameraPositionMeters,
    float windPulse,
    const DeformableExternalCoupling* sharedCoupling)
{
    DeformableWorldStepSummary summary {};
    summary.TotalInstances = static_cast<uint32_t>(Instances_.size());

    for (auto& inst : Instances_)
    {
        if (!inst.Enabled)
        {
            inst.ActivityTier = DeformableActivityTier::Asleep;
            ++summary.SleepingCount;
            continue;
        }

        const float dx = inst.WorldOriginMeters[0] - cameraPositionMeters[0];
        const float dy = inst.WorldOriginMeters[1] - cameraPositionMeters[1];
        const float dz = inst.WorldOriginMeters[2] - cameraPositionMeters[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        inst.LastCameraDistanceM = dist;

        if (dist > Policy_.SleepRadiusMeters)
        {
            inst.ActivityTier = DeformableActivityTier::Asleep;
            ++summary.SleepingCount;
            continue;
        }

        if (dist > Policy_.FullRateRadiusMeters)
        {
            inst.ActivityTier = DeformableActivityTier::HalfRate;
            ++summary.HalfRateCount;
            if ((WorldTickIndex_ & 1ull) == 0ull)
            {
                DeformableSimulationConfiguration reduced = inst.Configuration;
                reduced.Substeps = std::max(2u, reduced.Substeps / 2u);
                if (inst.GpuExecutor && inst.GpuExecutor->Ready())
                {
                    (void)inst.GpuExecutor->ExecuteTickInPlace(
                        inst.Body, dtSeconds * 2.0f, reduced, simTimeSeconds, windPulse, sharedCoupling);
                    if (inst.GpuExecutor->InspectTelemetry().RealGpuDispatched)
                    {
                        ++summary.GpuDispatchedCount;
                    }
                }
                else
                {
                    XpbdConstraintSolver::Step(inst.Body, dtSeconds * 2.0f, reduced, simTimeSeconds, windPulse, sharedCoupling);
                }
                ++inst.SteppedTickCount;
            }
            summary.TotalActivePoints += inst.Body.PointCount;
            summary.TotalActiveTets   += inst.Body.TetCount;
            continue;
        }

        inst.ActivityTier = DeformableActivityTier::FullRate;
        ++summary.FullRateCount;
        if (inst.GpuExecutor && inst.GpuExecutor->Ready())
        {
            (void)inst.GpuExecutor->ExecuteTickInPlace(
                inst.Body, dtSeconds, inst.Configuration, simTimeSeconds, windPulse, sharedCoupling);
            if (inst.GpuExecutor->InspectTelemetry().RealGpuDispatched)
            {
                ++summary.GpuDispatchedCount;
            }
        }
        else
        {
            XpbdConstraintSolver::Step(inst.Body, dtSeconds, inst.Configuration, simTimeSeconds, windPulse, sharedCoupling);
        }
        ++inst.SteppedTickCount;
        summary.TotalActivePoints += inst.Body.PointCount;
        summary.TotalActiveTets   += inst.Body.TetCount;
    }

    ++WorldTickIndex_;
    return summary;
}

} // namespace Frontier::SimulationSpace::Deformable
