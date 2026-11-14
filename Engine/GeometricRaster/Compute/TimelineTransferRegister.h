/*============================================================================================================================================
                                                            TIMELINETRANSFERREGISTER.H
============================================================================================================================================*/
// 🧩 Reusable triple-buffered HOST_VISIBLE | HOST_CACHED GPU->CPU readback ring synchronized via Vulkan Timeline Semaphores

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <array>
#include <cstdint>
#include <span>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ReadbackTicket
{
    uint64_t TimelineSignal = 0ull; // [count] - Vulkan Timeline Semaphore counter signaling completion
    uint32_t SlotIndex      = 0u;   // [idx]   - Ring slot index [0..2]
    uint64_t ByteOffset     = 0ull; // [B]     - Offset inside staging slot
    uint64_t ByteLength     = 0ull; // [B]     - Requested readback payload byte count

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return TimelineSignal != 0ull && ByteLength != 0ull;
    }
};

class TimelineTransferRegister
{
public:
    static constexpr uint32_t RingSlotCount = 3u;

    TimelineTransferRegister() = default;
    ~TimelineTransferRegister();

    TimelineTransferRegister(const TimelineTransferRegister&)            = delete;
    TimelineTransferRegister& operator=(const TimelineTransferRegister&) = delete;

    [[nodiscard]] bool InitializeRegister(
        VulkanInterchange* BoundDevice,
        uint64_t             SlotCapacityBytes = 8ull * 1024ull * 1024ull);

    void TeardownRegister() noexcept;

    void AdvanceRingSlot() noexcept;

    [[nodiscard]] ReadbackTicket EncodeBufferReadback(
        VkCommandBuffer SourceCmdBuffer,
        VkBuffer        SourceDeviceBuffer,
        uint64_t        SourceOffsetBytes,
        uint64_t        CopyByteCount);

    [[nodiscard]] ReadbackTicket EncodeImageRegionReadback(
        VkCommandBuffer SourceCmdBuffer,
        VkImage         SourceImage,
        VkImageLayout   CurrentImageLayout,
        int32_t         OffsetX,
        int32_t         OffsetY,
        uint32_t        ExtentWidth,
        uint32_t        ExtentHeight,
        uint32_t        BytesPerPixel);

    void BindTicketTimelineSignal(ReadbackTicket& Ticket, uint64_t TimelineSignal) noexcept;

    [[nodiscard]] bool QueryTicketReady(const ReadbackTicket& Ticket) const noexcept;
    [[nodiscard]] bool AwaitAndCopyBytes(const ReadbackTicket& Ticket, std::span<uint8_t> DestinationBytes) const noexcept;

    template <typename EntryScalar>
    [[nodiscard]] bool AwaitAndCopyTyped(const ReadbackTicket& Ticket, std::span<EntryScalar> DestinationSpan) const noexcept
    {
        return AwaitAndCopyBytes(
            Ticket,
            std::span<uint8_t>(
                reinterpret_cast<uint8_t*>(DestinationSpan.data()),
                DestinationSpan.size_bytes()));
    }

private:
    struct TransferSlot
    {
        DeviceBufferResource Buffer         {};   // [-]     - Host-visible cached staging buffer
        uint64_t             WriteCursor    = 0ull; // [B]   - Current 64-byte aligned write offset
        uint64_t             LastSignal     = 0ull; // [cnt] - Last timeline value written to this slot
    };

    VulkanInterchange*                     DeviceInterchange      = nullptr; // [-]   - Non-owning device interchange
    std::array<TransferSlot, RingSlotCount>   Slots        {};        // [-]   - Triple-buffered staging slots
    uint32_t                                 ActiveSlot   = 0u;      // [idx] - Current ring write slot
};

} // namespace Frontier::GeometricRaster
