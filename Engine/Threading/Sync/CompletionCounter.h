/*============================================================================================================================================
                                                           COMPLETIONCOUNTER.H
============================================================================================================================================*/
// 🧩 Wait-free atomic countdown latch and non-blocking continuation trigger preventing dependent-task sleep waits

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Sync/SpinLock.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class CompletionCounter
{
public:
    using ContinuationCallable = std::function<void()>;

    explicit CompletionCounter(uint32_t InitialCount = 0u) noexcept
        : RemainingPrerequisites(InitialCount)
    {
    }

    CompletionCounter(const CompletionCounter&)            = delete;
    CompletionCounter& operator=(const CompletionCounter&) = delete;

    void Reset(uint32_t InitialCount) noexcept
    {
        ScopedSpinGuard Guard(ContinuationLock);
        Continuations.clear();
        RemainingPrerequisites.store(InitialCount, std::memory_order_release);
    }

    void AddPrerequisite(uint32_t AdditionalCount = 1u) noexcept
    {
        RemainingPrerequisites.fetch_add(AdditionalCount, std::memory_order_acq_rel);
    }

    void Decrement() noexcept
    {
        const uint32_t Previous = RemainingPrerequisites.fetch_sub(1u, std::memory_order_acq_rel);
        if (Previous == 1u)
        {
            DispatchContinuations();
        }
    }

    void AttachContinuation(ContinuationCallable Continuation)
    {
        if (!Continuation)
        {
            return;
        }
        bool ExecuteImmediately = false;
        {
            ScopedSpinGuard Guard(ContinuationLock);
            if (RemainingPrerequisites.load(std::memory_order_acquire) == 0u)
            {
                ExecuteImmediately = true;
            }
            else
            {
                Continuations.push_back(std::move(Continuation));
            }
        }
        if (ExecuteImmediately)
        {
            Continuation();
        }
    }

    [[nodiscard]] uint32_t PendingCount() const noexcept
    {
        return RemainingPrerequisites.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool Completed() const noexcept
    {
        return PendingCount() == 0u;
    }

private:
    void DispatchContinuations() noexcept
    {
        std::vector<ContinuationCallable> ReadyList;
        {
            ScopedSpinGuard Guard(ContinuationLock);
            ReadyList.swap(Continuations);
        }
        for (ContinuationCallable& Callable : ReadyList)
        {
            if (Callable)
            {
                Callable();
            }
        }
    }

    std::atomic<uint32_t>             RemainingPrerequisites { 0u }; // [count] - Unfinished prerequisite task count
    SpinLock                          ContinuationLock       {};     // [-]     - Leaf lock protecting continuation list
    std::vector<ContinuationCallable> Continuations          {};     // [-]     - Non-blocking dependent continuations
};

} // namespace Frontier::Threading
