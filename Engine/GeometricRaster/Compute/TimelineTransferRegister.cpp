/*============================================================================================================================================
                                                           TIMELINETRANSFERREGISTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of TimelineTransferRegister encoding zero-stall GPU buffer/image transfers into 64B-aligned host-cached staging slots

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/TimelineTransferRegister.h"

#include <cstring>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

TimelineTransferRegister::~TimelineTransferRegister()
{
    TeardownRegister();
}

bool TimelineTransferRegister::InitializeRegister(
    VulkanInterchange* BoundDevice,
    uint64_t             SlotCapacityBytes)
{
    TeardownRegister();
    if (BoundDevice == nullptr || !BoundDevice->Ready() || SlotCapacityBytes == 0ull)
    {
        return false;
    }

    DeviceInterchange = BoundDevice;
    for (TransferSlot& Slot : Slots)
    {
        Slot.Buffer = DeviceInterchange->AllocateBuffer(
            SlotCapacityBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        Slot.WriteCursor = 0ull;
        Slot.LastSignal  = 0ull;

        if (Slot.Buffer.BufferHandle == VK_NULL_HANDLE || Slot.Buffer.HostAddress == nullptr)
        {
            TeardownRegister();
            return false;
        }
    }

    ActiveSlot = 0u;
    return true;
}

void TimelineTransferRegister::TeardownRegister() noexcept
{
    if (DeviceInterchange != nullptr)
    {
        for (TransferSlot& Slot : Slots)
        {
            DeviceInterchange->ReleaseBuffer(Slot.Buffer);
            Slot.WriteCursor = 0ull;
            Slot.LastSignal  = 0ull;
        }
    }
    DeviceInterchange = nullptr;
}

void TimelineTransferRegister::AdvanceRingSlot() noexcept
{
    ActiveSlot = (ActiveSlot + 1u) % RingSlotCount;
    TransferSlot& Next = Slots[ActiveSlot];
    if (DeviceInterchange != nullptr && Next.LastSignal != 0ull)
    {
        (void)DeviceInterchange->AwaitTimelineTick(Next.LastSignal);
    }
    Next.WriteCursor = 0ull;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                TRANSFER ENCODING & READBACK
//------------------------------------------------------------------------------------------------------------------------

ReadbackTicket TimelineTransferRegister::EncodeBufferReadback(
    VkCommandBuffer SourceCmdBuffer,
    VkBuffer        SourceDeviceBuffer,
    uint64_t        SourceOffsetBytes,
    uint64_t        CopyByteCount)
{
    ReadbackTicket Ticket {};
    if (DeviceInterchange == nullptr || CopyByteCount == 0ull)
    {
        return Ticket;
    }

    TransferSlot& Slot = Slots[ActiveSlot];
    const uint64_t AlignedOffset = (Slot.WriteCursor + 63ull) & ~63ull;
    if (AlignedOffset + CopyByteCount > Slot.Buffer.ByteCapacity)
    {
        AdvanceRingSlot();
    }

    TransferSlot& TargetSlot    = Slots[ActiveSlot];
    const uint64_t DestOffset  = (TargetSlot.WriteCursor + 63ull) & ~63ull;
    if (DestOffset + CopyByteCount > TargetSlot.Buffer.ByteCapacity)
    {
        return Ticket;
    }

    VkMemoryBarrier ComputeToTransfer {};
    ComputeToTransfer.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeToTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ComputeToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        SourceCmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        1u,
        &ComputeToTransfer,
        0u,
        nullptr,
        0u,
        nullptr);

    VkBufferCopy CopyRegion {};
    CopyRegion.srcOffset = SourceOffsetBytes;
    CopyRegion.dstOffset = DestOffset;
    CopyRegion.size      = CopyByteCount;
    vkCmdCopyBuffer(SourceCmdBuffer, SourceDeviceBuffer, TargetSlot.Buffer.BufferHandle, 1u, &CopyRegion);

    VkMemoryBarrier HostBarrier {};
    HostBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    HostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    HostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        SourceCmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        1u,
        &HostBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    TargetSlot.WriteCursor = DestOffset + CopyByteCount;
    Ticket.SlotIndex       = ActiveSlot;
    Ticket.ByteOffset      = DestOffset;
    Ticket.ByteLength      = CopyByteCount;
    return Ticket;
}

ReadbackTicket TimelineTransferRegister::EncodeImageRegionReadback(
    VkCommandBuffer SourceCmdBuffer,
    VkImage         SourceImage,
    VkImageLayout   CurrentImageLayout,
    int32_t         OffsetX,
    int32_t         OffsetY,
    uint32_t        ExtentWidth,
    uint32_t        ExtentHeight,
    uint32_t        BytesPerPixel)
{
    ReadbackTicket Ticket {};
    const uint64_t TotalBytes = static_cast<uint64_t>(ExtentWidth) * static_cast<uint64_t>(ExtentHeight) * BytesPerPixel;
    if (DeviceInterchange == nullptr || TotalBytes == 0ull)
    {
        return Ticket;
    }

    TransferSlot& Slot = Slots[ActiveSlot];
    if (((Slot.WriteCursor + 63ull) & ~63ull) + TotalBytes > Slot.Buffer.ByteCapacity)
    {
        AdvanceRingSlot();
    }

    TransferSlot&   TargetSlot = Slots[ActiveSlot];
    const uint64_t DestOffset = (TargetSlot.WriteCursor + 63ull) & ~63ull;
    if (DestOffset + TotalBytes > TargetSlot.Buffer.ByteCapacity)
    {
        return Ticket;
    }

    VkImageMemoryBarrier ToTransfer {};
    ToTransfer.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToTransfer.oldLayout                       = CurrentImageLayout;
    ToTransfer.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ToTransfer.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.image                           = SourceImage;
    ToTransfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ToTransfer.subresourceRange.baseMipLevel   = 0u;
    ToTransfer.subresourceRange.levelCount     = 1u;
    ToTransfer.subresourceRange.baseArrayLayer = 0u;
    ToTransfer.subresourceRange.layerCount     = 1u;
    ToTransfer.srcAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    ToTransfer.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        SourceCmdBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u,
        nullptr,
        0u,
        nullptr,
        1u,
        &ToTransfer);

    VkBufferImageCopy CopyRegion {};
    CopyRegion.bufferOffset                    = DestOffset;
    CopyRegion.bufferRowLength                 = 0u;
    CopyRegion.bufferImageHeight               = 0u;
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = 0u;
    CopyRegion.imageSubresource.layerCount     = 1u;
    CopyRegion.imageOffset                     = { OffsetX, OffsetY, 0 };
    CopyRegion.imageExtent                     = { ExtentWidth, ExtentHeight, 1u };

    vkCmdCopyImageToBuffer(
        SourceCmdBuffer,
        SourceImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        TargetSlot.Buffer.BufferHandle,
        1u,
        &CopyRegion);

    VkMemoryBarrier HostBarrier {};
    HostBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    HostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    HostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        SourceCmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        1u,
        &HostBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    TargetSlot.WriteCursor = DestOffset + TotalBytes;
    Ticket.SlotIndex       = ActiveSlot;
    Ticket.ByteOffset      = DestOffset;
    Ticket.ByteLength      = TotalBytes;
    return Ticket;
}

void TimelineTransferRegister::BindTicketTimelineSignal(ReadbackTicket& Ticket, uint64_t TimelineSignal) noexcept
{
    Ticket.TimelineSignal = TimelineSignal;
    if (Ticket.SlotIndex < RingSlotCount)
    {
        Slots[Ticket.SlotIndex].LastSignal = TimelineSignal;
    }
}

bool TimelineTransferRegister::QueryTicketReady(const ReadbackTicket& Ticket) const noexcept
{
    return DeviceInterchange != nullptr && Ticket.Valid() && DeviceInterchange->QueryTimelineReached(Ticket.TimelineSignal);
}

bool TimelineTransferRegister::AwaitAndCopyBytes(const ReadbackTicket& Ticket, std::span<uint8_t> DestinationBytes) const noexcept
{
    if (DeviceInterchange == nullptr || !Ticket.Valid() || DestinationBytes.size() < Ticket.ByteLength || Ticket.SlotIndex >= RingSlotCount)
    {
        return false;
    }
    if (!DeviceInterchange->AwaitTimelineTick(Ticket.TimelineSignal))
    {
        return false;
    }

    const TransferSlot& Slot = Slots[Ticket.SlotIndex];
    if (Slot.Buffer.HostAddress == nullptr || Ticket.ByteOffset + Ticket.ByteLength > Slot.Buffer.ByteCapacity)
    {
        return false;
    }

    const auto* SourceBytes = static_cast<const uint8_t*>(Slot.Buffer.HostAddress) + Ticket.ByteOffset;
    std::memcpy(DestinationBytes.data(), SourceBytes, static_cast<std::size_t>(Ticket.ByteLength));
    return true;
}

} // namespace Frontier::GeometricRaster
