/*==========================================================================================================================================
                                                        THREADPROGRESSIONTRACKER.CPP
==========================================================================================================================================*/
// 🧩 Lock-free monotonic thread heartbeat implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Sync/ThreadProgressionTracker.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 PROGRESSION TRACKER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

uint64_t ThreadProgressionTracker::QueryMonotonicMicroseconds() noexcept
{
    const auto Now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Now).count());
}

uint32_t ThreadProgressionTracker::RegisterThreadSlot(std::string_view ThreadLabel) noexcept
{
    const uint64_t InitialUs = QueryMonotonicMicroseconds();
    for (uint32_t Index = 0u; Index < Constants::MaximumMonitoredThreads; ++Index)
    {
        bool Expected = false;
        if (Slots[Index].SlotOccupied.compare_exchange_strong(Expected, true, std::memory_order_acq_rel))
        {
            std::memset(Slots[Index].LabelCharacters, 0, sizeof(Slots[Index].LabelCharacters));
            const std::size_t CopyLength = std::min<std::size_t>(ThreadLabel.size(), sizeof(Slots[Index].LabelCharacters) - 1u);
            if (CopyLength > 0u)
            {
                std::memcpy(Slots[Index].LabelCharacters, ThreadLabel.data(), CopyLength);
            }
            Slots[Index].ActiveTaskIdentifier.store(0u, std::memory_order_relaxed);
            Slots[Index].PulseCount.store(1ull, std::memory_order_relaxed);
            Slots[Index].LatestPulseUs.store(InitialUs, std::memory_order_release);
            return Index + 1u;
        }
    }
    return 0u;
}

void ThreadProgressionTracker::UnregisterThreadSlot(uint32_t SlotIndex) noexcept
{
    if (SlotIndex == 0u || SlotIndex > Constants::MaximumMonitoredThreads)
    {
        return;
    }
    CacheIsolatedSlot& Target = Slots[SlotIndex - 1u];
    Target.ActiveTaskIdentifier.store(0u, std::memory_order_relaxed);
    Target.SlotOccupied.store(false, std::memory_order_release);
}

void ThreadProgressionTracker::RegisterPulse(uint32_t SlotIndex, uint32_t ActiveTaskIdentifier) noexcept
{
    if (SlotIndex == 0u || SlotIndex > Constants::MaximumMonitoredThreads)
    {
        return;
    }
    CacheIsolatedSlot& Target = Slots[SlotIndex - 1u];
    Target.ActiveTaskIdentifier.store(ActiveTaskIdentifier, std::memory_order_relaxed);
    Target.PulseCount.fetch_add(1ull, std::memory_order_relaxed);
    Target.LatestPulseUs.store(QueryMonotonicMicroseconds(), std::memory_order_release);
}

ThreadProgressionReading ThreadProgressionTracker::InspectSlot(
    uint32_t SlotIndex,
    uint64_t WarningThresholdUs,
    uint64_t HangThresholdUs) const noexcept
{
    ThreadProgressionReading Reading{};
    if (SlotIndex == 0u || SlotIndex > Constants::MaximumMonitoredThreads)
    {
        return Reading;
    }

    const CacheIsolatedSlot& Target = Slots[SlotIndex - 1u];
    Reading.SlotIndex               = SlotIndex;
    Reading.ThreadLabel             = std::string_view{ Target.LabelCharacters };
    if (!Target.SlotOccupied.load(std::memory_order_acquire))
    {
        Reading.Condition = ThreadLivenessCondition::Inactive;
        return Reading;
    }

    const uint64_t NowUs         = QueryMonotonicMicroseconds();
    const uint64_t PulseUs       = Target.LatestPulseUs.load(std::memory_order_acquire);
    const uint64_t ElapsedUs     = NowUs >= PulseUs ? (NowUs - PulseUs) : 0ull;
    Reading.LatestPulseUs        = PulseUs;
    Reading.ElapsedSincePulseUs  = ElapsedUs;
    Reading.TotalPulseCount      = Target.PulseCount.load(std::memory_order_relaxed);
    Reading.ActiveTaskIdentifier = Target.ActiveTaskIdentifier.load(std::memory_order_relaxed);

    if (ElapsedUs >= HangThresholdUs)
    {
        Reading.Condition = ThreadLivenessCondition::Unresponsive;
    }
    else if (ElapsedUs >= WarningThresholdUs)
    {
        Reading.Condition = ThreadLivenessCondition::StallWarned;
    }
    else
    {
        Reading.Condition = ThreadLivenessCondition::Healthy;
    }
    return Reading;
}

uint32_t ThreadProgressionTracker::QueryActiveSlotCount() const noexcept
{
    uint32_t ActiveTotal = 0u;
    for (const CacheIsolatedSlot& Slot : Slots)
    {
        if (Slot.SlotOccupied.load(std::memory_order_relaxed))
        {
            ++ActiveTotal;
        }
    }
    return ActiveTotal;
}

uint32_t ThreadProgressionTracker::QueryStalledSlotCount(uint64_t WarningThresholdUs) const noexcept
{
    uint32_t StalledTotal = 0u;
    for (uint32_t Index = 1u; Index <= Constants::MaximumMonitoredThreads; ++Index)
    {
        const ThreadProgressionReading Reading = InspectSlot(Index, WarningThresholdUs, Constants::WatchdogHangThresholdUs);
        if (Reading.Condition == ThreadLivenessCondition::StallWarned ||
            Reading.Condition == ThreadLivenessCondition::Unresponsive)
        {
            ++StalledTotal;
        }
    }
    return StalledTotal;
}

} // namespace Frontier::Threading
