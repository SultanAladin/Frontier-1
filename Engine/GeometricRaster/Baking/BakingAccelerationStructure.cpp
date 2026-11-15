/*============================================================================================================================================
                                                       BAKINGACCELERATIONSTRUCTURE.CPP
============================================================================================================================================*/
// 🧩 Dual GTX BVH4 + hardware RTX VK_KHR_acceleration_structure BLAS/TLAS builder and upload manager implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/BakingAccelerationStructure.h"

#include <cstring>
#include <vector>

namespace Frontier::GeometricRaster::Baking
{

namespace
{

template <typename RecordT>
[[nodiscard]] DeviceBufferResource UploadVectorToDeviceBuffer(
    VulkanInterchange&          Device,
    const std::vector<RecordT>& Items,
    VkBufferUsageFlags          UsageFlags)
{
    const uint64_t ByteLen = static_cast<uint64_t>(Items.size()) * sizeof(RecordT);
    if (ByteLen == 0ull)
    {
        return {};
    }
    DeviceBufferResource Buffer = Device.AllocateBuffer(
        ByteLen,
        UsageFlags,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (Buffer.HostAddress != nullptr)
    {
        std::memcpy(Buffer.HostAddress, Items.data(), static_cast<std::size_t>(ByteLen));
    }
    return Buffer;
}

[[nodiscard]] uint64_t AlignUp(uint64_t Value, uint64_t Alignment) noexcept
{
    const uint64_t Mask = (Alignment > 0ull) ? (Alignment - 1ull) : 0ull;
    return (Value + Mask) & ~Mask;
}

} // namespace

BakingAccelerationStructure::~BakingAccelerationStructure()
{
    ReleaseResources();
}

void BakingAccelerationStructure::ReleaseResources() noexcept
{
    if (DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        LowVertexCount      = 0u;
        HighWireVertexCount = 0u;
        HighTriangleCount   = 0u;
        WideEntryCount      = 0u;
        HardwareAccelReady  = false;
        return;
    }

    VkDevice DeviceHandle = DeviceOwner->AccessDevice();
    vkDeviceWaitIdle(DeviceHandle);

    if (TopLevelAccel != VK_NULL_HANDLE && vkDestroyAccelerationStructureKHR != nullptr)
    {
        vkDestroyAccelerationStructureKHR(DeviceHandle, TopLevelAccel, nullptr);
        TopLevelAccel = VK_NULL_HANDLE;
    }
    if (BottomLevelAccel != VK_NULL_HANDLE && vkDestroyAccelerationStructureKHR != nullptr)
    {
        vkDestroyAccelerationStructureKHR(DeviceHandle, BottomLevelAccel, nullptr);
        BottomLevelAccel = VK_NULL_HANDLE;
    }

    DeviceOwner->ReleaseBuffer(TopLevelStorageBuffer);
    DeviceOwner->ReleaseBuffer(BottomLevelStorageBuffer);
    DeviceOwner->ReleaseBuffer(HighPositionAddressBuffer);
    DeviceOwner->ReleaseBuffer(BvhPrimitiveBuffer);
    DeviceOwner->ReleaseBuffer(WideEntryBuffer);
    DeviceOwner->ReleaseBuffer(HighTriangleBuffer);
    DeviceOwner->ReleaseBuffer(HighWireVertexBuffer);
    DeviceOwner->ReleaseBuffer(LowVertexBuffer);

    LowVertexCount      = 0u;
    HighWireVertexCount = 0u;
    HighTriangleCount   = 0u;
    WideEntryCount      = 0u;
    HardwareAccelReady  = false;
}

bool BakingAccelerationStructure::UploadPreparedStudy(
    VulkanInterchange*       BoundDevice,
    const PreparedStudyPair& Study)
{
    ReleaseResources();
    DeviceOwner = BoundDevice;
    if (DeviceOwner == nullptr || !DeviceOwner->Ready() || Study.LowSurfaceVertices.empty() || Study.HighTriangles.empty())
    {
        return false;
    }

    SpatialAcceleration::WideBoundingTree Bvh4 {};
    if (!Bvh4.BuildFromTrianglePrimitives(Study.HighBvhPrimitives))
    {
        return false;
    }

    LowVertexBuffer      = UploadVectorToDeviceBuffer(*DeviceOwner, Study.LowSurfaceVertices, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    HighWireVertexBuffer = UploadVectorToDeviceBuffer(*DeviceOwner, Study.HighWireVertices, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    HighTriangleBuffer   = UploadVectorToDeviceBuffer(*DeviceOwner, Study.HighTriangles, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    WideEntryBuffer      = UploadVectorToDeviceBuffer(*DeviceOwner, Bvh4.WideEntries, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BvhPrimitiveBuffer   = UploadVectorToDeviceBuffer(*DeviceOwner, Bvh4.Primitives, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    if (LowVertexBuffer.BufferHandle == VK_NULL_HANDLE ||
        HighTriangleBuffer.BufferHandle == VK_NULL_HANDLE ||
        WideEntryBuffer.BufferHandle == VK_NULL_HANDLE ||
        BvhPrimitiveBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        ReleaseResources();
        return false;
    }

    LowVertexCount      = static_cast<uint32_t>(Study.LowSurfaceVertices.size());
    HighWireVertexCount = static_cast<uint32_t>(Study.HighWireVertices.size());
    HighTriangleCount   = static_cast<uint32_t>(Study.HighTriangles.size());
    WideEntryCount      = static_cast<uint32_t>(Bvh4.WideEntries.size());

    if (DeviceOwner->QueryHardwareRayQuerySupported())
    {
        HardwareAccelReady = BuildHardwareRayQueryStructures(Study);
    }

    return true;
}

bool BakingAccelerationStructure::BuildHardwareRayQueryStructures(const PreparedStudyPair& Study)
{
    if (DeviceOwner == nullptr || !DeviceOwner->QueryHardwareRayQuerySupported())
    {
        return false;
    }
    if (vkCreateAccelerationStructureKHR == nullptr ||
        vkGetAccelerationStructureBuildSizesKHR == nullptr ||
        vkCmdBuildAccelerationStructuresKHR == nullptr ||
        vkGetAccelerationStructureDeviceAddressKHR == nullptr)
    {
        return false;
    }

    VkDevice DeviceHandle = DeviceOwner->AccessDevice();
    const uint64_t ScratchAlign = std::max<uint64_t>(256ull, DeviceOwner->QueryAccelerationScratchAlignment());

    // 1. Upload High positions with SHADER_DEVICE_ADDRESS + ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY
    HighPositionAddressBuffer = DeviceOwner->AllocateBuffer(
        static_cast<uint64_t>(Study.HighPositionsFlat.size()) * sizeof(Mathematics::Axis3D),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (HighPositionAddressBuffer.HostAddress == nullptr)
    {
        return false;
    }
    std::memcpy(
        HighPositionAddressBuffer.HostAddress,
        Study.HighPositionsFlat.data(),
        Study.HighPositionsFlat.size() * sizeof(Mathematics::Axis3D));

    const uint64_t VertexAddr = DeviceOwner->QueryBufferDeviceAddress(HighPositionAddressBuffer.BufferHandle);
    if (VertexAddr == 0ull)
    {
        return false;
    }

    // 2. Describe Bottom-Level Acceleration Structure (BLAS) triangles
    VkAccelerationStructureGeometryTrianglesDataKHR TriSpec {};
    TriSpec.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    TriSpec.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    TriSpec.vertexData.deviceAddress = VertexAddr;
    TriSpec.vertexStride = sizeof(Mathematics::Axis3D);
    TriSpec.maxVertex    = static_cast<uint32_t>(Study.HighPositionsFlat.size());
    TriSpec.indexType    = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureGeometryKHR BlasGeom {};
    BlasGeom.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    BlasGeom.flags              = VK_GEOMETRY_OPAQUE_BIT_KHR;
    BlasGeom.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    BlasGeom.geometry.triangles = TriSpec;

    VkAccelerationStructureBuildGeometryInfoKHR BlasBuildSpec {};
    BlasBuildSpec.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    BlasBuildSpec.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    BlasBuildSpec.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    BlasBuildSpec.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    BlasBuildSpec.geometryCount = 1u;
    BlasBuildSpec.pGeometries   = &BlasGeom;

    const uint32_t PrimitiveCount = Study.HighTriangleCount;
    VkAccelerationStructureBuildSizesInfoKHR BlasSizes {};
    BlasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        DeviceHandle,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &BlasBuildSpec,
        &PrimitiveCount,
        &BlasSizes);

    BottomLevelStorageBuffer = DeviceOwner->AllocateBuffer(
        BlasSizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (BottomLevelStorageBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    VkAccelerationStructureCreateInfoKHR BlasCreateSpec {};
    BlasCreateSpec.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    BlasCreateSpec.buffer = BottomLevelStorageBuffer.BufferHandle;
    BlasCreateSpec.size   = BlasSizes.accelerationStructureSize;
    BlasCreateSpec.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (vkCreateAccelerationStructureKHR(DeviceHandle, &BlasCreateSpec, nullptr, &BottomLevelAccel) != VK_SUCCESS)
    {
        return false;
    }

    DeviceBufferResource BlasScratch = DeviceOwner->AllocateBuffer(
        BlasSizes.buildScratchSize + ScratchAlign,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const uint64_t BlasScratchAddr = AlignUp(DeviceOwner->QueryBufferDeviceAddress(BlasScratch.BufferHandle), ScratchAlign);
    if (BlasScratchAddr == 0ull)
    {
        DeviceOwner->ReleaseBuffer(BlasScratch);
        return false;
    }

    BlasBuildSpec.dstAccelerationStructure  = BottomLevelAccel;
    BlasBuildSpec.scratchData.deviceAddress = BlasScratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR BlasRange {};
    BlasRange.primitiveCount = PrimitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* BlasRangePtr = &BlasRange;

    VkCommandBuffer BlasCmd = DeviceOwner->BeginSingleTimeCommands();
    vkCmdBuildAccelerationStructuresKHR(BlasCmd, 1u, &BlasBuildSpec, &BlasRangePtr);
    const uint64_t BlasTick = DeviceOwner->DispatchCommandBufferAsync(BlasCmd);
    (void)DeviceOwner->AwaitTimelineTick(BlasTick);
    DeviceOwner->FreeCommandBuffer(BlasCmd);
    DeviceOwner->ReleaseBuffer(BlasScratch);

    // 3. Query BLAS device address & build single-instance Top-Level Acceleration Structure (TLAS)
    VkAccelerationStructureDeviceAddressInfoKHR BlasAddrQuery {};
    BlasAddrQuery.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    BlasAddrQuery.accelerationStructure = BottomLevelAccel;
    const uint64_t BlasAddress          = vkGetAccelerationStructureDeviceAddressKHR(DeviceHandle, &BlasAddrQuery);

    VkAccelerationStructureInstanceKHR InstanceEntry {};
    InstanceEntry.transform.matrix[0][0] = 1.0f;
    InstanceEntry.transform.matrix[1][1] = 1.0f;
    InstanceEntry.transform.matrix[2][2] = 1.0f;
    InstanceEntry.mask                               = 0xFFu;
    InstanceEntry.flags                              = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    InstanceEntry.accelerationStructureReference     = BlasAddress;

    DeviceBufferResource InstanceBuffer = DeviceOwner->AllocateBuffer(
        sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (InstanceBuffer.HostAddress == nullptr)
    {
        return false;
    }
    std::memcpy(InstanceBuffer.HostAddress, &InstanceEntry, sizeof(VkAccelerationStructureInstanceKHR));
    const uint64_t InstAddr = DeviceOwner->QueryBufferDeviceAddress(InstanceBuffer.BufferHandle);

    VkAccelerationStructureGeometryInstancesDataKHR InstGeom {};
    InstGeom.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    InstGeom.data.deviceAddress = InstAddr;

    VkAccelerationStructureGeometryKHR TlasGeom {};
    TlasGeom.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    TlasGeom.geometryType       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    TlasGeom.geometry.instances = InstGeom;

    VkAccelerationStructureBuildGeometryInfoKHR TlasBuildSpec {};
    TlasBuildSpec.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    TlasBuildSpec.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    TlasBuildSpec.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    TlasBuildSpec.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    TlasBuildSpec.geometryCount = 1u;
    TlasBuildSpec.pGeometries   = &TlasGeom;

    const uint32_t InstanceCount = 1u;
    VkAccelerationStructureBuildSizesInfoKHR TlasSizes {};
    TlasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        DeviceHandle,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &TlasBuildSpec,
        &InstanceCount,
        &TlasSizes);

    TopLevelStorageBuffer = DeviceOwner->AllocateBuffer(
        TlasSizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (TopLevelStorageBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        DeviceOwner->ReleaseBuffer(InstanceBuffer);
        return false;
    }

    VkAccelerationStructureCreateInfoKHR TlasCreateSpec {};
    TlasCreateSpec.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    TlasCreateSpec.buffer = TopLevelStorageBuffer.BufferHandle;
    TlasCreateSpec.size   = TlasSizes.accelerationStructureSize;
    TlasCreateSpec.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (vkCreateAccelerationStructureKHR(DeviceHandle, &TlasCreateSpec, nullptr, &TopLevelAccel) != VK_SUCCESS)
    {
        DeviceOwner->ReleaseBuffer(InstanceBuffer);
        return false;
    }

    DeviceBufferResource TlasScratch = DeviceOwner->AllocateBuffer(
        TlasSizes.buildScratchSize + ScratchAlign,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const uint64_t TlasScratchAddr = AlignUp(DeviceOwner->QueryBufferDeviceAddress(TlasScratch.BufferHandle), ScratchAlign);
    if (TlasScratchAddr == 0ull)
    {
        DeviceOwner->ReleaseBuffer(TlasScratch);
        DeviceOwner->ReleaseBuffer(InstanceBuffer);
        return false;
    }

    TlasBuildSpec.dstAccelerationStructure  = TopLevelAccel;
    TlasBuildSpec.scratchData.deviceAddress = TlasScratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR TlasRange {};
    TlasRange.primitiveCount = 1u;
    const VkAccelerationStructureBuildRangeInfoKHR* TlasRangePtr = &TlasRange;

    VkCommandBuffer TlasCmd = DeviceOwner->BeginSingleTimeCommands();
    vkCmdBuildAccelerationStructuresKHR(TlasCmd, 1u, &TlasBuildSpec, &TlasRangePtr);
    const uint64_t TlasTick = DeviceOwner->DispatchCommandBufferAsync(TlasCmd);
    (void)DeviceOwner->AwaitTimelineTick(TlasTick);
    DeviceOwner->FreeCommandBuffer(TlasCmd);

    DeviceOwner->ReleaseBuffer(TlasScratch);
    DeviceOwner->ReleaseBuffer(InstanceBuffer);
    return true;
}

} // namespace Frontier::GeometricRaster::Baking
