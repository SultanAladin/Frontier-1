/*============================================================================================================================================
                                                                TASKQUEUE.H
============================================================================================================================================*/
// 🧩 Bounded preallocated work-stealing task deque supporting LIFO local pop and FIFO cross-lane steal

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Sync/CompletionCounter.h"
#include "Engine/Threading/Sync/SpinLock.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class TaskPriorityTier : uint8_t
{
    ImmediateCritical  = 0, // [-] - Tier 0: Replication delta transmission & snapshot publish (always executes first)
    DeterministicTick  = 1, // [-] - Tier 1: Mandatory deterministic simulation & constraint work
    WorkspaceAuthoring = 2, // [-] - Tier 2: Interactive .Space authoring, UVTile unwrap, & revision replay
    OpportunisticSlack = 3  // [-] - Tier 3: Background work strictly bounded by remaining tick slice
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ScheduledTaskUnit
{
    uint32_t              TaskIdentifier      = 0u;                                    // [idx] - Diagnostic & heartbeat task tag
    TaskPriorityTier      PriorityTier        = TaskPriorityTier::WorkspaceAuthoring;  // [-]   - Execution priority tier
    uint64_t              EstimatedDurationUs = 50ull;                                 // [us]  - Budget estimate for Tier 3 admission
    std::function<void()> Callable            {};                                      // [-]   - Encapsulated task body
    CompletionCounter*    CompletionTarget    = nullptr;                               // [-]   - Optional non-blocking completion latch

    void Execute() noexcept
    {
        if (Callable)
        {
            Callable();
        }
        if (CompletionTarget != nullptr)
        {
            CompletionTarget->Decrement();
        }
    }
};

class TaskQueue
{
public:
    explicit TaskQueue(std::size_t RequestedCapacity = 1024u)
    {
        RingCapacity = std::bit_ceil(RequestedCapacity < 4u ? std::size_t{ 4u } : RequestedCapacity);
        IndexMask    = RingCapacity - 1u;
        RingStorage  = std::make_unique<ScheduledTaskUnit[]>(RingCapacity);
    }

    TaskQueue(const TaskQueue&)            = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    [[nodiscard]] bool PushBottom(ScheduledTaskUnit IncomingUnit) noexcept
    {
        ScopedSpinGuard Guard(DequeLock);
        if ((BottomCursor - TopCursor) >= RingCapacity)
        {
            return false;
        }
        RingStorage[BottomCursor & IndexMask] = std::move(IncomingUnit);
        ++BottomCursor;
        return true;
    }

    [[nodiscard]] bool PopBottom(ScheduledTaskUnit& OutUnit) noexcept
    {
        ScopedSpinGuard Guard(DequeLock);
        if (BottomCursor == TopCursor)
        {
            return false;
        }
        --BottomCursor;
        OutUnit = std::move(RingStorage[BottomCursor & IndexMask]);
        return true;
    }

    [[nodiscard]] bool StealTop(ScheduledTaskUnit& OutUnit) noexcept
    {
        ScopedSpinGuard Guard(DequeLock);
        if (TopCursor == BottomCursor)
        {
            return false;
        }
        OutUnit = std::move(RingStorage[TopCursor & IndexMask]);
        ++TopCursor;
        return true;
    }

    [[nodiscard]] std::size_t Depth() const noexcept
    {
        ScopedSpinGuard Guard(DequeLock);
        return BottomCursor - TopCursor;
    }

    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return RingCapacity;
    }

private:
    std::size_t                          RingCapacity = 0u; // [count] - Power-of-two deque slot capacity
    std::size_t                          IndexMask    = 0u; // [mask]  - Wrap mask for circular indexing
    std::size_t                          TopCursor    = 0u; // [idx]   - FIFO steal end cursor
    std::size_t                          BottomCursor = 0u; // [idx]   - LIFO local push/pop cursor
    std::unique_ptr<ScheduledTaskUnit[]> RingStorage  {};   // [-]     - Preallocated task unit storage
    mutable SpinLock                     DequeLock    {};   // [-]     - Leaf spin guard for move-only callables
};

} // namespace Frontier::Threading
