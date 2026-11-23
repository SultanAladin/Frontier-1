//============================================================================================================================================
//                                                          PROMOTIONSCHEDULER.H
//============================================================================================================================================
// 🧩 Two independent budgets, spent per rotation, and the ordering eviction follows when they cannot be met.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TWO MEASURES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one rotation may spend promoting tiles.
/// note  🔴 `20` §2.2: promotion is bounded by **two independent measures**, not by tile count. Transfer volume
///        alone does not bound analytic resolution, because an analytically resolved tile transfers nothing —
///        it is produced on the device from a source description. A surface carrying many analytic layers can
///        therefore consume unbounded device time while the transfer measure truthfully reports zero.
/// note  ⚠️ An unbounded promotion burst on a camera cut produces a stall exactly where it is most visible.
///        Exceeding either measure defers, and deferral is normal operation rather than an error — `86` §5.
/// tag   nonallocating, nonthrowing
struct PromotionBudget
{
    std::uint64_t  TransferBytes   = 0u;   // [B] - reconstruction from `56` and from `SurfaceDepot`
    std::uint64_t  EvaluationUnits = 0u;   // [-] - analytic resolution through `70`
};

/// 🧩 What promoting one tile would cost, against both measures at once.
/// note  📝 A tile is rarely one or the other. A surface with textured layers beneath a placed decal charges
///        transfer for the first and evaluation for the second, and both must fit — which is why the two are
///        carried together rather than resolved to whichever dominates.
/// tag   nonallocating, nonthrowing
struct PromotionCost
{
    std::uint64_t  TransferBytes   = 0u;   // [B]
    std::uint64_t  EvaluationUnits = 0u;   // [-]
};

// 📝 One analytic entry resolved into one tile is one evaluation unit. The unit is declared rather than
//    measured because `20` §6 leaves the budget open and `70` §7 leaves the per-slot resolution cost open
//    with it; what matters now is that the measure exists and is charged, not what a unit is worth in
//    microseconds. Read by this unit alone, so `00` §2 keeps it here.
inline constexpr std::uint64_t EvaluationUnitsPerEntry = 1u;   // [-] - one analytic entry, one tile

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT A PROMOTION DID
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How one considered promotion ended.
/// tag   guarantee
enum class PromotionVerdict : std::uint32_t
{
    Promoted         = 0u,   // [-] - a tile was claimed and the cost charged
    ReResolved       = 1u,   // [-] - already resident at a stale revision; resolved again into its own slot
    AlreadyResident  = 2u,   // [-] - resident at the current revision; nothing was done — `70` §2
    Deferred         = 3u,   // [-] - the budget or the index could not admit it this rotation
    VerdictCount = 4u    // [-] - the closed count, never a verdict
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  EVICTION ORDERING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One resident cell offered as an eviction candidate.
/// tag   nonallocating, nonthrowing
struct EvictionCandidate
{
    std::uint32_t  CellIndex = 0u;   // [-] - the cell holding the slot
    std::uint32_t  Level       = 0u;   // [-] - its reduction level; zero is finest
    std::uint64_t  DemandedAt  = 0u;   // [-] - the last rotation a demand named it
    std::uint64_t  PromotedAt  = 0u;   // [-] - the rotation it became resident
};

/// 🧩 Which ordering eviction follows.
/// note  🚧 `20` §6 carries this as open — least-recent, or distance from the camera. The second would need an
///        edge to `46` that `20` does not declare, and `00` §11 gates that a declared edge is a real read, so
///        acquiring one to answer a tuning question would be paying a build-order cost for a constant. Minimum
///        recently demanded ships; the row stays open.
/// tag   guarantee
enum class EvictionOrdering : std::uint32_t
{
    OldestDemanded = 0u,   // [-] - the cell nothing has sampled for longest
    FinestLevelFirst      = 1u,   // [-] - the deepest level first; coarse levels answer more samples each
    OrderingCount         = 2u    // [-] - the closed count, never an ordering
};

/// 🧩 Whether the first candidate is evicted before the second.
/// note  📐 A total order over the candidates, so the choice does not depend on which was offered first. An
///        ordering that left two candidates incomparable would evict whichever the walk reached, and the walk
///        order is a storage detail the artist would then be able to feel.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
bool PrecedesInEviction(EvictionOrdering         Declared,
                        const EvictionCandidate& Earlier,
                        const EvictionCandidate& Later);

//------------------------------------------------------------------------------------------------------------------------
//                                                    COST ESTIMATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What promoting one tile of a surface would cost, from what its layer sequence holds.
/// in    Sequence           [-]  the surface's content — `56`
/// in    TileBytes          [B]  what one stored tile occupies, apron included
/// in    DepotArtefactValid [-]  a depot artefact stands for this tile
/// out   Costing            [-]  both measures
/// note  🔴 A valid depot artefact short-circuits to **pure transfer** — `20` §2.1's second reconstruction
///        source. That is the whole reason the depot pays for itself: it converts an evaluation cost that the
///        second budget bounds tightly into a transfer cost that the first bounds loosely.
/// note  🚧 Estimated at the sequence level rather than per cell, because `56` carries no per-entry extent to
///        test a cell against. It is therefore conservative: an entry covering a tenth of the surface is
///        charged against every tile. Sharpening it needs a coverage extent per entry, which is `56` §10's
///        third open row read from this side.
/// cost  🚩
/// tag   api, nonthrowing
PromotionCost Estimate(const SurfaceLayerSequence& Sequence,
                       std::uint64_t               TileBytes,
                       bool                        DepotArtefactValid);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SCHEDULER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One rotation's promotion budget, spent in arrival order, and the ordering eviction follows.
/// note  🔴 The scheduler decides **whether** and **what to evict**; it does not decide how a tile is
///        reconstructed and never touches one. `20` §2.1 ⑤'s three reconstruction sources belong to `56`, the
///        depot and `70`, and a scheduler that reached for them would be a scheduler with an opinion about
///        content.
/// tag   owning
class PromotionScheduler
{
public:

