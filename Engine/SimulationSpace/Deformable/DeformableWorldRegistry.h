/*============================================================================================================================================
                                                    DEFORMABLEWORLDREGISTRY.H
============================================================================================================================================*/
// 🧩 Phase D15 Multi-instance world deformable registry with world origins, distance sleep/LOD culling & shared ForceField/Jolt coupling
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D15)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformablePhysicsCoupling.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"
#include "Engine/SimulationSpace/Deformable/DeformableVulkanExecutor.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                WORLD INSTANCE RECORDS
//------------------------------------------------------------------------------------------------------------------------

struct DeformableWorldCullingPolicy
{
    float FullRateRadiusMeters = 30.0f; // [m] - Full substep rate within 30 m of camera
    float HalfRateRadiusMeters = 65.0f; // [m] - Reduced substep budget between 30..65 m
    float SleepRadiusMeters    = 95.0f; // [m] - Beyond 95 m -> asleep (0 CPU cost) until camera approaches
};

enum class DeformableActivityTier : uint8_t
{
    FullRate = 0u,
    HalfRate = 1u,
    Asleep   = 2u
};

struct DeformableWorldInstance
{
    uint32_t                                  InstanceId          = 0u;
    std::string                               InstanceName        {};
    std::array<float, 3>                      WorldOriginMeters   { 0.0f, 0.0f, 0.0f };
    DeformableBody                            Body                {};
    DeformableSimulationConfiguration         Configuration       {};
    std::unique_ptr<DeformableVulkanExecutor> GpuExecutor         {};
    DeformableActivityTier                    ActivityTier        = DeformableActivityTier::FullRate;
    uint64_t                                  SteppedTickCount    = 0ull;
    float                                     LastCameraDistanceM = 0.0f;
    bool                                      Enabled             = true;
};

struct DeformableWorldStepSummary
{
    uint32_t TotalInstances     = 0u;
    uint32_t FullRateCount      = 0u;
    uint32_t HalfRateCount      = 0u;
    uint32_t SleepingCount      = 0u;
    uint32_t TotalActivePoints  = 0u;
    uint32_t TotalActiveTets    = 0u;
    uint32_t GpuDispatchedCount = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                DEFORMABLE WORLD REGISTRY
//------------------------------------------------------------------------------------------------------------------------

class DeformableWorldRegistry
{
public:
    void SetCullingPolicy(const DeformableWorldCullingPolicy& policy) noexcept { Policy_ = policy; }
    [[nodiscard]] const DeformableWorldCullingPolicy& InspectCullingPolicy() const noexcept { return Policy_; }

#if FRONTIER_DEFORMABLE_HAS_VULKAN
    /// Attaches a live Vulkan 1.2 interchange to all existing and future world deformable executors.
    [[nodiscard]] bool AttachVulkanInterchange(
        GeometricRaster::VulkanInterchange* interchange,
        const std::filesystem::path& compiledShaderDirectory = "CompiledShaders") noexcept;
#endif

    /// Spawns a new deformable instance from one of the 9 built-in DeformablePresetKind presets at `worldOriginMeters`.
    [[nodiscard]] uint32_t SpawnPreset(
        DeformablePresetKind kind,
        const std::array<float, 3>& worldOriginMeters = { 0.0f, 0.0f, 0.0f },
        std::string instanceName = {});

    /// Spawns a new deformable instance directly from a `.SoftBody` preset file on disk.
    [[nodiscard]] uint32_t SpawnFromSoftBodyFile(
        const std::filesystem::path& softBodyPath,
        const std::array<float, 3>& worldOriginMeters = { 0.0f, 0.0f, 0.0f },
        std::string instanceName = {},
        std::string* outError = nullptr);

    /// Removes an instance by its InstanceId.
    bool RemoveInstance(uint32_t instanceId);

    void ClearAll() noexcept;

    [[nodiscard]] DeformableWorldInstance* FindMutable(uint32_t instanceId) noexcept;
    [[nodiscard]] const DeformableWorldInstance* Find(uint32_t instanceId) const noexcept;
    [[nodiscard]] std::span<const DeformableWorldInstance> EnumerateInstances() const noexcept { return Instances_; }

    /// Steps all enabled deformable instances with distance-based Full/Half/Sleep LOD culling and shared ForceField/Jolt coupling.
    [[nodiscard]] DeformableWorldStepSummary StepWorld(
        float dtSeconds,
        float simTimeSeconds,
        const std::array<float, 3>& cameraPositionMeters,
        float windPulse = 0.0f,
        const DeformableExternalCoupling* sharedCoupling = nullptr);

    [[nodiscard]] uint64_t QueryWorldTickIndex() const noexcept { return WorldTickIndex_; }
    void                   RestoreWorldTickIndex(uint64_t tickIndex) noexcept { WorldTickIndex_ = tickIndex; }

private:
    static void TranslateBodyBy(DeformableBody& body, const std::array<float, 3>& offsetMeters) noexcept;
    void        InitializeInstanceExecutor(DeformableWorldInstance& inst);

    DeformableWorldCullingPolicy         Policy_          {};
#if FRONTIER_DEFORMABLE_HAS_VULKAN
    GeometricRaster::VulkanInterchange*  Interchange_             = nullptr;
    std::filesystem::path                CompiledShaderDirectory_ = "CompiledShaders";
#endif
    std::vector<DeformableWorldInstance> Instances_       {};
    uint32_t                             NextInstanceId_  = 1u;
    uint64_t                             WorldTickIndex_  = 0ull;
};

} // namespace Frontier::SimulationSpace::Deformable
