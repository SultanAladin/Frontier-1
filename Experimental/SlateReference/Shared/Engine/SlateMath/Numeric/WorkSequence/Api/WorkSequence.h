//============================================================================================================================================
//                                                              WORKSEQUENCE.H
//============================================================================================================================================
// 🧩 The only thread creation in the repository — declared work, immutable inputs, results applied on the tick.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Slate
{

// 📝 No slot; never a valid record ordinal. Declared here rather than shared with `12`'s AbsentSlot, because
//    nothing reads both and a shared spelling would be a dependency edge no traversal can see — `00` §2.
inline constexpr std::uint32_t AbsentWork = 0xFFFFFFFFu;   // [-] - no record

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRIORITY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How urgently declared work is wanted, and therefore what it may starve.
/// note  🔴 At least one worker is reserved for Interactive. A residency promotion under the cursor and a
///        whole-document export are both long solves, and `34` §4 forbids the export occupying every worker.
/// tag   guarantee
enum class WorkPriority : std::uint32_t
{
    Interactive   = 0u,   // [-] - the artist is waiting and the workspace shows a gap; never starves
    Background    = 1u,   // [-] - wanted soon; the artist has not asked for it directly
    Deferred      = 2u,   // [-] - speculative — resolved because it is likely, not because asked
    PriorityCount = 3u    // [-] - the closed count, never a priority
};

/// 🧩 How one declaration ended.
/// note  ⚠️ Cancelled and Superseded are both cancellations and are reported apart, because `86` §5 rules a
///        superseded cancellation ordinary operation and a withdrawn one the requester's own decision.
/// tag   guarantee
enum class WorkConclusion : std::uint32_t
{
    Delivered  = 0u,   // [-] - the resolution delivered
    Cancelled  = 1u,   // [-] - the requester withdrew it; no result was produced
    Superseded = 2u,   // [-] - a newer declaration replaced it; no result was produced
    Rejected    = 3u    // [-] - the resolution rejected; the refusal is carried and reported through `86`
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CANCELLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a resolution reads at each of its declared cancellation points.
/// note  🔴 Cancellation is cooperative and observed only where the declaration says it is. `34` §5: a
///        cancelled declaration still runs to its next declared point and releases what it holds — a worker
///        that is simply never joined leaks its inputs, proportional to how often the artist changes their mind.
/// tag   nonallocating, nonthrowing
struct WorkCancellation
{
    const std::atomic<bool>*  CancellationSlot = nullptr;   // [-] - owned by the sequence, never by the resolution

    /// 🧩 Whether the requester has withdrawn this declaration.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool CancellationDeclared() const
    {
        return CancellationSlot != nullptr && CancellationSlot->load(std::memory_order_relaxed);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PROGRESS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a long solve reports while it runs.
/// note  🔴 Written by the resolution and **sampled** by the tick — `34` §7. A solve that pushed progress at its
///        own rate would contend with the tick for the very state the tick is presenting.
/// note  📝 The real reading is atomic and may not be lock-free on every host. It is written at declared points
///        and read once per tick, so the contention it could suffer never arises.
/// tag   owning
class WorkProgress
{
public:

    WorkProgress()                               = default;
    WorkProgress(const WorkProgress&)            = delete;
    WorkProgress& operator=(const WorkProgress&) = delete;

    /// 🧩 Declares the resolved fraction, clamped to the closed unit interval.
    /// in    Resolved  [-]  zero at the beginning, one at the end
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareFraction(double Resolved)
    {
        const double Bounded = Resolved < 0.0 ? 0.0 : (Resolved > 1.0 ? 1.0 : Resolved);
        ResolvedFraction.store(Bounded, std::memory_order_relaxed);
    }

    /// 🧩 Declares the resolved count out of the spanned count, and the fraction they imply.
    /// in    Resolved  [-]  parts completed
    /// in    Spanned   [-]  parts the solve holds; zero declares the span unknown
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareCount(std::uint64_t Resolved, std::uint64_t Spanned)
    {
        ResolvedParts.store(Resolved, std::memory_order_relaxed);
        SpannedParts.store(Spanned, std::memory_order_relaxed);

        if (Spanned != 0u)
            DeclareFraction(static_cast<double>(Resolved) / static_cast<double>(Spanned));
    }

    /// 🧩 The resolved fraction as last declared.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double Fraction() const { return ResolvedFraction.load(std::memory_order_relaxed); }

    /// 🧩 The resolved count as last declared.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t ResolvedCount() const { return ResolvedParts.load(std::memory_order_relaxed); }

    /// 🧩 The spanned count as last declared; zero declares the span unknown.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t SpannedCount() const { return SpannedParts.load(std::memory_order_relaxed); }

    /// 🧩 Returns every reading to its beginning, for a reused record.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reclaim()
    {
        ResolvedFraction.store(0.0, std::memory_order_relaxed);
        ResolvedParts.store(0u, std::memory_order_relaxed);
        SpannedParts.store(0u, std::memory_order_relaxed);
    }

private:

    std::atomic<double>         ResolvedFraction { 0.0 };   // [-] - the closed unit interval
    std::atomic<std::uint64_t>  ResolvedParts    { 0u };    // [-] - parts completed
    std::atomic<std::uint64_t>  SpannedParts     { 0u };    // [-] - parts the solve holds
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  DECLARED WORK
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One declaration of work to be resolved off the tick.
/// note  🔴 `34` §2: the resolution reads inputs that are **immutable for its whole run**. It may not read the
///        document, the tick's state, or anything in `76`. The requester captures what the work needs at
///        declaration and hands it over, which is the rule that makes every lock here unnecessary.
/// note  🔴 The resolution mutates nothing and commits nothing. It returns an Result, and the requester applies
///        it on the tick after `Drain` delivers it — `34` §3.
/// note  ⚠️ There is deliberately no field naming another declaration. `34` §4 forbids waiting on one: with a
///        bounded worker count, waiting is a deadlock that appears only under load, on someone else's machine.
/// tag   owning
struct WorkDeclaration
{
    const char*   Origin           = "";                        // [-] - static text naming document and section
    WorkPriority  Priority         = WorkPriority::Background;  // [-] - what it may starve
    bool          ProgressReported = false;                     // [-] - whether the resolution declares progress

    // 📝 The captured inputs live inside this callable, which is why they are the requester's to make immutable.
    std::function<Deliver<bool>(const WorkCancellation&, WorkProgress&)>  Resolve;   // [-] - the whole of the work
};

/// 🧩 One completed declaration, crossing back to the tick.
/// tag   nonallocating, nonthrowing
struct WorkCompletion
{
    WorkIdentity    Declared        = {};                          // [-]  - the identity Declare registered
    const char*     Origin          = "";                          // [-]  - as declared
    WorkConclusion  Completed       = WorkConclusion::Cancelled;   // [-]  - how it ended
    Refusal         Declining       = {};                          // [-]  - meaningful only when Rejected
    std::uint64_t   DeclaredIndex = 0u;                          // [-]  - declaration order; Drain sorts by it
    TickPoint       Sealed          = {};                          // [ns] - when the conclusion was recorded
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE QUEUE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Pending declarations at one priority level, claimed in declaration order.
/// note  A withdrawn declaration is struck from the order rather than erased from the middle of it, so a
///        withdrawal costs a write instead of a shift of everything behind it.
/// tag   owning
class WorkQueue
{
public:

    /// 🧩 Accepts one record ordinal at the end of the order.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Accept(std::uint32_t RecordIndex);

    /// 🧩 Reservations the earliest pending record ordinal.
    /// out   Result  [-]  refuses with ExtentExhausted when nothing is pending
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<std::uint32_t> Reserve();

    /// 🧩 Strikes one record ordinal from the order without claiming it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Withdraw(std::uint32_t RecordIndex);

    /// 🧩 How many declarations are pending here.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t PendingCount() const;

private:

    std::vector<std::uint32_t>  PendingOrder;          // [-] - record ordinals; AbsentWork where struck
    std::size_t                 ReservationIndex = 0u;     // [-] - how far the claim has walked
    std::uint32_t               PendingHeld  = 0u;     // [-] - unstruck, unclaimed entries
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The workers, their lifetime, and the dispatch order over three priority levels.
/// note  🔴 `34` §8: no thread is created anywhere in the repository except here. Every long solve in `38`,
///        `50`, `68`, `20`, `24`, `70` and `82` is declared into this and applied by its requester on the tick.
/// note  🔴 `34` §6: a result must not depend on how many workers ran it or in what order they finished. `Drain`
///        delivers completions ordered by declaration ordinal for exactly that reason — an application order
///        that followed completion order would make the same inputs produce two documents on two machines.
/// tag   owning
class WorkSequence
{
public:

    static constexpr std::uint32_t WorkerLimit = 64u;   // [-] - workers this sequence will construct

    WorkSequence();
    WorkSequence(const WorkSequence&)            = delete;
    WorkSequence& operator=(const WorkSequence&) = delete;
    ~WorkSequence();

    /// 🧩 Constructs the workers, once, at bring-up.
    /// in    RequestedWorkers  [-]  workers wanted; zero derives the count from the host
    /// in    HostTimeline      [-]  the process's one timeline, for conclusion stamps
    /// in    Reporting         [-]  where `34` §5's failures are appended
    /// out   Result           [-]  refuses with HostDenied when workers already stand
    /// post  the worker count is fixed and recorded, so `HardwareMetrics` can attribute a measurement to it
    /// note  📝 A zero request reads the count from `04`'s `PlatformInterchange`, which reports the host once
    ///        at bring-up. Nothing here decides how many workers a host should run — `34` §4 does, from a
    ///        reading this only asks for.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructWorkerSequence(std::uint32_t RequestedWorkers, const TickSequence& HostTimeline, ReportSequence& Reporting);

    /// 🧩 Declares one unit of work, to be resolved by a worker.
    /// in    Incoming  [-]  the declaration, its inputs already captured
    /// out   Result   [-]  refuses with HostDenied when no worker stands, and with ContentUnsupported when the
    ///                      declaration carries no resolution
    /// note  Declaring is not spawning. The declaration takes its place in its priority's order and a worker
    ///        claims it; nothing about the calling thread decides when.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<WorkIdentity> Declare(const WorkDeclaration& Incoming);

    /// 🧩 Withdraws one declaration, because the requester no longer wants it.
    /// in    Subject  [-]  the identity Declare registered
    /// out   Result  [-]  refuses with IdentityStale when the declaration has already completed
    /// post  the declaration concludes as Cancelled and produces no result — `34` §5
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Withdraw(WorkIdentity Subject);

    /// 🧩 Withdraws one declaration because a newer one replaces it.
    /// out   Result  [-]  refuses with IdentityStale when the declaration has already completed
    /// note  Reported apart from a withdrawal so the requester can tell the two apart. `86` §5 rules a
    ///        superseded cancellation ordinary operation, so nothing is appended to the register for it.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Supersede(WorkIdentity Subject);

    /// 🧩 Delivers every conclusion recorded since the last drain, in declaration order.
    /// out   Completions  [-]  ordered by declaration ordinal within the drain, never by finishing order
    /// note  🔴 The ordering is **within one drain** and is not a global prefix across drains. A conclusion is
    ///        delivered as soon as it is recorded, so an earlier declaration still resolving does not hold a
    ///        later one back. Holding it back would make a `Background` export block every `Interactive`
    ///        promotion declared after it, which is the starvation `34` §4 forbids outright.
    /// note  📝 `34` §6's determinism rule binds the parts of **one** split solve — recombined by declared
    ///        index, never by completion. Two independent declarations read disjoint immutable inputs, so the
    ///        order their results are applied in carries no information and cannot make two machines differ.
    /// note  🔴 Called on the tick and nowhere else. The requester applies each result here — `34` §3 — because
    ///        a worker applying its own result would linearise against `RevisionSequence` from a thread that
    ///        does not observe the tick's ordering, which `12` invariant 10 forbids.
    /// cost  🚩
    /// tag   api, nonthrowing
    const std::vector<WorkCompletion>& Drain();

    /// 🧩 One declaration's resolved fraction.
    /// out   Result  [-]  refuses with IdentityStale once the declaration has completed
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<double> Progress(WorkIdentity Subject) const;

    /// 🧩 One declaration's resolved count.
    /// out   Result  [-]  refuses with IdentityStale once the declaration has completed
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint64_t> ProgressCount(WorkIdentity Subject) const;

    /// 🧩 How many workers stand.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t WorkerCount() const;

    /// 🧩 How many workers are resolving something now.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t OccupiedWorkers() const;

    /// 🧩 How many declarations are pending across every priority.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t PendingCount() const;

    /// 🧩 Withdraws everything pending, joins every worker, and returns the sequence to its unconstructed state.
    /// post  every pending declaration has completed as Cancelled and is drainable
    /// cost  🔴
    /// tag   api, nonthrowing
    void Reclaim();

private:

    struct WorkRecord;

    static constexpr std::size_t   PrioritySpan               = static_cast<std::size_t>(WorkPriority::PriorityCount);
    static constexpr std::uint32_t InteractiveReservedIndex = 0u;

    void          Serve(std::uint32_t WorkerIndex);
    bool          Reservable(std::uint32_t WorkerIndex) const;
    std::uint32_t Reserve(std::uint32_t WorkerIndex);
    void          Seal(std::uint32_t RecordIndex, const Deliver<bool>& Resolved);
    Deliver<bool> Cancel(WorkIdentity Subject, bool SupersessionPosed);
    std::uint32_t Resolved(WorkIdentity Subject) const;

    std::vector<std::thread>                   Workers;                     // [-] - fixed at Construct
    std::vector<std::unique_ptr<WorkRecord>>   Records;                     // [-] - one per declaration slot
    std::vector<std::uint32_t>                 ReleasedIndexs;            // [-] - slots free for reuse
    WorkQueue                                  PendingByPriority[PrioritySpan] = {};
    std::vector<WorkCompletion>                SealedCompletions;           // [-] - awaiting the next Drain
    std::vector<WorkCompletion>                DrainedCompletions;          // [-] - what the last Drain returned
    const TickSequence*                        Timeline            = nullptr; // [-] - the process's own
    ReportSequence*                            Reporting           = nullptr; // [-] - `34` §5's destination
    mutable std::mutex                         WorkGuard;                   // [-] - guards everything above
    std::condition_variable                    ArrivalDeclared;             // [-] - a declaration or a teardown
    std::uint64_t                              DeclaredCount       = 0u;    // [-] - declarations this session
    std::uint32_t                              SpannedWorkers      = 0u;    // [-] - workers standing
    std::uint32_t                              OccupiedWorkerCount = 0u;    // [-] - workers resolving
    bool                                       TeardownDeclared    = false; // [-] - workers must return
};

}   // namespace Slate
