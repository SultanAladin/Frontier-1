/*============================================================================================================================================
                                                         FIELDLATTICEVULKANEXECUTOR.H
============================================================================================================================================*/
// 🧩 Runtime Vulkan 1.3 Compute executor dispatching FieldLatticeCompute.spv (hardware ICD or headless SwiftShader fallback)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class FieldLatticeVulkanExecutor
{
public:
    FieldLatticeVulkanExecutor() noexcept;
    ~FieldLatticeVulkanExecutor() noexcept;

    FieldLatticeVulkanExecutor(const FieldLatticeVulkanExecutor&)            = delete;
    FieldLatticeVulkanExecutor& operator=(const FieldLatticeVulkanExecutor&) = delete;

    [[nodiscard]] bool Initialize(
        const std::filesystem::path& ComputeSpirvPath,
        const std::filesystem::path& SwiftShaderIcdDir,
        std::string&                 OutDiagnostic) noexcept;

    void Finalize() noexcept;

    [[nodiscard]] bool QueryDeviceReady() const noexcept;
    [[nodiscard]] std::string QueryDeviceName() const;
    [[nodiscard]] void* QueryNativeInstance() const noexcept;
    [[nodiscard]] void* QueryNativePhysicalDevice() const noexcept;
    [[nodiscard]] void* QueryNativeDevice() const noexcept;
    [[nodiscard]] void* QueryNativeQueue() const noexcept;
    [[nodiscard]] uint32_t QueryQueueFamilyIndex() const noexcept;
    [[nodiscard]] bool QueryUsesKhrDynamicRendering() const noexcept;
    [[nodiscard]] bool QueryUsesKhrSynchronization2() const noexcept;

    [[nodiscard]] bool DispatchLatticeTier(
        const FieldLatticeTierDescriptor&      TierDescriptor,
        std::span<const ForceSourceShaderEntry> PackedSources,
        std::span<const SpatialVector3>         PackedWaypoints,
        double                                  TimeSeconds,
        std::vector<FieldLatticeVoxelSample>&   OutVoxels,
        std::string&                            OutDiagnostic) noexcept;

private:
    struct DeviceImplementation;
    std::unique_ptr<DeviceImplementation> DeviceHandle{};
};

} // namespace Frontier::SimulationSpace::ForceFields
