/*============================================================================================================================================
                                                           VIEWPORTPICKQUERY.CPP
============================================================================================================================================*/
// 🧩 Implementation of ViewportPickQuery executing 1x1 GPU pixel readbacks and parallel ViewportPickReduction.slang region scans

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Picking/ViewportPickQuery.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

struct alignas(16) GPUPickReductionParams
{
    uint32_t ViewportWidth  = 0u;
    uint32_t ViewportHeight = 0u;
    uint32_t RegionMinX     = 0u;
    uint32_t RegionMinY     = 0u;
    uint32_t RegionMaxX     = 0u;
    uint32_t RegionMaxY     = 0u;
    uint32_t BrushCenterX   = 0u;
    uint32_t BrushCenterY   = 0u;
    uint32_t BrushRadiusSq  = 0u;
    uint32_t MaxFaceCount   = 0u;
    uint32_t Reserved0      = 0u;
    uint32_t Reserved1      = 0u;
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ViewportPickQuery::~ViewportPickQuery()
{
    TeardownPickQuery();
}

bool ViewportPickQuery::InitializePickQuery(
    VulkanInterchange*         BoundDevice,
    ComputeDispatchEncoder*      BoundComputeEncoder,
    TimelineTransferRegister*           BoundTransferRegister,
    const std::filesystem::path& PickReductionSpirvPath)
{
    TeardownPickQuery();
    if (BoundDevice == nullptr || BoundComputeEncoder == nullptr || BoundTransferRegister == nullptr)
    {
        return false;
    }

    DeviceInterchange        = BoundDevice;
    ComputeEncoder = BoundComputeEncoder;
    TransferRegister   = BoundTransferRegister;

    const std::array<VkDescriptorType, 4u> SlotCategories {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    ReductionProgramHandle = ComputeEncoder->RegisterProgram(PickReductionSpirvPath, SlotCategories);
    if (ReductionProgramHandle == UINT32_MAX)
    {
        TeardownPickQuery();
        return false;
    }

    ParamsUniformBuffer  = ComputeEncoder->AllocateHostWritableBuffer(sizeof(GPUPickReductionParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    SummaryStorageBuffer = ComputeEncoder->AllocateHostWritableBuffer(16u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    return ParamsUniformBuffer.BufferHandle != VK_NULL_HANDLE && SummaryStorageBuffer.BufferHandle != VK_NULL_HANDLE;
}

void ViewportPickQuery::TeardownPickQuery() noexcept
{
    if (DeviceInterchange != nullptr && DeviceInterchange->Ready())
    {
        DeviceInterchange->ReleaseBuffer(ParamsUniformBuffer);
        DeviceInterchange->ReleaseBuffer(FlattenedPickBuffer);
        DeviceInterchange->ReleaseBuffer(SummaryStorageBuffer);
    }
    DeviceInterchange                = nullptr;
    ComputeEncoder         = nullptr;
    TransferRegister           = nullptr;
    ReductionProgramHandle = UINT32_MAX;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           1x1 CURSOR PICK & REGION REDUCTION
//------------------------------------------------------------------------------------------------------------------------

ViewportPickHit ViewportPickQuery::QuerySinglePixel(
    VkImage       PickAttachmentImage,
    VkImageLayout CurrentLayout,
    uint32_t      ImageWidthPx,
    uint32_t      ImageHeightPx,
    int32_t       CursorX,
    int32_t       CursorY)
{
    ViewportPickHit Result {};
    if (DeviceInterchange == nullptr || TransferRegister == nullptr || ImageWidthPx == 0u || ImageHeightPx == 0u)
    {
        return Result;
    }

    const int32_t ClampedX = std::clamp(CursorX, 0, static_cast<int32_t>(ImageWidthPx) - 1);
    const int32_t ClampedY = std::clamp(CursorY, 0, static_cast<int32_t>(ImageHeightPx) - 1);

    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();
    ReadbackTicket Ticket = TransferRegister->EncodeImageRegionReadback(
        Cmd,
        PickAttachmentImage,
        CurrentLayout,
        ClampedX,
        ClampedY,
        1u,
        1u,
        16u); // R32G32B32A32_UINT = 16 bytes/pixel

    const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    TransferRegister->BindTicketTimelineSignal(Ticket, Signal);

    std::array<uint32_t, 4u> RawChannels {{ 0u, GeometrySpace::InvalidNetworkIndex, GeometrySpace::InvalidNetworkIndex, 0u }};
    if (TransferRegister->AwaitAndCopyTyped<uint32_t>(Ticket, RawChannels))
    {
        Result.InstanceSlot       = RawChannels[0u];
        Result.FaceIndex          = RawChannels[1u];
        Result.DirectedEdgeIndex  = RawChannels[2u];
        Result.GizmoHandleOrdinal = RawChannels[3u];
    }

    vkFreeCommandBuffers(DeviceInterchange->AccessDevice(), DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
    return Result;
}

RegionPickSummary ViewportPickQuery::ExecuteRegionReduction(
    VkImage       PickAttachmentImage,
    VkImageLayout CurrentLayout,
    uint32_t      ImageWidthPx,
    uint32_t      ImageHeightPx,
    uint32_t      RegionMinX,
    uint32_t      RegionMinY,
    uint32_t      RegionMaxX,
    uint32_t      RegionMaxY,
    uint32_t      BrushCenterX,
    uint32_t      BrushCenterY,
    uint32_t      BrushRadiusPx,
    VkBuffer      TargetSelectionBitmaskBuffer,
    uint32_t      MaxFaceCount)
{
    RegionPickSummary Summary {};
    if (DeviceInterchange == nullptr || ComputeEncoder == nullptr || TransferRegister == nullptr ||
        ImageWidthPx == 0u || ImageHeightPx == 0u)
    {
        return Summary;
    }

    const uint64_t TotalPickBytes = static_cast<uint64_t>(ImageWidthPx) * static_cast<uint64_t>(ImageHeightPx) * 16ull;
    EnsureFlattenedBufferCapacity(TotalPickBytes);
    if (FlattenedPickBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return Summary;
    }

    const uint32_t ClampedMinX = std::min(RegionMinX, ImageWidthPx - 1u);
    const uint32_t ClampedMinY = std::min(RegionMinY, ImageHeightPx - 1u);
    const uint32_t ClampedMaxX = std::clamp(RegionMaxX, ClampedMinX, ImageWidthPx - 1u);
    const uint32_t ClampedMaxY = std::clamp(RegionMaxY, ClampedMinY, ImageHeightPx - 1u);

    GPUPickReductionParams Params {};
    Params.ViewportWidth  = ImageWidthPx;
    Params.ViewportHeight = ImageHeightPx;
    Params.RegionMinX     = ClampedMinX;
    Params.RegionMinY     = ClampedMinY;
    Params.RegionMaxX     = ClampedMaxX;
    Params.RegionMaxY     = ClampedMaxY;
    Params.BrushCenterX   = BrushCenterX;
    Params.BrushCenterY   = BrushCenterY;
    Params.BrushRadiusSq  = BrushRadiusPx * BrushRadiusPx;
    Params.MaxFaceCount   = MaxFaceCount;

    const std::array<GPUPickReductionParams, 1u> ParamsSpan {{ Params }};
    ComputeEncoder->UploadSpanToBuffer<GPUPickReductionParams>(ParamsUniformBuffer, ParamsSpan);

    const std::array<uint32_t, 4u> ZeroSummary {{ 0u, 0u, 0u, 0u }};
    ComputeEncoder->UploadSpanToBuffer<uint32_t>(SummaryStorageBuffer, ZeroSummary);

    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();

    VkImageMemoryBarrier ImgToTransfer {};
    ImgToTransfer.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ImgToTransfer.oldLayout                       = CurrentLayout;
    ImgToTransfer.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ImgToTransfer.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ImgToTransfer.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ImgToTransfer.image                           = PickAttachmentImage;
    ImgToTransfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ImgToTransfer.subresourceRange.levelCount     = 1u;
    ImgToTransfer.subresourceRange.layerCount     = 1u;
    ImgToTransfer.srcAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ImgToTransfer.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u,
        nullptr,
        0u,
        nullptr,
        1u,
        &ImgToTransfer);

    VkBufferImageCopy FullCopy {};
    FullCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    FullCopy.imageSubresource.layerCount = 1u;
    FullCopy.imageExtent                 = { ImageWidthPx, ImageHeightPx, 1u };
    vkCmdCopyImageToBuffer(Cmd, PickAttachmentImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, FlattenedPickBuffer.BufferHandle, 1u, &FullCopy);

    VkMemoryBarrier TransferToCompute {};
    TransferToCompute.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    TransferToCompute.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    TransferToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &TransferToCompute, 0u, nullptr, 0u, nullptr);

    const uint32_t SpanW  = (ClampedMaxX - ClampedMinX) + 1u;
    const uint32_t SpanH  = (ClampedMaxY - ClampedMinY) + 1u;
    const uint32_t GroupX = (SpanW + 7u) / 8u;
    const uint32_t GroupY = (SpanH + 7u) / 8u;

    const std::array<ComputeSlotDescriptor, 4u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ParamsUniformBuffer.BufferHandle, sizeof(GPUPickReductionParams) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FlattenedPickBuffer.BufferHandle, TotalPickBytes },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, TargetSelectionBitmaskBuffer, VK_WHOLE_SIZE },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SummaryStorageBuffer.BufferHandle, 16u },
    }};
    (void)ComputeEncoder->EncodeComputeDispatch(Cmd, ReductionProgramHandle, Slots, GroupX, GroupY, 1u);

    ReadbackTicket Ticket = TransferRegister->EncodeBufferReadback(Cmd, SummaryStorageBuffer.BufferHandle, 0ull, 12ull);
    const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    TransferRegister->BindTicketTimelineSignal(Ticket, Signal);

    std::array<uint32_t, 3u> OutCounts {{ 0u, 0u, 0u }};
    if (TransferRegister->AwaitAndCopyTyped<uint32_t>(Ticket, OutCounts))
    {
        Summary.HitPixelCount     = OutCounts[0u];
        Summary.FirstInstanceSlot = OutCounts[1u];
        Summary.FirstGizmoOrdinal = OutCounts[2u];
    }

    vkFreeCommandBuffers(DeviceInterchange->AccessDevice(), DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
    return Summary;
}

void ViewportPickQuery::EnsureFlattenedBufferCapacity(uint64_t RequiredBytes)
{
    if (FlattenedPickBuffer.ByteCapacity >= RequiredBytes)
    {
        return;
    }
    if (DeviceInterchange != nullptr)
    {
        DeviceInterchange->ReleaseBuffer(FlattenedPickBuffer);
        FlattenedPickBuffer = DeviceInterchange->AllocateBuffer(
            RequiredBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
}

} // namespace Frontier::GeometricRaster
