//============================================================================================================================================
//                                                            REQUESTQUEUE.CPP
//============================================================================================================================================
// 🧩 Coalescing by cell, the cyclic cycle slots, and the readback that is exactly one depth behind.

#include "SlateCompute/Compute/RequestQueue/Api/RequestQueue.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ARRIVAL ORDER
//------------------------------------------------------------------------------------------------------------------------

void PageQueue::Accept(const CellDemand& Incoming)
{
    // 📝 Searched newest first. A sample walking a surface demands the same cell for a run of adjacent pixels,
    //    so the coalescing terminates on its first comparison for the case that dominates.
    for (std::size_t Index = ArrivalOrder.size(); Index-- > 0u;)
    {
        CellDemand& Held = ArrivalOrder[Index];

        if (Held.SurfaceIndex == Incoming.SurfaceIndex && Held.CellIndex == Incoming.CellIndex)
        {
            Held.OccurrenceCount += Incoming.OccurrenceCount;
            return;
        }
    }

    if (ArrivalOrder.size() >= ArrivalLimit)
    {
        ++DiscardedDemands;
        return;
    }

    ArrivalOrder.push_back(Incoming);
}

const std::vector<CellDemand>& PageQueue::Arrivals() const { return ArrivalOrder; }

void PageQueue::Reclaim()
{
    ArrivalOrder.clear();
    DiscardedDemands = 0u;
}

std::uint32_t PageQueue::ArrivalCount() const
{
    return static_cast<std::uint32_t>(ArrivalOrder.size());
}

std::uint32_t PageQueue::DiscardedCount() const { return DiscardedDemands; }

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE DEMANDS
//------------------------------------------------------------------------------------------------------------------------

void RequestQueue::Demand(std::uint32_t SurfaceIndex, std::uint32_t CellIndex, std::uint64_t RecordingIndex)
{
    CellDemand Incoming;
    Incoming.SurfaceIndex = SurfaceIndex;
    Incoming.CellIndex    = CellIndex;

    CycleSlots[RecordingIndex % SlotCount].Accept(Incoming);
    ++RecordedDemands;
}

PageQueue& RequestQueue::SlotAt(std::uint64_t RecordingIndex)
{
    return CycleSlots[RecordingIndex % SlotCount];
}

const PageQueue& RequestQueue::SlotAt(std::uint64_t RecordingIndex) const
{
    return CycleSlots[RecordingIndex % SlotCount];
}

std::uint64_t RequestQueue::RecordedCount() const { return RecordedDemands; }

std::uint64_t RequestQueue::DiscardedCount() const
{
    std::uint64_t Discarded = 0u;

    for (const PageQueue& Held : CycleSlots)
        Discarded += Held.DiscardedCount();

    return Discarded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE READBACK
//------------------------------------------------------------------------------------------------------------------------

Deliver<const PageQueue*> ReturnIndex::Drain(RequestQueue& Requesting, std::uint64_t RecordingIndex)
{
    // 📝 The first rotations of a session have nothing recorded a depth ago. Refusing is honest: the caller
    //    promotes nothing, and the coarsest levels are permanently resident so every sample still resolves.
    if (RecordingIndex < RecordingSlotCount)
    {
        return Deliver<const PageQueue*>::Refuse(
            { RefusalReason::ExtentExhausted, "the readback latency has not yet elapsed" });
    }

    if (DrainCurrent && RecordingIndex <= LastDrained)
    {
        return Deliver<const PageQueue*>::Refuse(
            { RefusalReason::HostDenied, "this rotation has already been drained" });
    }

    // 🔴 Exactly one depth behind. `20` §2.1 ②'s latency is not an approximation of the device's readback — it
    //    **is** the readback, and a drain that read the current slot would present demands the device has not
    //    finished writing.
    PageQueue& Drained = Requesting.SlotAt(RecordingIndex - RecordingSlotCount);

    LastDrained   = RecordingIndex;
    DrainCurrent = true;
    ++DrainCount;

    return Deliver<const PageQueue*>::Result(&Drained);
}

std::uint64_t ReturnIndex::DrainedRecording() const { return LastDrained; }
std::uint64_t ReturnIndex::DrainedCount() const    { return DrainCount;  }

}   // namespace Slate
