/*============================================================================================================================================
                                                       ASYNCGPUREADBACKRING.H
============================================================================================================================================*/
// 🧩 Merge-Prep Phase U01: 3-slot non-blocking GPU->CPU async readback ring (Single-Queue FIFO compliant).
//    Strict hard ban on mid-frame GPU stalls: NEVER calls vkDeviceWaitIdle, vkQueueWaitIdle, vkWaitForFences,
//    or AwaitTimelineTick. Completed slots are polled exclusively via VulkanInterchange::QueryTimelineReached()
//    with 1-2 frames latency and zero per-frame heap allocations.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#if __has_include(<volk.h>)
#define FRONTIER_SIM_HAS_VULKAN 1
#include "Engine/GeometricRaster/VulkanInterchange.h"
#else
#define FRONTIER_SIM_HAS_VULKAN 0
#endif

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                             3-SLOT ASYNC GPU READBACK RING
//------------------------------------------------------------------------------------------------------------------------

struct AsyncReadbackTelemetry
{
    uint64_t SubmittedCount    = 0ull; // [cnt] - Total async readback copies submitted
    uint64_t CompletedCount    = 0ull; // [cnt] - Total slots retired non-blockingly
    uint64_t PendingCount      = 0ull; // [cnt] - Currently in-flight slots [0..3]
    uint64_t LastCompletedTick = 0ull; // [cnt] - Latest retired timeline ticket
    uint32_t ActiveWriteSlot   = 0u;   // [0..2]- Current write ring slot
};

/// Triple-buffered non-blocking GPU->CPU readback ring for Fluid occupancy, Particle->Rigid impulses,
/// and Deformable->Jolt contact feedback. Pre-allocates all 3 staging slots once during Initialize().
template <typename ElementT, uint32_t SlotCount = 3u>
class AsyncGpuReadbackRing
{
    static_assert(SlotCount == 3u, "Single-Queue FIFO readback rings must use exactly 3 triple-buffered slots.");

public:
    AsyncGpuReadbackRing() = default;
    ~AsyncGpuReadbackRing() { Teardown(); }

    AsyncGpuReadbackRing(const AsyncGpuReadbackRing&)            = delete;
    AsyncGpuReadbackRing& operator=(const AsyncGpuReadbackRing&) = delete;

