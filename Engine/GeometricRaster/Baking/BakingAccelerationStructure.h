/*============================================================================================================================================
                                                        BAKINGACCELERATIONSTRUCTURE.H
============================================================================================================================================*/
// 🧩 Dual GTX SAH-BVH4 + optional RTX VK_KHR_acceleration_structure (BLAS + TLAS) upload & synchronization for GPU baking

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/StudyGeometryBuilder.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>

namespace Frontier::GeometricRaster::Baking
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class BakeAccelerationEngine : uint8_t
{
    GtxComputeBvh4      = 0u,
    RtxHardwareRayQuery = 1u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ACCELERATION STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

class BakingAccelerationStructure
{
public:
    BakingAccelerationStructure() = default;
    ~BakingAccelerationStructure();

    BakingAccelerationStructure(const BakingAccelerationStructure&)            = delete;
    BakingAccelerationStructure& operator=(const BakingAccelerationStructure&) = delete;

    [[nodiscard]] bool UploadPreparedStudy(
        VulkanInterchange*       BoundDevice,
        const PreparedStudyPair& Study);

    void ReleaseResources() noexcept;

    [[nodiscard]] bool Ready() const noexcept { return HighTriangleCount > 0u && WideEntryCount > 0u; }
    [[nodiscard]] bool QueryHardwareAccelReady() const noexcept { return HardwareAccelReady; }
    [[nodiscard]] BakeAccelerationEngine QueryActiveBackend(bool PreferComputeBvh4 = false) const noexcept
    {
        return (!PreferComputeBvh4 && HardwareAccelReady)
            ? BakeAccelerationEngine::RtxHardwareRayQuery
            : BakeAccelerationEngine::GtxComputeBvh4;
    }

    [[nodiscard]] uint32_t QueryLowVertexCount() const noexcept   { return LowVertexCount; }
    [[nodiscard]] uint32_t QueryHighWireVertexCount() const noexcept { return HighWireVertexCount; }
    [[nodiscard]] uint32_t QueryHighTriangleCount() const noexcept { return HighTriangleCount; }
    [[nodiscard]] uint32_t QueryWideEntryCount() const noexcept    { return WideEntryCount; }

    [[nodiscard]] VkBuffer AccessLowVertexBuffer() const noexcept    { return LowVertexBuffer.BufferHandle; }
    [[nodiscard]] VkBuffer AccessHighWireVertexBuffer() const noexcept { return HighWireVertexBuffer.BufferHandle; }
    [[nodiscard]] VkBuffer AccessHighTriangleBuffer() const noexcept { return HighTriangleBuffer.BufferHandle; }
    [[nodiscard]] VkBuffer AccessWideEntryBuffer() const noexcept    { return WideEntryBuffer.BufferHandle; }
    [[nodiscard]] VkBuffer AccessBvhPrimitiveBuffer() const noexcept { return BvhPrimitiveBuffer.BufferHandle; }
    [[nodiscard]] VkAccelerationStructureKHR AccessTopLevelAccel() const noexcept { return TopLevelAccel; }

private:
    [[nodiscard]] bool BuildHardwareRayQueryStructures(const PreparedStudyPair& Study);

    VulkanInterchange*       DeviceOwner          = nullptr;
    DeviceBufferResource     LowVertexBuffer      {};
    DeviceBufferResource     HighWireVertexBuffer {};
    DeviceBufferResource     HighTriangleBuffer   {};
    DeviceBufferResource     WideEntryBuffer      {};
    DeviceBufferResource     BvhPrimitiveBuffer   {};

    // Optional RTX hardware ray-query resources
    DeviceBufferResource       HighPositionAddressBuffer {};
    DeviceBufferResource       BottomLevelStorageBuffer  {};
    DeviceBufferResource       TopLevelStorageBuffer     {};
    VkAccelerationStructureKHR BottomLevelAccel          = VK_NULL_HANDLE;
    VkAccelerationStructureKHR TopLevelAccel             = VK_NULL_HANDLE;

    uint32_t LowVertexCount      = 0u;
    uint32_t HighWireVertexCount = 0u;
    uint32_t HighTriangleCount   = 0u;
    uint32_t WideEntryCount      = 0u;
    bool     HardwareAccelReady  = false;
};

} // namespace Frontier::GeometricRaster::Baking
