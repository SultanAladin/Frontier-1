//============================================================================================================================================
//                                                             REQUESTQUEUE.H
//============================================================================================================================================
// 🧩 Device-written cell demands, drained with latency — the readback half named apart, and the arrival order it yields.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/NumericTolerance.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE DEMAND
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One cell a sample touched and did not find resident.
/// note  🔴 A demand carries no level of its own: the cell ordinal already names one, because `20` §1's
///        subdivision numbers every cell of every reduction level in one span. A demand carrying both would be
///        two answers to one question and the two would eventually disagree.
/// tag   nonallocating, nonthrowing
struct CellDemand
{
    std::uint32_t  SurfaceIndex  = 0u;   // [-] - which surface's residency
    std::uint32_t  CellIndex     = 0u;   // [-] - the cell, level included — `20` §1
    std::uint32_t  OccurrenceCount = 1u;   // [-] - raised by coalescing, never by the caller
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE ARRIVAL ORDER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The drained demands of one rotation, in first-arrival order and coalesced.
/// note  🔴 `20` §2.1 ③ presents demands **in arrival order**, and arrival order is what the promotion budget
///        then spends itself against. Ordering by occurrence count instead would promote the largest surface
///        before the one under the cursor, which is exactly backwards.
/// note  📝 Coalesced by surface and cell together. A million pixels sampling one non-resident cell is one
///        demand with a count of a million, and the count is what a presenter reads to say how badly it is
///        wanted — not what the ordering reads.
/// tag   owning
class PageQueue
{
public:

    // 🚧 `20` §6 carries the promotion budget as open and this ceiling with it. It bounds one rotation's
    //    distinct demands, so a surface that suddenly becomes wholly visible defers rather than allocating
    //    during a rotation. Read by this unit alone, so `00` §2 keeps it here.
    static constexpr std::uint32_t ArrivalLimit = 4096u;   // [-] - distinct cells one drain may present

    /// 🧩 Accepts one demand, coalescing it into an earlier demand for the same cell.
    /// post  the arrival count never exceeds ArrivalLimit; a demand beyond it is discarded and counted
    /// note  ⚠️ A discarded demand is a **deferral**, not a loss. Sampling still touches the cell next
    ///        rotation and demands it again, so the discard costs one rotation of coarseness and nothing else.
    ///        That is why it is a measure rather than a report — `86` §5.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Accept(const CellDemand& Incoming);

    /// 🧩 The demands, in first-arrival order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<CellDemand>& Arrivals() const;

    /// 🧩 Empties the order, ready for the next drain.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

    std::uint32_t ArrivalCount() const;
    std::uint32_t DiscardedCount() const;

private:

    std::vector<CellDemand>  ArrivalOrder;             // [-] - first-arrival order, coalesced
    std::uint32_t            DiscardedDemands = 0u;    // [-] - demands the ceiling turned away
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DEMANDS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The demands sampling wrote, held per cycle slot until the readback latency has elapsed.
/// note  🔴 `20` §2.1 ②: demands are read back with a latency of the **recording slot count**. Sampling a non-resident
///        cell must produce something immediately — the coarsest resident level — rather than stalling. A
///        residency system that stalls on demand has converted a memory problem into a frame-time problem.
/// note  📝 One slot more than the recording slot count, so the slot being written is never the slot being read.
///        With the two sharing a slot the drain would read demands recorded moments earlier and the latency
///        would be zero on the tick and the full depth on every other, which is worse than either.
/// tag   owning
class RequestQueue
{
public:

    static constexpr std::uint32_t SlotCount = RecordingSlotCount + 1u;   // [-] - cycle slots held

    /// 🧩 Records one demand against the rotation now sampling.
    /// in    SurfaceIndex   [-]  which surface
    /// in    CellIndex      [-]  the cell that was not resident
    /// in    RecordingIndex  [-]  the rotation being recorded
    /// cost  🚩
    /// tag   api, nonthrowing
    void Demand(std::uint32_t SurfaceIndex, std::uint32_t CellIndex, std::uint64_t RecordingIndex);

    /// 🧩 The slot a rotation writes into.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PageQueue& SlotAt(std::uint64_t RecordingIndex);

    /// 🧩 The slot a rotation writes into, for reading.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const PageQueue& SlotAt(std::uint64_t RecordingIndex) const;

    std::uint64_t RecordedCount() const;
    std::uint64_t DiscardedCount() const;

private:

    PageQueue      CycleSlots[SlotCount] = {};   // [-] - cyclic over the rotation ordinal
    std::uint64_t  RecordedDemands          = 0u;   // [-] - demands recorded this session
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE READBACK
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The readback half of the request traffic specifically — the one thing that makes the latency real.
/// note  🔴 Named apart from `RequestQueue` because the write and the read are different mechanisms at different
///        latencies: the write is a device store during sampling, and the read is a host readback of a slot
///        recorded `RecordingSlotCount` rotations ago. One component doing both would be one whose latency
///        nobody can point at.
/// note  ⚠️ `FeedbackIndex` is the retired spelling — `SKILL-Naming`'s substitution record.
/// tag   owning
class ReturnIndex
{
public:

    /// 🧩 Reads back the demands recorded a recording slot count ago.
    /// in    Requesting       [-]  the queue those demands were written into
    /// in    RecordingIndex  [-]  the rotation now being recorded
    /// out   Result          [-]  refuses with ExtentExhausted before the depth has elapsed, and with
    ///                             HostDenied when this rotation has already been drained
    /// post  the drained slot is emptied and is the slot the caller may write next
    /// note  🔴 Refusing a second drain of one rotation is not fussiness. A promotion pass that ran twice would
    ///        charge one rotation's budget twice and promote against the second charge, which reads as the
    ///        budget being half what it was declared to be.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<const PageQueue*> Drain(RequestQueue& Requesting, std::uint64_t RecordingIndex);

    /// 🧩 The rotation last drained; zero before anything has been.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t DrainedRecording() const;

    std::uint64_t DrainedCount() const;

private:

    std::uint64_t  LastDrained  = 0u;      // [-] - the rotation the last drain read for
    std::uint64_t  DrainCount   = 0u;      // [-] - drains this session
    bool           DrainCurrent = false;  // [-] - a drain has happened at all
};

}   // namespace Slate