    /// Pre-allocates 3 host-cached staging slots (real Vulkan buffers when interchange is ready, else CPU staging).
    [[nodiscard]] bool Initialize(
        uint32_t maxElementCount
#if FRONTIER_SIM_HAS_VULKAN
        , GeometricRaster::VulkanInterchange* interchange = nullptr
#endif
    )
    {
        Teardown();
        if (maxElementCount == 0u) return false;

        MaxElementCount_ = maxElementCount;
        SlotByteCapacity_ = static_cast<uint64_t>(maxElementCount) * sizeof(ElementT);
        LatestCompleted_.reserve(maxElementCount);
        LatestCompleted_.clear();

#if FRONTIER_SIM_HAS_VULKAN
        Interchange_ = interchange;
        if (Interchange_ != nullptr && Interchange_->Ready())
        {
            for (uint32_t i = 0u; i < SlotCount; ++i)
            {
                Slots_[i].GpuStaging = Interchange_->AllocateBuffer(
                    SlotByteCapacity_,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                if (Slots_[i].GpuStaging.BufferHandle == VK_NULL_HANDLE || Slots_[i].GpuStaging.HostAddress == nullptr)
                {
                    Teardown();
                    return false;
                }
            }
            GpuBacked_ = true;
        }
#endif

        if (!GpuBacked_)
        {
            for (uint32_t i = 0u; i < SlotCount; ++i)
            {
                Slots_[i].CpuMirror.reserve(maxElementCount);
                Slots_[i].CpuMirror.clear();
            }
        }

        Initialized_ = true;
        return true;
    }

    void Teardown() noexcept
    {
#if FRONTIER_SIM_HAS_VULKAN
        if (Interchange_ != nullptr && Interchange_->Ready())
        {
            for (uint32_t i = 0u; i < SlotCount; ++i)
            {
                if (Slots_[i].GpuStaging.BufferHandle != VK_NULL_HANDLE)
                {
                    Interchange_->ReleaseBuffer(Slots_[i].GpuStaging);
                }
            }
        }
        Interchange_ = nullptr;
#endif
        for (uint32_t i = 0u; i < SlotCount; ++i)
        {
            Slots_[i].CpuMirror.clear();
            Slots_[i].TimelineSignal = 0ull;
            Slots_[i].ElementCount   = 0u;
            Slots_[i].Pending        = false;
        }
        LatestCompleted_.clear();
        Telemetry_       = {};
        MaxElementCount_ = 0u;
        SlotByteCapacity_ = 0ull;
        GpuBacked_       = false;
        Initialized_     = false;
    }

#if FRONTIER_SIM_HAS_VULKAN
    /// Encodes a non-blocking GPU->staging buffer copy into `cmdBuffer` for the active ring slot.
    /// Caller must follow with BindActiveSlotTimeline(submittedTimelineTick) after DispatchCommandBufferAsync().
    [[nodiscard]] bool EncodeAsyncCopy(
        VkCommandBuffer cmdBuffer,
        VkBuffer        sourceDeviceBuffer,
        uint32_t        elementCount,
        uint64_t        sourceOffsetBytes = 0ull) noexcept
    {
        if (!Initialized_ || !GpuBacked_ || cmdBuffer == VK_NULL_HANDLE || sourceDeviceBuffer == VK_NULL_HANDLE)
        {
            return false;
        }
        const uint32_t clampedCount = (elementCount <= MaxElementCount_) ? elementCount : MaxElementCount_;
        if (clampedCount == 0u) return false;

        auto& slot = Slots_[Telemetry_.ActiveWriteSlot];
        VkBufferCopy copyRegion {};
        copyRegion.srcOffset = sourceOffsetBytes;
        copyRegion.dstOffset = 0ull;
        copyRegion.size      = static_cast<uint64_t>(clampedCount) * sizeof(ElementT);
        vkCmdCopyBuffer(cmdBuffer, sourceDeviceBuffer, slot.GpuStaging.BufferHandle, 1u, &copyRegion);

        slot.ElementCount = clampedCount;
        return true;
    }

    /// Associates the Vulkan Timeline Semaphore ticket returned by DispatchCommandBufferAsync with the active write slot
    /// and advances the ring write cursor. Zero blocking wait.
    void BindActiveSlotTimeline(uint64_t timelineSignal) noexcept
    {
        if (!Initialized_ || timelineSignal == 0ull) return;
        auto& slot = Slots_[Telemetry_.ActiveWriteSlot];
        slot.TimelineSignal = timelineSignal;
        slot.Pending        = true;
        Telemetry_.SubmittedCount += 1ull;
        Telemetry_.ActiveWriteSlot = (Telemetry_.ActiveWriteSlot + 1u) % SlotCount;
        RefreshPendingCount();
    }

    [[nodiscard]] VkBuffer QueryActiveStagingBuffer() const noexcept
    {
        return GpuBacked_ ? Slots_[Telemetry_.ActiveWriteSlot].GpuStaging.BufferHandle : VK_NULL_HANDLE;
    }
#endif

    /// Headless/CPU-mirror equivalent of EncodeAsyncCopy + BindActiveSlotTimeline preserving exact 1-2 frame ring semantics.
    void SubmitHeadlessSnapshot(std::span<const ElementT> elements, uint64_t monotonicTicket) noexcept
    {
        if (!Initialized_ || GpuBacked_ || monotonicTicket == 0ull) return;
        const uint32_t clampedCount = static_cast<uint32_t>(
            (elements.size() <= MaxElementCount_) ? elements.size() : MaxElementCount_);
        auto& slot = Slots_[Telemetry_.ActiveWriteSlot];
        slot.CpuMirror.resize(clampedCount);
        if (clampedCount > 0u)
        {
            std::memcpy(slot.CpuMirror.data(), elements.data(), static_cast<size_t>(clampedCount) * sizeof(ElementT));
        }
        slot.ElementCount   = clampedCount;
        slot.TimelineSignal = monotonicTicket;
        slot.Pending        = true;
        Telemetry_.SubmittedCount += 1ull;
        Telemetry_.ActiveWriteSlot = (Telemetry_.ActiveWriteSlot + 1u) % SlotCount;
        RefreshPendingCount();
    }

    /// Strictly non-blocking poll: retires any slot whose TimelineSignal has been reached (GPU via QueryTimelineReached,
    /// headless via completedHeadlessTicket >= slot.TimelineSignal). Copies the newest completed slot into LatestCompleted_.
    /// Returns true if a newer readback payload became available this call. Zero heap allocations.
    [[nodiscard]] bool PollNonBlocking(uint64_t completedHeadlessTicket = 0ull) noexcept
    {
        if (!Initialized_) return false;

        int32_t newestReadyIdx = -1;
        uint64_t newestSignal  = 0ull;

        for (uint32_t i = 0u; i < SlotCount; ++i)
        {
            auto& slot = Slots_[i];
            if (!slot.Pending) continue;

            bool ready = false;
#if FRONTIER_SIM_HAS_VULKAN
            if (GpuBacked_ && Interchange_ != nullptr && Interchange_->Ready())
            {
                ready = Interchange_->QueryTimelineReached(slot.TimelineSignal);
            }
            else
#endif
            {
                ready = (completedHeadlessTicket >= slot.TimelineSignal);
            }

            if (ready)
            {
                if (slot.TimelineSignal >= newestSignal)
                {
                    newestSignal   = slot.TimelineSignal;
                    newestReadyIdx = static_cast<int32_t>(i);
                }
                slot.Pending = false;
                Telemetry_.CompletedCount += 1ull;
            }
        }

        RefreshPendingCount();
        if (newestReadyIdx < 0) return false;

        const auto& winner = Slots_[static_cast<uint32_t>(newestReadyIdx)];
        LatestCompleted_.resize(winner.ElementCount);
        if (winner.ElementCount > 0u)
        {
#if FRONTIER_SIM_HAS_VULKAN
            if (GpuBacked_ && winner.GpuStaging.HostAddress != nullptr)
            {
                std::memcpy(
                    LatestCompleted_.data(),
                    winner.GpuStaging.HostAddress,
                    static_cast<size_t>(winner.ElementCount) * sizeof(ElementT));
            }
            else
#endif
            {
                std::memcpy(
                    LatestCompleted_.data(),
                    winner.CpuMirror.data(),
                    static_cast<size_t>(winner.ElementCount) * sizeof(ElementT));
            }
        }

        Telemetry_.LastCompletedTick = winner.TimelineSignal;
        return true;
    }

    [[nodiscard]] std::span<const ElementT> InspectLatestCompleted() const noexcept
    {
        return LatestCompleted_;
    }

    [[nodiscard]] bool QueryReady() const noexcept { return Initialized_; }
    [[nodiscard]] bool QueryGpuBacked() const noexcept { return GpuBacked_; }
    [[nodiscard]] const AsyncReadbackTelemetry& InspectTelemetry() const noexcept { return Telemetry_; }

private:
    struct RingSlot
    {
#if FRONTIER_SIM_HAS_VULKAN
        GeometricRaster::DeviceBufferResource GpuStaging {};
#endif
        std::vector<ElementT> CpuMirror      {};
        uint64_t              TimelineSignal = 0ull;
        uint32_t              ElementCount   = 0u;
        bool                  Pending        = false;
    };

    void RefreshPendingCount() noexcept
    {
        uint64_t pending = 0ull;
        for (const auto& s : Slots_)
        {
            if (s.Pending) ++pending;
        }
        Telemetry_.PendingCount = pending;
    }

#if FRONTIER_SIM_HAS_VULKAN
    GeometricRaster::VulkanInterchange* Interchange_ = nullptr;
#endif
    std::array<RingSlot, SlotCount> Slots_            {};
    std::vector<ElementT>           LatestCompleted_  {};
    AsyncReadbackTelemetry          Telemetry_        {};
    uint32_t                        MaxElementCount_  = 0u;
    uint64_t                        SlotByteCapacity_ = 0ull;
    bool                            GpuBacked_        = false;
    bool                            Initialized_      = false;
};

} // namespace Frontier::SimulationSpace