    /// 🧩 Declares what one rotation may spend.
    /// out   Result  [-]  refuses with ContentUnsupported when both measures are zero
    /// note  A budget of zero on one measure alone is accepted deliberately: a document with no analytic
    ///        content genuinely has no evaluation to bound, and refusing it would force a fictitious number.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareBudget(const PromotionBudget& Declaring);

    /// 🧩 Declares which ordering eviction follows.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareOrdering(EvictionOrdering Declaring);

    /// 🧩 Opens one rotation, restoring the whole budget.
    /// out   Result  [-]  refuses with HostDenied when the rotation is not later than the one last opened
    /// note  🔴 The budget is per rotation and is restored here rather than accumulated. A budget that carried
    ///        its unspent remainder forward would let a still workspace bank several rotations of promotion and
    ///        spend them all on the rotation the artist finally moved — which is precisely the stall the budget
    ///        exists to prevent.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> OpenRecording(std::uint64_t RecordingIndex);

    /// 🧩 Whether a cost fits what remains of this rotation.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Accepts(const PromotionCost& Costing) const;

    /// 🧩 Charges a cost against what remains.
    /// out   Result  [-]  refuses with ExtentExhausted when it does not fit
    /// post  a rejected charge spends nothing
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Charge(const PromotionCost& Costing);

    /// 🧩 Records that one promotion was deferred — `86` §5's measure row for `20` §2.2.
    /// note  🔴 A measure and **not** a report. `86` §5: deferral against budget is ordinary operation, and a
    ///        register that appended one per deferred tile per rotation is a register nobody reads.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeferOne();

    /// 🧩 Records that one promotion was accepted.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void PromoteOne();

    EvictionOrdering       Ordering() const;
    const PromotionBudget& Declared() const;
    const PromotionBudget& Remaining() const;
    std::uint64_t          OpenedRecording() const;
    std::uint32_t          PromotedCount() const;
    std::uint32_t          DeferredCount() const;
    std::uint64_t          PromotedTotal() const;
    std::uint64_t          DeferredTotal() const;

private:

    PromotionBudget   DeclaredBudget  = {};                                 // [-] - restored every rotation
    PromotionBudget   RemainingBudget = {};                                 // [-] - what this rotation has left
    EvictionOrdering  DeclaredOrder   = EvictionOrdering::OldestDemanded;
    std::uint64_t     RecordingOpened  = 0u;                                 // [-] - the rotation now spending
    std::uint32_t     PromotedThis    = 0u;                                 // [-] - this rotation
    std::uint32_t     DeferredThis    = 0u;                                 // [-] - this rotation
    std::uint64_t     PromotedSession = 0u;                                 // [-] - the whole session
    std::uint64_t     DeferredSession = 0u;                                 // [-] - the whole session
    bool              RecordingCurrent = false;                             // [-] - OpenRecording has delivered
};

}   // namespace Slate
