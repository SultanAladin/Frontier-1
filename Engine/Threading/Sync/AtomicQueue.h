/*============================================================================================================================================
                                                               ATOMICQUEUE.H
============================================================================================================================================*/
// 🧩 Bounded Vyukov lock-free multi-producer multi-consumer ring queue with zero post-initialization heap allocations

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

template <typename PayloadType>
class AtomicQueue
{
public:
    explicit AtomicQueue(std::size_t RequestedCapacity = 1024u)
    {
        const std::size_t Normalized = std::bit_ceil(RequestedCapacity < 2u ? std::size_t{ 2u } : RequestedCapacity);
        RingCapacity                 = Normalized;
        IndexMask                   = RingCapacity - 1u;
        RingSlots                   = std::make_unique<SequenceSlot[]>(RingCapacity);
        for (std::size_t SlotOrdinal = 0u; SlotOrdinal < RingCapacity; ++SlotOrdinal)
        {
            RingSlots[SlotOrdinal].SequenceStamp.store(SlotOrdinal, std::memory_order_relaxed);
        }
    }

    AtomicQueue(const AtomicQueue&)            = delete;
    AtomicQueue& operator=(const AtomicQueue&) = delete;

    [[nodiscard]] bool TryEnqueue(PayloadType IncomingPayload) noexcept
    {
        SequenceSlot* TargetSlot = nullptr;
        std::size_t   WritePos   = EnqueueCursor.load(std::memory_order_relaxed);
        for (;;)
        {
            TargetSlot                  = &RingSlots[WritePos & IndexMask];
            const std::size_t Stamp     = TargetSlot->SequenceStamp.load(std::memory_order_acquire);
            const std::ptrdiff_t Offset = static_cast<std::ptrdiff_t>(Stamp) - static_cast<std::ptrdiff_t>(WritePos);
            if (Offset == 0)
            {
                if (EnqueueCursor.compare_exchange_weak(WritePos, WritePos + 1u, std::memory_order_relaxed))
                {
                    break;
                }
            }
            else if (Offset < 0)
            {
                return false;
            }
            else
            {
                WritePos = EnqueueCursor.load(std::memory_order_relaxed);
            }
        }

        TargetSlot->Payload = std::move(IncomingPayload);
        TargetSlot->SequenceStamp.store(WritePos + 1u, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryDequeue(PayloadType& OutPayload) noexcept
    {
        SequenceSlot* TargetSlot = nullptr;
        std::size_t   ReadPos    = DequeueCursor.load(std::memory_order_relaxed);
        for (;;)
        {
            TargetSlot                  = &RingSlots[ReadPos & IndexMask];
            const std::size_t Stamp     = TargetSlot->SequenceStamp.load(std::memory_order_acquire);
            const std::ptrdiff_t Offset = static_cast<std::ptrdiff_t>(Stamp) - static_cast<std::ptrdiff_t>(ReadPos + 1u);
            if (Offset == 0)
            {
                if (DequeueCursor.compare_exchange_weak(ReadPos, ReadPos + 1u, std::memory_order_relaxed))
                {
                    break;
                }
            }
            else if (Offset < 0)
            {
                return false;
            }
            else
            {
                ReadPos = DequeueCursor.load(std::memory_order_relaxed);
            }
        }

        OutPayload = std::move(TargetSlot->Payload);
        TargetSlot->SequenceStamp.store(ReadPos + IndexMask + 1u, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t ApproximateDepth() const noexcept
    {
        const std::size_t WritePos = EnqueueCursor.load(std::memory_order_relaxed);
        const std::size_t ReadPos  = DequeueCursor.load(std::memory_order_relaxed);
        return WritePos >= ReadPos ? (WritePos - ReadPos) : 0u;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return ApproximateDepth() == 0u;
    }

    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return RingCapacity;
    }

private:
    struct alignas(64) SequenceSlot
    {
        std::atomic<std::size_t> SequenceStamp { 0u }; // [count] - Turn sequence stamp for lock-free slot handoff
        PayloadType              Payload       {};     // [-]     - Stored payload instance
    };

    std::size_t                     RingCapacity  = 0u;      // [count] - Power-of-two slot capacity
    std::size_t                     IndexMask     = 0u;      // [mask]  - Bitmask wrapping cursors into RingSlots
    std::unique_ptr<SequenceSlot[]> RingSlots     {};        // [-]     - Preallocated cache-aligned ring array
    alignas(64) std::atomic<std::size_t> EnqueueCursor { 0u }; // [idx] - Monotonic producer cursor
    alignas(64) std::atomic<std::size_t> DequeueCursor { 0u }; // [idx] - Monotonic consumer cursor
};

} // namespace Frontier::Threading
